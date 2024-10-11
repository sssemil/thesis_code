#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <cerrno>
#include <fstream>
#include <ctime>
#include <unordered_map>
#include <poll.h>

#include "static_config.hpp"
#include "thread_utils.hpp"

using namespace std;

std::chrono::steady_clock::time_point client_start_time;

struct Metrics {
    double timestamp;
    int64_t requests_completed;
    double throughput; // requests per second
    double gbit_per_second;
};

struct ThreadResult {
    int64_t total_requests_completed;
    int64_t total_bytes_sent;
    int64_t total_bytes_received;
    double duration;
    std::vector<std::vector<Metrics>> per_second_metrics; // per_second_metrics[conn_index][]
};

bool setup_buffers(char *&send_buffers, char *&recv_buffers) {
    if (config.use_aligned_allocations) {
        if (posix_memalign((void **) &send_buffers, 4096, config.inflight_ops * 4) != 0 ||
            posix_memalign((void **) &recv_buffers, 4096, config.inflight_ops * config.page_size) != 0) {
            perror("posix_memalign");
            return false;
        }
    } else {
        send_buffers = new char[config.inflight_ops * 4];
        recv_buffers = new char[config.inflight_ops * config.page_size];
    }

    if (config.alloc_pin) {
        if (mlock(send_buffers, config.inflight_ops * 4) ||
            mlock(recv_buffers, config.inflight_ops * config.page_size)) {
            perror("mlock");
        }
    }

    for (int i = 0; i < config.inflight_ops; ++i) {
        memcpy(send_buffers + i * 4, "TEST", 4);
    }

    return true;
}

void cleanup_buffers(char *send_buffers, char *recv_buffers) {
    if (config.alloc_pin) {
        munlock(send_buffers, config.inflight_ops * 4);
        munlock(recv_buffers, config.inflight_ops * config.page_size);
    }

    if (config.use_aligned_allocations) {
        free(send_buffers);
        free(recv_buffers);
    } else {
        delete[] send_buffers;
        delete[] recv_buffers;
    }
}

void client_handle_connection(const int thread_id, ThreadResult &result,
                              char *send_buffers, char *recv_buffers, std::vector<int>& connections,
                              std::unordered_map<int, int>& fd_to_conn_index) {
    int num_connections = connections.size();
    std::vector<int64_t> total_bytes_sent(num_connections, 0);
    std::vector<int64_t> total_bytes_received(num_connections, 0);
    std::vector<int64_t> bytes_sent_since_last_report(num_connections, 0);
    std::vector<int64_t> bytes_received_since_last_report(num_connections, 0);
    std::vector<int64_t> requests_completed_since_last_report(num_connections, 0);
    std::vector<int64_t> requests_completed(num_connections, 0);

    int64_t total_requests_completed = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;
    std::chrono::time_point<std::chrono::steady_clock> now;

    std::vector<pollfd> poll_fds(num_connections);
    for (int i = 0; i < num_connections; ++i) {
        poll_fds[i].fd = connections[i];
        poll_fds[i].events = POLLIN | POLLOUT;
    }

    std::vector<bool> is_sending(num_connections, true);
    std::vector<int> buffer_indices(num_connections, 0);

    while (true) {
        now = std::chrono::steady_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(now - client_start_time).count();
        if (elapsed_seconds >= config.run_duration_seconds) {
            break;
        }

        int ready = poll(poll_fds.data(), num_connections, 1000); // 1 second timeout
        if (ready == -1) {
            perror("poll");
            break;
        }

        for (int i = 0; i < num_connections; ++i) {
            int conn_fd = connections[i];
            int conn_index = fd_to_conn_index[conn_fd];

            if (poll_fds[i].revents & POLLIN) {
                char* recv_buffer = recv_buffers + buffer_indices[i] * config.page_size;
                int bytes_received = recv(conn_fd, recv_buffer, config.page_size, 0);
                if (bytes_received > 0) {
                    total_bytes_received[conn_index] += bytes_received;
                    bytes_received_since_last_report[conn_index] += bytes_received;
                    ++requests_completed[conn_index];
                    ++total_requests_completed;
                    is_sending[i] = true;
                } else if (bytes_received == 0 || (bytes_received == -1 && errno != EAGAIN)) {
                    // Connection closed or error
                    close(conn_fd);
                    connections[i] = -1;
                    poll_fds[i].fd = -1;
                }
            }

            if (poll_fds[i].revents & POLLOUT && is_sending[i]) {
                char* send_buffer = send_buffers + buffer_indices[i] * 4;
                int bytes_sent = send(conn_fd, send_buffer, 4, 0);
                if (bytes_sent > 0) {
                    total_bytes_sent[conn_index] += bytes_sent;
                    bytes_sent_since_last_report[conn_index] += bytes_sent;
                    is_sending[i] = false;
                } else if (bytes_sent == -1 && errno != EAGAIN) {
                    // Error
                    close(conn_fd);
                    connections[i] = -1;
                    poll_fds[i].fd = -1;
                }
            }

            buffer_indices[i] = (buffer_indices[i] + 1) % config.inflight_ops;
        }

        now = std::chrono::steady_clock::now();
        double time_since_last_report = std::chrono::duration<double>(now - last_report_time).count();
        if (time_since_last_report >= 1.0) {
            double segment_duration = time_since_last_report;

            for (int i = 0; i < num_connections; ++i) {
                double conn_throughput = (requests_completed[i] - requests_completed_since_last_report[i]) / segment_duration;

                double data_transferred_bits =
                    (bytes_sent_since_last_report[i] + bytes_received_since_last_report[i]) * 8;
                double conn_gbit_per_second = data_transferred_bits / (segment_duration * 1e9);

                cout << "Client thread " << thread_id << ", connection " << i << " completed "
                     << requests_completed[i] << " requests. Throughput: " << conn_throughput
                     << " it/s, " << conn_gbit_per_second << " Gbit/s." << endl;

                Metrics m;
                m.timestamp = std::chrono::duration<double>(now - client_start_time).count();
                m.requests_completed = requests_completed[i];
                m.throughput = conn_throughput;
                m.gbit_per_second = conn_gbit_per_second;
                result.per_second_metrics[i].push_back(m);

                bytes_sent_since_last_report[i] = 0;
                bytes_received_since_last_report[i] = 0;
                requests_completed_since_last_report[i] = requests_completed[i];
            }

            last_report_time = now;
        }
    }

    result.total_requests_completed = total_requests_completed;
    result.total_bytes_sent = 0;
    result.total_bytes_received = 0;
    for (int i = 0; i < num_connections; ++i) {
        result.total_bytes_sent += total_bytes_sent[i];
        result.total_bytes_received += total_bytes_received[i];
    }

    cout << "Completed total of " << total_requests_completed << " requests." << endl;
}

void client_thread(const int thread_id, ThreadResult &result) {
    cout << "Client thread " << thread_id << " started." << endl;
    if (!set_thread_affinity(thread_id)) {
        std::cerr << "set_thread_affinity failed: " << thread_id << std::endl;
    }

    std::vector<int> connections;
    for (int i = 0; i < config.connections_per_thread; ++i) {
        int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sock_fd < 0) {
            perror("socket");
            continue;
        }

        if (!config.enable_nagle) {
            int flag = 1;
            if (setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
                perror("setsockopt TCP_NODELAY");
            }
        }

        if (config.increase_socket_buffers) {
            int buf_size = 4 * 1024 * 1024; // 4MB
            if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
                perror("setsockopt SO_SNDBUF");
            }
            if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
                perror("setsockopt SO_RCVBUF");
            }
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        inet_pton(AF_INET, config.server_addr.c_str(), &addr.sin_addr);

        if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
            perror("connect");
            close(sock_fd);
            continue;
        }

        cout << "Client thread " << thread_id << " connected to server on fd " << sock_fd << "." << endl;
        connections.push_back(sock_fd);
    }

    std::unordered_map<int, int> fd_to_conn_index;
    for (int i = 0; i < connections.size(); ++i) {
        fd_to_conn_index[connections[i]] = i;
    }

    result.per_second_metrics.resize(connections.size());

    char *send_buffers;
    char *recv_buffers;

    if (!setup_buffers(send_buffers, recv_buffers)) {
        for (int fd : connections) {
            close(fd);
        }
        return;
    }

    client_handle_connection(thread_id, result, send_buffers, recv_buffers, connections, fd_to_conn_index);

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration<double>(end_time - client_start_time).count();

    cleanup_buffers(send_buffers, recv_buffers);

    for (int fd : connections) {
        close(fd);
    }

    cout << "Client thread " << thread_id << " exiting." << endl;
}

int main() {
    config.load_from_env();

    cout << "Client starting..." << endl;

    client_start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> clients;
    std::vector<ThreadResult> thread_results(config.thread_count);

    for (int i = 0; i < config.thread_count; ++i) {
        clients.emplace_back(client_thread, i, std::ref(thread_results[i]));
    }

    for (auto &client: clients) {
        client.join();
    }

    int64_t total_requests_completed = 0;
    int64_t total_bytes_sent = 0;
    int64_t total_bytes_received = 0;
    double total_duration = 0;

    for (const auto &result: thread_results) {
        total_requests_completed += result.total_requests_completed;
        total_bytes_sent += result.total_bytes_sent;
        total_bytes_received += result.total_bytes_received;
        if (result.duration > total_duration) {
            total_duration = result.duration;
        }
    }

    double total_throughput = total_requests_completed / total_duration;
    double total_data_transferred_bits = (total_bytes_sent + total_bytes_received) * 8;
    double total_gbit_per_second = total_data_transferred_bits / (total_duration * 1e9);

    cout << "All client threads completed. Total requests: " << total_requests_completed
         << " in " << total_duration << " seconds." << endl;
    cout << "Aggregate Throughput: " << total_throughput << " it/s, "
         << total_gbit_per_second << " Gbit/s." << endl;

    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    char datetime_buffer[100];
    std::strftime(datetime_buffer, sizeof(datetime_buffer), "%Y-%m-%d_%H-%M-%S", std::localtime(&now_time_t));
    std::string datetime_str(datetime_buffer);

    std::string metrics_filename = "report_client_" + datetime_str + ".csv";
    std::ofstream metrics_file(metrics_filename);
    metrics_file << "timestamp,thread_id,connection_num,requests_completed,throughput,gbit_per_second\n";
    for (int thread_id = 0; thread_id < thread_results.size(); ++thread_id) {
        const auto& per_second_metrics = thread_results[thread_id].per_second_metrics;
        for (int conn_index = 0; conn_index < per_second_metrics.size(); ++conn_index) {
            const auto& conn_metrics = per_second_metrics[conn_index];
            for (const auto& m : conn_metrics) {
                metrics_file << m.timestamp << "," << thread_id << "," << conn_index << "," << m.requests_completed << ","
                             << m.throughput << "," << m.gbit_per_second << "\n";
            }
        }
    }
    metrics_file.close();

    std::string config_filename = "report_client_" + datetime_str + "_env";
    config.save_to_file(config_filename);
    cout << "Client finished." << endl;
    return 0;
}