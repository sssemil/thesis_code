#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/mman.h>
#include <chrono>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <fstream>
#include <ctime>
#include <unordered_map>
#include <poll.h>

#include "static_config.hpp"
#include "thread_utils.hpp"

using namespace std;

int thread_count = -1;
int next_thread = 0;
std::vector<std::vector<uint16_t>> connection_fds;
std::atomic<bool> accepting_connections(true);

std::chrono::steady_clock::time_point server_start_time;
std::atomic<bool> timer_started(false);

struct Metrics {
    double timestamp;
    int64_t message_count;
    double throughput;
    double gbit_per_second;
};

struct ThreadResult {
    int64_t total_message_count;
    int64_t total_bytes_sent;
    int64_t total_bytes_received;
    double duration;
    std::vector<std::vector<Metrics>> per_second_metrics;
};

void accept_connections(const int listen_fd) {
    cout << "Acceptor thread started." << endl;
    while (accepting_connections.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept4(listen_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
        if (conn_fd >= 0) {
            cout << "Accepted connection: fd=" << conn_fd << endl;

            if (!config.enable_nagle) {
                int flag = 1;
                setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
            }

            if (config.increase_socket_buffers) {
                int buf_size = 4 * 1024 * 1024;
                setsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
                setsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
            }

            if (!timer_started.load()) {
                server_start_time = std::chrono::steady_clock::now();
                timer_started.store(true);
                cout << "Server timer started." << endl;
            }

            int assigned_thread = next_thread % thread_count;
            cout << "Adding fd " << conn_fd << " to thread " << assigned_thread << endl;
            connection_fds[assigned_thread].push_back(conn_fd);
            next_thread++;
        } else {
            if (errno != EINTR && errno != EAGAIN) {
                perror("accept");
                accepting_connections = false;
                break;
            }

            if (timer_started.load()) {
                auto now = std::chrono::steady_clock::now();
                double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
                if (elapsed_seconds >= config.run_duration_seconds) {
                    cout << "Time limit reached. Stopping acceptor thread." << endl;
                    accepting_connections = false;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    cout << "Acceptor thread exiting." << endl;
}

void handle_connection(const int thread_id, ThreadResult& result) {
    int num_connections = connection_fds[thread_id].size();
    std::vector<int64_t> message_count(num_connections, 0);
    std::vector<int64_t> total_bytes_sent(num_connections, 0);
    std::vector<int64_t> total_bytes_received(num_connections, 0);
    std::vector<int64_t> bytes_sent_since_last_report(num_connections, 0);
    std::vector<int64_t> bytes_received_since_last_report(num_connections, 0);
    std::vector<int64_t> messages_since_last_report(num_connections, 0);

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    std::vector<char> recv_buffer(4);
    std::vector<char> send_buffer(config.page_size);

    std::vector<pollfd> poll_fds(num_connections);
    for (int i = 0; i < num_connections; ++i) {
        poll_fds[i].fd = connection_fds[thread_id][i];
        poll_fds[i].events = POLLIN | POLLOUT;
    }

    while (accepting_connections.load()) {
        if (timer_started.load()) {
            auto now = std::chrono::steady_clock::now();
            double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
            if (elapsed_seconds >= config.run_duration_seconds) {
                cout << "Time limit reached. Worker thread " << thread_id << " closing connection." << endl;
                break;
            }
        }

        int ready = poll(poll_fds.data(), num_connections, 1000);
        if (ready < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < num_connections; ++i) {
            if (poll_fds[i].revents & POLLIN) {
                ssize_t bytes_received = recv(poll_fds[i].fd, recv_buffer.data(), recv_buffer.size(), 0);
                if (bytes_received > 0) {
                    total_bytes_received[i] += bytes_received;
                    bytes_received_since_last_report[i] += bytes_received;
                } else if (bytes_received == 0 || (bytes_received < 0 && errno != EAGAIN)) {
                    close(poll_fds[i].fd);
                    poll_fds[i].fd = -1;
                    continue;
                }
            }

            if (poll_fds[i].revents & POLLOUT) {
                ssize_t bytes_sent = send(poll_fds[i].fd, send_buffer.data(), send_buffer.size(), 0);
                if (bytes_sent > 0) {
                    total_bytes_sent[i] += bytes_sent;
                    bytes_sent_since_last_report[i] += bytes_sent;
                    message_count[i]++;
                } else if (bytes_sent < 0 && errno != EAGAIN) {
                    close(poll_fds[i].fd);
                    poll_fds[i].fd = -1;
                    continue;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        double time_since_last_report = std::chrono::duration<double>(now - last_report_time).count();
        if (time_since_last_report >= 1.0) {
            double segment_duration = time_since_last_report;

            for (int i = 0; i < num_connections; ++i) {
                double conn_throughput = (message_count[i] - messages_since_last_report[i]) / segment_duration;
                double data_transferred_bits = (bytes_sent_since_last_report[i] + bytes_received_since_last_report[i]) * 8;
                double conn_gbit_per_second = data_transferred_bits / (segment_duration * 1e9);

                cout << "Thread " << thread_id << ", connection " << i << " processed "
                     << message_count[i] << " messages. Throughput: " << conn_throughput
                     << " it/s, " << conn_gbit_per_second << " Gbit/s." << endl;

                Metrics m;
                m.timestamp = std::chrono::duration<double>(now - start_time).count();
                m.message_count = message_count[i];
                m.throughput = conn_throughput;
                m.gbit_per_second = conn_gbit_per_second;
                result.per_second_metrics[i].push_back(m);

                bytes_sent_since_last_report[i] = 0;
                bytes_received_since_last_report[i] = 0;
                messages_since_last_report[i] = message_count[i];
            }
            last_report_time = now;
        }
    }

    for (int i = 0; i < num_connections; ++i) {
        if (poll_fds[i].fd != -1) {
            close(poll_fds[i].fd);
        }
    }

    result.total_message_count = 0;
    result.total_bytes_sent = 0;
    result.total_bytes_received = 0;
    for (int i = 0; i < num_connections; ++i) {
        result.total_message_count += message_count[i];
        result.total_bytes_sent += total_bytes_sent[i];
        result.total_bytes_received += total_bytes_received[i];
    }
}

void worker_thread(const int thread_id, ThreadResult& result) {
    cout << "Worker thread " << thread_id << " started." << endl;

    if (!set_thread_affinity(thread_id)) {
        std::cerr << "set_thread_affinity failed: " << thread_id << std::endl;
    }

    result.per_second_metrics.resize(config.connections_per_thread);

    auto start_time = std::chrono::steady_clock::now();

    while (accepting_connections.load()) {
        if (connection_fds[thread_id].size() < config.connections_per_thread) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        cout << "Worker thread " << thread_id << " handling connections" << endl;

        handle_connection(thread_id, result);

        if (timer_started.load()) {
            auto now = std::chrono::steady_clock::now();
            double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
            if (elapsed_seconds >= config.run_duration_seconds) {
                cout << "Time limit reached. Worker thread " << thread_id << " exiting." << endl;
                break;
            }
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration<double>(end_time - start_time).count();

    cout << "Worker thread " << thread_id << " processed " << result.total_message_count << " messages in "
         << result.duration << " seconds. Total Throughput: " << (result.total_message_count / result.duration) << " it/s, "
         << ((result.total_bytes_sent + result.total_bytes_received) * 8 / (result.duration * 1e9)) << " Gbit/s."
         << endl;
    cout << "Sent throughput: " << (result.total_bytes_sent * 8 / (result.duration * 1e9)) << " Gbit/s." << endl;
    cout << "Recv throughput: " << (result.total_bytes_received * 8 / (result.duration * 1e9)) << " Gbit/s." << endl;

    cout << "Worker thread " << thread_id << " exiting." << endl;
}

int main() {
    config.load_from_env();

    thread_count = config.thread_count;
    for (int i = 0; i < thread_count; ++i) {
        connection_fds.push_back(std::vector<uint16_t>());
    }

    cout << "Server starting..." << endl;

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    cout << "Server listening on port " << config.port << "." << endl;

    std::thread acceptor(accept_connections, listen_fd);

    std::vector<std::thread> workers;
    std::vector<ThreadResult> thread_results(config.thread_count);
    for (int i = 0; i < config.thread_count; ++i) {
        thread_results[i].per_second_metrics.resize(config.connections_per_thread);
    }

    for (int i = 0; i < config.thread_count; ++i) {
        workers.emplace_back(worker_thread, i, std::ref(thread_results[i]));
    }

    for (auto& worker : workers) {
        worker.join();
    }

    int64_t total_messages_processed = 0;
    int64_t total_bytes_sent = 0;
    int64_t total_bytes_received = 0;
    double total_duration = 0;

    for (const auto& result : thread_results) {
        total_messages_processed += result.total_message_count;
        total_bytes_sent += result.total_bytes_sent;
        total_bytes_received += result.total_bytes_received;
        if (result.duration > total_duration) {
            total_duration = result.duration;
        }
    }

    double total_throughput = total_messages_processed / total_duration;
    double total_data_transferred_bits = (total_bytes_sent + total_bytes_received) * 8;
    double total_gbit_per_second = total_data_transferred_bits / (total_duration * 1e9);

    cout << "All worker threads completed. Total messages: " << total_messages_processed
         << " in " << total_duration << " seconds." << endl;
    cout << "Aggregate Throughput: " << total_throughput << " it/s, "
         << total_gbit_per_second << " Gbit/s." << endl;

    auto now_system = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now_system);
    char datetime_buffer[100];
    std::strftime(datetime_buffer, sizeof(datetime_buffer), "%Y-%m-%d_%H-%M-%S", std::localtime(&now_time_t));
    std::string datetime_str(datetime_buffer);

    std::string metrics_filename = "report_server_" + datetime_str + ".csv";
    std::ofstream metrics_file(metrics_filename);
    metrics_file << "timestamp,thread_id,connection_num,message_count,throughput,gbit_per_second\n";
    for (int thread_id = 0; thread_id < thread_results.size(); ++thread_id) {
        const auto& per_second_metrics = thread_results[thread_id].per_second_metrics;
        for (int conn_index = 0; conn_index < per_second_metrics.size(); ++conn_index) {
            const auto& conn_metrics = per_second_metrics[conn_index];
            for (const auto& m : conn_metrics) {
                metrics_file << m.timestamp << "," << thread_id << "," << conn_index << "," << m.message_count << ","
                             << m.throughput << "," << m.gbit_per_second << "\n";
            }
        }
    }
    metrics_file.close();

    std::string config_filename = "report_server_" + datetime_str + "_env";
    config.save_to_file(config_filename);

    close(listen_fd);

    if (acceptor.joinable()) {
        acceptor.join();
    }

    cout << "Server shutting down." << endl;
    return 0;
}