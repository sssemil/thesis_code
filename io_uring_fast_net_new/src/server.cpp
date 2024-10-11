#include <iostream>
#include <liburing.h>
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

#include "static_config.hpp"
#include "thread_utils.hpp"

using namespace std;

int thread_count = -1;
int next_thread = 0;
std::vector<std::vector<uint16_t>> connection_fds;
std::atomic<bool> accepting_connections(true);

struct UserData
{
    uint32_t buffer_idx;
    bool is_send;
    uint16_t fd;
};

uint64_t pack_user_data(const UserData& data)
{
    uint64_t result = 0;
    result |= (uint64_t)(data.buffer_idx) & 0xFFFFFFFFULL; // bits 0..31
    result |= ((uint64_t)(data.is_send ? 1 : 0) & 0x1ULL) << 32; // bit 32
    result |= ((uint64_t)(data.fd) & 0xFFFFULL) << 33; // bits 33..48
    return result;
}

UserData unpack_user_data(uint64_t user_data)
{
    UserData data;
    data.buffer_idx = (uint32_t)(user_data & 0xFFFFFFFFULL);
    data.is_send = ((user_data >> 32) & 0x1ULL) != 0;
    data.fd = (uint16_t)((user_data >> 33) & 0xFFFFULL);
    return data;
}

std::chrono::steady_clock::time_point server_start_time;
std::atomic<bool> timer_started(false);

struct Metrics
{
    double timestamp;
    int64_t message_count;
    double throughput; 
    double gbit_per_second;
};

struct ThreadResult
{
    int64_t total_message_count;
    int64_t total_bytes_sent;
    int64_t total_bytes_received;
    double duration;
    std::vector<std::vector<Metrics>> per_second_metrics; 
};

void accept_connections(const int listen_fd)
{
    cout << "Acceptor thread started." << endl;
    while (accepting_connections.load())
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept4(listen_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
        if (conn_fd >= 0)
        {
            cout << "Accepted connection: fd=" << conn_fd << endl;

            if (!config.enable_nagle)
            {
                int flag = 1;
                int ret = setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
                if (ret < 0)
                {
                    perror("setsockopt TCP_NODELAY");
                }
            }

            if (config.increase_socket_buffers)
            {
                int buf_size = 4 * 1024 * 1024; // 4MB
                int ret = setsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
                if (ret < 0)
                {
                    perror("setsockopt SO_SNDBUF");
                }
                ret = setsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
                if (ret < 0)
                {
                    perror("setsockopt SO_RCVBUF");
                }
            }

            if (!timer_started.load())
            {
                server_start_time = std::chrono::steady_clock::now();
                timer_started.store(true);
                cout << "Server timer started." << endl;
            }

            {
                int assigned_thread = next_thread % thread_count;
                cout << "Adding fd " << conn_fd << " to thread " << assigned_thread << endl;
                connection_fds[assigned_thread].push_back(conn_fd);
                next_thread++;
            }
        }
        else
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                perror("accept");
                accepting_connections = false;
                break;
            }

            if (timer_started.load())
            {
                auto now = std::chrono::steady_clock::now();
                double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
                if (elapsed_seconds >= config.run_duration_seconds)
                {
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

bool setup_io_uring(struct io_uring& ring)
{
    int ret;
    struct io_uring_params params = {};
    params.flags = IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN;
    ret = io_uring_queue_init_params(config.queue_depth, &ring, &params);
    if (ret)
    {
        std::cerr << "io_uring_queue_init: " << strerror(-ret) << std::endl;
        return false;
    }
    return true;
}

bool setup_buffers(struct io_uring& ring, char*& recv_buffers, char*& send_buffers)
{
    int ret;

    if (config.use_aligned_allocations)
    {
        if (posix_memalign((void**)&recv_buffers, 4096, config.inflight_ops * 4) != 0 ||
            posix_memalign((void**)&send_buffers, 4096, config.inflight_ops * config.page_size) != 0)
        {
            perror("posix_memalign");
            io_uring_queue_exit(&ring);
            return false;
        }
    }
    else
    {
        recv_buffers = new char[config.inflight_ops * 4];
        send_buffers = new char[config.inflight_ops * config.page_size];
    }

    if (config.alloc_pin)
    {
        ret = mlock(recv_buffers, config.inflight_ops * 4);
        if (ret)
        {
            perror("mlock recv_buffers");
            exit(-1);
        }
        ret = mlock(send_buffers, config.inflight_ops * config.page_size);
        if (ret)
        {
            perror("mlock send_buffers");
            exit(-1);
        }
    }

    struct iovec* iovecs = new struct iovec[config.inflight_ops * 2];
    for (int i = 0; i < config.inflight_ops; ++i)
    {
        iovecs[i].iov_base = recv_buffers + i * 4;
        iovecs[i].iov_len = 4;

        iovecs[config.inflight_ops + i].iov_base = send_buffers + i * config.page_size;
        iovecs[config.inflight_ops + i].iov_len = config.page_size;
    }

    ret = io_uring_register_buffers(&ring, iovecs, config.inflight_ops * 2);
    delete[] iovecs;

    if (ret < 0)
    {
        std::cerr << "io_uring_register_buffers: " << strerror(-ret) << std::endl;
        io_uring_queue_exit(&ring);
        if (config.use_aligned_allocations)
        {
            munlock(recv_buffers, config.inflight_ops * 4);
            munlock(send_buffers, config.inflight_ops * config.page_size);
            free(recv_buffers);
            free(send_buffers);
        }
        else
        {
            delete[] recv_buffers;
            delete[] send_buffers;
        }
        exit(-1);
    }

    return true;
}

void cleanup_buffers(struct io_uring& ring, char* recv_buffers, char* send_buffers)
{
    io_uring_queue_exit(&ring);

    if (config.alloc_pin)
    {
        munlock(recv_buffers, config.inflight_ops * 4);
        munlock(send_buffers, config.inflight_ops * config.page_size);
    }

    if (config.use_aligned_allocations)
    {
        free(recv_buffers);
        free(send_buffers);
    }
    else
    {
        delete[] recv_buffers;
        delete[] send_buffers;
    }
}

void handle_connection(const int thread_id, ThreadResult& result, struct io_uring& ring,
                       char* recv_buffers, char* send_buffers,
                       std::unordered_map<int, int>& fd_to_conn_index)
{
    int ret;
    int inflight = 0;
    int sqes_to_submit = 0;

    int num_connections = connection_fds[thread_id].size();

    std::vector<int64_t> message_count(num_connections, 0);
    std::vector<int64_t> total_bytes_sent(num_connections, 0);
    std::vector<int64_t> total_bytes_received(num_connections, 0);
    std::vector<int64_t> bytes_sent_since_last_report(num_connections, 0);
    std::vector<int64_t> bytes_received_since_last_report(num_connections, 0);
    std::vector<int64_t> messages_since_last_report(num_connections, 0);

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    bool connection_active = true;

    if (config.half_duplex_mode)
    {
        memset(send_buffers, 0, config.page_size * config.inflight_ops);
    }
    else
    {
        for (int i = 0; i < config.inflight_ops; ++i)
        {
            memset(send_buffers + i * config.page_size, 0, config.page_size);
        }
    }

    for (int i = 0; i < config.inflight_ops; ++i)
    {
        auto cfds_len = connection_fds[thread_id].size();
        auto conn_fd = connection_fds[thread_id][i % cfds_len];

        fd_to_conn_index[conn_fd] = i % cfds_len;

        if (config.half_duplex_mode)
        {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                std::cerr << "io_uring_get_sqe failed" << std::endl;
                connection_active = false;
                break;
            }
            // io_uring_prep_send(sqe, conn_fd, send_buffers + i * config.page_size, config.page_size, 0);
            io_uring_prep_send_zc(sqe, conn_fd, send_buffers + i * config.page_size, config.page_size, 0, 0);
            UserData data;
            data.buffer_idx = i;
            data.is_send = true;
            data.fd = conn_fd;
            sqe->user_data = pack_user_data(data);
            ++sqes_to_submit;
            ++inflight;
        }
        else
        {
            // Submit initial receive requests
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            if (!sqe)
            {
                std::cerr << "io_uring_get_sqe failed" << std::endl;
                connection_active = false;
                break;
            }
            io_uring_prep_recv(sqe, conn_fd, recv_buffers + i * 4, 4, 0);
            UserData data;
            data.buffer_idx = i;
            data.is_send = false;
            data.fd = conn_fd;
            sqe->user_data = pack_user_data(data);
            ++sqes_to_submit;
            ++inflight;
        }
    }

    if (sqes_to_submit > 0)
    {
        ret = io_uring_submit(&ring);
        if (ret < 0)
        {
            std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
            connection_active = false;
        }
        sqes_to_submit = 0;
    }

    struct __kernel_timespec timeout;
    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;

    while (connection_active)
    {
        // Check time limit
        if (timer_started.load())
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
            if (elapsed_seconds >= config.run_duration_seconds)
            {
                cout << "Time limit reached. Worker thread " << thread_id << " closing connection." << endl;
                connection_active = false;
                break;
            }
        }

        struct io_uring_cqe* cqe;
        ret = io_uring_wait_cqe_timeout(&ring, &cqe, &timeout);
        if (ret == -ETIME || ret == -EINTR)
        {
            continue;
        }
        else if (ret < 0)
        {
            std::cerr << "io_uring_wait_cqe_timeout: " << strerror(-ret) << std::endl;
            connection_active = false;
            break;
        }

        UserData data = unpack_user_data(cqe->user_data);
        uint32_t buffer_idx = data.buffer_idx;
        bool is_send = data.is_send;
        uint16_t conn_fd = data.fd;

        int conn_index = fd_to_conn_index[conn_fd];

        if (cqe->res < 0)
        {
            if (cqe->res == -EAGAIN)
            {
                struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                if (!sqe)
                {
                    std::cerr << "io_uring_get_sqe failed" << std::endl;
                    connection_active = false;
                    break;
                }
                if (is_send)
                {
                    // io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_idx * config.page_size, config.page_size, 0);
                    io_uring_prep_send_zc(sqe, conn_fd, send_buffers + buffer_idx * config.page_size, config.page_size, 0, 0);
                    UserData new_data = data; // Same data
                    sqe->user_data = pack_user_data(new_data);
                }
                else
                {
                    io_uring_prep_recv(sqe, conn_fd, recv_buffers + buffer_idx * 4, 4, 0);
                    UserData new_data = data; // Same data
                    sqe->user_data = pack_user_data(new_data);
                }
                ++sqes_to_submit;
            }
            else if (cqe->res == -ECONNRESET || cqe->res == -EPIPE)
            {
                if (config.verbose) cout << "Connection closed by client on fd " << conn_fd << endl;
                connection_active = false;
                break;
            }
            else
            {
                std::cerr << "Operation error on fd " << conn_fd << ": " << strerror(-cqe->res) << std::endl;
                connection_active = false;
                break;
            }
        }
        else if (cqe->res == 0)
        {
            cout << "Connection closed by client on fd " << conn_fd << endl;
            connection_active = false;
            break;
        }
        else
        {
            if (is_send)
            {
                int bytes_written = cqe->res;
                if (config.verbose) cout << "Sent " << bytes_written << " bytes to fd " << conn_fd << endl;

                total_bytes_sent[conn_index] += bytes_written;
                bytes_sent_since_last_report[conn_index] += bytes_written;

                if (!config.half_duplex_mode)
                {
                    --inflight;
                }

                if (config.half_duplex_mode)
                {
                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                    if (!sqe)
                    {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        connection_active = false;
                        break;
                    }
                    io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_idx * config.page_size, config.page_size, 0);
                    UserData new_data = data; // Same data
                    sqe->user_data = pack_user_data(new_data);
                    ++sqes_to_submit;
                }

                ++message_count[conn_index];

                auto now = std::chrono::steady_clock::now();
                double time_since_last_report = std::chrono::duration<double>(now - last_report_time).count();
                if (time_since_last_report >= 1.0)
                {
                    double segment_duration = time_since_last_report;

                    for (int i = 0; i < num_connections; ++i)
                    {
                        double conn_throughput = (message_count[i] - messages_since_last_report[i]) / segment_duration;

                        double data_transferred_bits =
                            (bytes_sent_since_last_report[i] + bytes_received_since_last_report[i]) * 8;
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
            else
            {
                int bytes_received = cqe->res;
                if (config.verbose) cout << "Received " << bytes_received << " bytes from fd " << conn_fd << endl;

                total_bytes_received[conn_index] += bytes_received;
                bytes_received_since_last_report[conn_index] += bytes_received;

                if (config.half_duplex_mode)
                {
                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                    if (!sqe)
                    {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        connection_active = false;
                        break;
                    }
                    io_uring_prep_recv(sqe, conn_fd, recv_buffers + buffer_idx * 4, 4, 0);
                    UserData new_data = data; 
                    sqe->user_data = pack_user_data(new_data);
                    ++sqes_to_submit;
                }
                else
                {
                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
                    if (!sqe)
                    {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        connection_active = false;
                        break;
                    }
                    io_uring_prep_send(sqe, conn_fd, send_buffers + buffer_idx * config.page_size, config.page_size, 0);
                    UserData send_data;
                    send_data.buffer_idx = buffer_idx;
                    send_data.is_send = true;
                    send_data.fd = conn_fd;
                    sqe->user_data = pack_user_data(send_data);
                    ++sqes_to_submit;
                    ++inflight;

                    struct io_uring_sqe* recv_sqe = io_uring_get_sqe(&ring);
                    if (!recv_sqe)
                    {
                        std::cerr << "io_uring_get_sqe failed" << std::endl;
                        connection_active = false;
                        break;
                    }
                    io_uring_prep_recv(recv_sqe, conn_fd, recv_buffers + buffer_idx * 4, 4, 0);
                    UserData recv_data = data;
                    recv_sqe->user_data = pack_user_data(recv_data);
                    ++sqes_to_submit;
                    ++inflight;
                }
            }
        }

        io_uring_cqe_seen(&ring, cqe);

        if (sqes_to_submit > 0)
        {
            ret = io_uring_submit(&ring);
            if (ret < 0)
            {
                std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
                connection_active = false;
                break;
            }
            sqes_to_submit = 0;
        }

        if (!connection_active || (inflight == 0 && !config.half_duplex_mode))
        {
            break;
        }
    }

    result.total_message_count = 0;
    result.total_bytes_sent = 0;
    result.total_bytes_received = 0;
    for (int i = 0; i < num_connections; ++i)
    {
        result.total_message_count += message_count[i];
        result.total_bytes_sent += total_bytes_sent[i];
        result.total_bytes_received += total_bytes_received[i];
    }
}

void worker_thread(const int thread_id, ThreadResult& result)
{
    cout << "Worker thread " << thread_id << " started in " << (config.half_duplex_mode ? "half-duplex" : "full-duplex")
        << " mode." << endl;

    if (!set_thread_affinity(thread_id))
    {
        std::cerr << "set_thread_affinity failed: " << thread_id << std::endl;
    }

    int ret;
    struct io_uring ring{};

    if (!setup_io_uring(ring))
    {
        return;
    }

    char* recv_buffers;
    char* send_buffers;

    if (!setup_buffers(ring, recv_buffers, send_buffers))
    {
        return;
    }

    std::unordered_map<int, int> fd_to_conn_index;

    result.per_second_metrics.resize(config.connections_per_thread);

    auto start_time = std::chrono::steady_clock::now();

    while (accepting_connections.load())
    {
        if (connection_fds[thread_id].size() < config.connections_per_thread)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        cout << "Worker thread " << thread_id << " handling connections" << endl;

        handle_connection(thread_id, result, ring, recv_buffers, send_buffers, fd_to_conn_index);

        if (timer_started.load())
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed_seconds = std::chrono::duration<double>(now - server_start_time).count();
            if (elapsed_seconds >= config.run_duration_seconds)
            {
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

    cleanup_buffers(ring, recv_buffers, send_buffers);

    cout << "Worker thread " << thread_id << " exiting." << endl;
}

int main()
{
    config.load_from_env();

    thread_count = config.thread_count;
    for (int i = 0; i < thread_count; ++i)
    {
        connection_fds.push_back(std::vector<uint16_t>());
    }

    cout << "Server starting..." << endl;
    int ret;

    connection_fds.resize(thread_count);

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0)
    {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    ret = listen(listen_fd, SOMAXCONN);
    if (ret < 0)
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    cout << "Server listening on port " << config.port << "." << endl;

    std::thread acceptor(accept_connections, listen_fd);

    std::vector<std::thread> workers;
    std::vector<ThreadResult> thread_results(config.thread_count);
    for (int i = 0; i < config.thread_count; ++i)
    {
        thread_results[i].per_second_metrics.resize(config.connections_per_thread);
    }

    for (int i = 0; i < config.thread_count; ++i)
    {
        workers.emplace_back(worker_thread, i, std::ref(thread_results[i]));
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    int64_t total_messages_processed = 0;
    int64_t total_bytes_sent = 0;
    int64_t total_bytes_received = 0;
    double total_duration = 0;

    for (const auto& result : thread_results)
    {
        total_messages_processed += result.total_message_count;
        total_bytes_sent += result.total_bytes_sent;
        total_bytes_received += result.total_bytes_received;
        if (result.duration > total_duration)
        {
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
    for (int thread_id = 0; thread_id < thread_results.size(); ++thread_id)
    {
        const auto& per_second_metrics = thread_results[thread_id].per_second_metrics;
        for (int conn_index = 0; conn_index < per_second_metrics.size(); ++conn_index)
        {
            const auto& conn_metrics = per_second_metrics[conn_index];
            for (const auto& m : conn_metrics)
            {
                metrics_file << m.timestamp << "," << thread_id << "," << conn_index << "," << m.message_count << ","
                    << m.throughput << "," << m.gbit_per_second << "\n";
            }
        }
    }
    metrics_file.close();

    std::string config_filename = "report_server_" + datetime_str + "_env";
    config.save_to_file(config_filename);

    close(listen_fd);

    if (acceptor.joinable())
    {
        acceptor.join();
    }

    cout << "Server shutting down." << endl;
    return 0;
}
