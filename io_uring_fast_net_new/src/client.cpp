#include <arpa/inet.h>
#include <iostream>
#include <liburing.h>
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

#include "static_config.hpp"
#include "thread_utils.hpp"

using namespace std;

std::chrono::steady_clock::time_point client_start_time;

struct Metrics {
    double timestamp;
    int64_t requests_completed;
    double throughput; 
    double gbit_per_second;
};

struct ThreadResult {
    int64_t total_requests_completed;
    int64_t total_bytes_sent;
    int64_t total_bytes_received;
    double duration;
    std::vector<std::vector<Metrics>> per_second_metrics; 
};

struct UserData {
    uint32_t buffer_idx;
    bool is_send;
    uint16_t fd;
};

uint64_t pack_user_data(const UserData& data) {
    uint64_t result = 0;
    result |= (uint64_t)(data.buffer_idx) & 0xFFFFFFFFULL; // bits 0..31
    result |= ((uint64_t)(data.is_send ? 1 : 0) & 0x1ULL) << 32; // bit 32
    result |= ((uint64_t)(data.fd) & 0xFFFFULL) << 33; // bits 33..48
    return result;
}

UserData unpack_user_data(uint64_t user_data) {
    UserData data;
    data.buffer_idx = (uint32_t)(user_data & 0xFFFFFFFFULL);
    data.is_send = ((user_data >> 32) & 0x1ULL) != 0;
    data.fd = (uint16_t)((user_data >> 33) & 0xFFFFULL);
    return data;
}

bool setup_io_uring(struct io_uring &ring) {
    int ret = io_uring_queue_init(config.queue_depth, &ring, 0);
    if (ret) {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << std::endl;
        return false;
    }
    return true;
}

bool setup_buffers(struct io_uring &ring, char *&send_buffers, char *&recv_buffers) {
    int ret;

    if (config.use_aligned_allocations) {
        if (posix_memalign((void **) &send_buffers, 4096, config.inflight_ops * 4) != 0 ||
            posix_memalign((void **) &recv_buffers, 4096, config.inflight_ops * config.page_size) != 0) {
            perror("posix_memalign");
            io_uring_queue_exit(&ring);
            return false;
        }
    } else {
        send_buffers = new char[config.inflight_ops * 4];
        recv_buffers = new char[config.inflight_ops * config.page_size];
    }

    if (config.alloc_pin) {
        ret = mlock(send_buffers, config.inflight_ops * 4);
        if (ret) {
            perror("mlock send_buffers");
        }
        ret = mlock(recv_buffers, config.inflight_ops * config.page_size);
        if (ret) {
            perror("mlock recv_buffers");
        }
    }

    struct iovec *iovecs = new struct iovec[config.inflight_ops * 2];
    for (int i = 0; i < config.inflight_ops; ++i) {
        memcpy(send_buffers + i * 4, "TEST", 4);
        iovecs[i].iov_base = send_buffers + i * 4;
        iovecs[i].iov_len = 4;

        iovecs[config.inflight_ops + i].iov_base = recv_buffers + i * config.page_size;
        iovecs[config.inflight_ops + i].iov_len = config.page_size;
    }

    ret = io_uring_register_buffers(&ring, iovecs, config.inflight_ops * 2);
    delete[] iovecs;

    if (ret < 0) {
        std::cerr << "io_uring_register_buffers: " << strerror(-ret) << std::endl;
        io_uring_queue_exit(&ring);
        if (config.use_aligned_allocations) {
            munlock(send_buffers, config.inflight_ops * 4);
            munlock(recv_buffers, config.inflight_ops * config.page_size);
            free(send_buffers);
            free(recv_buffers);
        } else {
            delete[] send_buffers;
            delete[] recv_buffers;
        }
        exit(-1);
    }

    return true;
}

void cleanup_buffers(struct io_uring &ring, char *send_buffers, char *recv_buffers) {
    io_uring_queue_exit(&ring);

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

void client_handle_connection(const int thread_id, ThreadResult &result, struct io_uring &ring,
                              char *send_buffers, char *recv_buffers, std::vector<int>& connections,
                              std::unordered_map<int, int>& fd_to_conn_index) {
    int ret;
    int inflight = 0;
    int64_t total_requests_completed = 0;

    int num_connections = connections.size();

    std::vector<int64_t> total_bytes_sent(num_connections, 0);
    std::vector<int64_t> total_bytes_received(num_connections, 0);
    std::vector<int64_t> bytes_sent_since_last_report(num_connections, 0);
    std::vector<int64_t> bytes_received_since_last_report(num_connections, 0);
    std::vector<int64_t> requests_completed_since_last_report(num_connections, 0);
    std::vector<int64_t> requests_completed(num_connections, 0);

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;
    std::chrono::time_point<std::chrono::steady_clock> now;
    double elapsed_seconds, segment_duration;

    int sqes_to_submit = 0;

    while (connections.size() < config.connections_per_thread) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    cout << "Thread " << thread_id << " has " << num_connections << " connections." << endl;

    for (int i = 0; i < config.inflight_ops; ++i) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            std::cerr << "io_uring_get_sqe failed" << std::endl;
            break;
        }
        int buffer_index = i % config.inflight_ops;

        int conn_fd = connections[i % num_connections];

        if (config.half_duplex_mode) {
            io_uring_prep_read_fixed(sqe, conn_fd, recv_buffers + buffer_index * config.page_size, config.page_size, 0,
                                     config.inflight_ops + buffer_index);
            UserData data;
            data.buffer_idx = buffer_index;
            data.is_send = false;
            data.fd = conn_fd;
            sqe->user_data = pack_user_data(data);
        } else {
            io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_index * 4, 4, 0);
            UserData data;
            data.buffer_idx = buffer_index;
            data.is_send = true;
            data.fd = conn_fd;
            sqe->user_data = pack_user_data(data);
        }
        ++sqes_to_submit;
        ++inflight;
    }

    if (sqes_to_submit > 0) {
        ret = io_uring_submit(&ring);
        if (ret < 0) {
            std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
        }
        sqes_to_submit = 0;
    }

    while (true) {
        now = std::chrono::steady_clock::now();
        elapsed_seconds = std::chrono::duration<double>(now - client_start_time).count();
        if (elapsed_seconds >= config.run_duration_seconds && inflight == 0) {
            cout << "Time limit reached. Client thread " << thread_id << " exiting loop." << endl;
            break;
        }

        struct io_uring_cqe *cqe;
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << std::endl;
            break;
        }

        UserData data = unpack_user_data(cqe->user_data);
        uint32_t buffer_index = data.buffer_idx;
        bool is_send = data.is_send;
        int conn_fd = data.fd;

        int conn_index = fd_to_conn_index[conn_fd];

        if (cqe->res < 0) {
            if (cqe->res == -EAGAIN) {
                struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                if (!sqe) {
                    std::cerr << "io_uring_get_sqe failed" << std::endl;
                    break;
                }
                if (is_send) {
                    io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_index * 4, 4, 0);
                    sqe->user_data = pack_user_data(data);
                } else {
                    if (config.half_duplex_mode) {
                        io_uring_prep_read_fixed(sqe, conn_fd, recv_buffers + buffer_index * config.page_size,
                                                 config.page_size, 0, config.inflight_ops + buffer_index);
                    } else {
                        io_uring_prep_recv(sqe, conn_fd, recv_buffers + buffer_index * config.page_size,
                                           config.page_size, 0);
                    }
                    sqe->user_data = pack_user_data(data);
                }
                ++sqes_to_submit;
            } else if (cqe->res == -ECONNRESET || cqe->res == -EPIPE) {
                if (config.verbose) cout << "Connection closed by server on fd " << conn_fd << endl;
                --inflight;
                break;
            } else {
                std::cerr << "Operation error: " << strerror(-cqe->res) << std::endl;
                --inflight;
                break;
            }
        } else if (cqe->res == 0) {
            cout << "Connection closed by server on fd " << conn_fd << endl;
            --inflight;
            break;
        } else {
            if (is_send) {
                total_bytes_sent[conn_index] += cqe->res;
                bytes_sent_since_last_report[conn_index] += cqe->res;

                if (config.half_duplex_mode) {
                    std::cerr << "Unexpected send completion in half-duplex mode" << std::endl;
                } else {
                    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    if (!sqe) {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        break;
                    }
                    io_uring_prep_recv(sqe, conn_fd, recv_buffers + buffer_index * config.page_size, config.page_size, 0);
                    sqe->user_data = pack_user_data({buffer_index, false, (uint16_t)conn_fd});
                    ++sqes_to_submit;
                }
            } else {
                int bytes_received = cqe->res;
                if (config.verbose)
                    cout << "Thread " << thread_id << " received " << bytes_received << " bytes on connection " << conn_index << "." << endl;

                total_bytes_received[conn_index] += bytes_received;
                bytes_received_since_last_report[conn_index] += bytes_received;

                ++requests_completed[conn_index];
                ++total_requests_completed;

                if (elapsed_seconds < config.run_duration_seconds) {
                    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    if (!sqe) {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        break;
                    }
                    if (config.half_duplex_mode) {
                        io_uring_prep_read_fixed(sqe, conn_fd, recv_buffers + buffer_index * config.page_size,
                                                 config.page_size, 0, config.inflight_ops + buffer_index);
                        sqe->user_data = pack_user_data(data);
                    } else {
                        io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_index * 4, 4, 0);
                        sqe->user_data = pack_user_data({buffer_index, true, (uint16_t)conn_fd});
                        ++inflight;
                    }
                    ++sqes_to_submit;
                } else {
                    --inflight;
                }
            }
        }

        io_uring_cqe_seen(&ring, cqe);

        now = std::chrono::steady_clock::now();
        double time_since_last_report = std::chrono::duration<double>(now - last_report_time).count();
        if (time_since_last_report >= 1.0) {
            segment_duration = time_since_last_report;

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

        if (sqes_to_submit > 0) {
            ret = io_uring_submit(&ring);
            if (ret < 0) {
                std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
                break;
            }
            sqes_to_submit = 0;
        }

        if (inflight == 0 && elapsed_seconds >= config.run_duration_seconds) {
            break;
        }
    }

    result.total_requests_completed = total_requests_completed;
    result.total_bytes_sent = 0;
    result.total_bytes_received = 0;
    for (int i = 0; i < num_connections; ++i) {
        result.total_bytes_sent += total_bytes_sent[i];
        result.total_bytes_received += total_bytes_received[i];
    }

    if (config.half_duplex_mode) {
        cout << "Half-duplex mode: Received total of " << total_requests_completed << " pages from server." << endl;
    } else {
        cout << "Full-duplex mode: Completed total of " << total_requests_completed << " requests." << endl;
    }
}

void client_thread(const int thread_id, ThreadResult &result) {
    cout << "Client thread " << thread_id << " started in " << (config.half_duplex_mode ? "half-duplex" : "full-duplex")
         << " mode." << endl;
    if (!set_thread_affinity(thread_id)) {
        std::cerr << "set_thread_affinity failed: " << thread_id << std::endl;
    }

    int ret;

    std::vector<int> connections;
    for (int i = 0; i < config.connections_per_thread; ++i) {
        int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sock_fd < 0) {
            perror("socket");
            continue;
        }

        if (!config.enable_nagle) {
            int flag = 1;
            ret = setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
            if (ret < 0) {
                perror("setsockopt TCP_NODELAY");
            }
        }

        if (config.increase_socket_buffers) {
            int buf_size = 4 * 1024 * 1024; 
            ret = setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
            if (ret < 0) {
                perror("setsockopt SO_SNDBUF");
            }
            ret = setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
            if (ret < 0) {
                perror("setsockopt SO_RCVBUF");
            }
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        inet_pton(AF_INET, config.server_addr.c_str(), &addr.sin_addr);

        ret = connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr));
        if (ret < 0 && errno != EINPROGRESS) {
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

    struct io_uring ring;

    if (!setup_io_uring(ring)) {
        for (int fd : connections) {
            close(fd);
        }
        return;
    }

    char *send_buffers;
    char *recv_buffers;

    if (!setup_buffers(ring, send_buffers, recv_buffers)) {
        for (int fd : connections) {
            close(fd);
        }
        return;
    }

    client_handle_connection(thread_id, result, ring, send_buffers, recv_buffers, connections, fd_to_conn_index);

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration<double>(end_time - client_start_time).count();

    cleanup_buffers(ring, send_buffers, recv_buffers);

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
