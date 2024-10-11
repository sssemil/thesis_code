#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include <netinet/tcp.h>

#include "buffer_pool.hpp"
#include "simple_consts.hpp"


void configure_socket_to_not_fragment(int socket) {
  // int val = IP_PMTUDISC_DO;
  // setsockopt(socket, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val));
  // int one = 1;
  // setsockopt(socket, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
}

int setup_socket() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    std::cout << "Socket creation failed" << std::endl;
    return -1;
  }

  struct sockaddr_in serv_addr {};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, SERVER_ADDR, &serv_addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
    std::cout << "Connection failed" << std::endl;
    close(sock);
    return -1;
  }

  configure_socket_to_not_fragment(sock);

  return sock;
}

void send_data(size_t start_index, size_t end_index, size_t thread_index) {
  size_t local_num_requests = end_index - start_index;
  printf("[%lu] Sending %lu requests\n", thread_index, local_num_requests);
  int sock = setup_socket();
  if (sock < 0) {
    std::cout << "[" << thread_index << "] Failed to connect to server"
              << std::endl;
    return;
  }

  struct io_uring ring {};
  int r = io_uring_queue_init(RING_SIZE, &ring, 0);
  if (r < 0) {
    std::cout << "[" << thread_index
              << "] io_uring_queue_init failed: " << strerror(-r) << std::endl;
    close(sock);
    return;
  }

  std::vector<int32_t> buffer(PAGE_SIZE);
  if constexpr (ALLOCATE_PIN) {
    if (const int res =
            mlock(buffer.data(), PAGE_SIZE * sizeof(int32_t));
        res != 0) {
      throw std::runtime_error("Failed to pin memory");
    }
  }
  size_t requests_sent = 0;
  size_t requests_completed = 0;

  auto start_time = std::chrono::high_resolution_clock::now();

  while (requests_completed < local_num_requests) {
    while (requests_sent < local_num_requests &&
           requests_sent - requests_completed < RING_SIZE) {
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      if (!sqe) break;

      // std::fill(buffer.begin(), buffer.end(), start_index + requests_sent);
      io_uring_prep_send(sqe, sock, buffer.data(),
                         buffer.size() * sizeof(int32_t), 0);
      io_uring_sqe_set_data(sqe, (void*)(start_index + requests_sent));

      requests_sent++;
    }

    io_uring_submit(&ring);

    struct io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring, head, cqe) {
      if (cqe->res < 0) {
        std::cout << "[" << thread_index
                  << "] Send failed: " << strerror(-cqe->res) << std::endl;
      } else {
        requests_completed++;
      }
      count++;
    }

    io_uring_cq_advance(&ring, count);

    if (requests_completed % 10000 == 0) {
      auto current_time = std::chrono::high_resolution_clock::now();
      auto elapsed =
          std::chrono::duration<double>(current_time - start_time).count();
      double rate = requests_completed / elapsed;
      std::cout << "[" << thread_index << "] Completed: " << requests_completed
                << " [" << rate << " req/s]" << std::endl;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration<double>(end_time - start_time);
  double gbps = (requests_completed * PAGE_SIZE * sizeof(int32_t) * 8) /
                (duration.count() * 1e9);

  std::cout << "[" << thread_index << "] Completed " << requests_completed
            << " requests in " << duration.count() << " seconds" << std::endl;
  std::cout << "[" << thread_index << "] Throughput: " << gbps << " Gbps"
            << std::endl;

  io_uring_queue_exit(&ring);
  close(sock);
}

int main() {
  std::vector<std::thread> threads;
  size_t requests_per_thread = NUM_REQUESTS / CLIENT_THREADS;

  auto start_time = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < CLIENT_THREADS; i++) {
    size_t start_index = i * requests_per_thread;
    size_t end_index = (i == CLIENT_THREADS - 1)
                           ? NUM_REQUESTS
                           : (i + 1) * requests_per_thread;
    threads.emplace_back(send_data, start_index, end_index, i);
  }

  for (auto& thread : threads) {
    thread.join();
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration<double>(end_time - start_time);
  double total_gbps =
      (static_cast<double>(NUM_REQUESTS) * PAGE_SIZE * sizeof(int32_t) * 8) /
      (duration.count() * 1e9);
  double total_rate = NUM_REQUESTS / duration.count();

  std::cout << "All threads completed in " << duration.count() << " seconds"
            << std::endl;
  std::cout << "Average rate: " << std::fixed << std::setprecision(2)
            << total_rate << " it/s" << std::endl;
  std::cout << "Average Gbps: " << total_gbps << " Gbps" << std::endl;

  return 0;
}
