#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic<X>
#endif

#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "buffer_pool.hpp"
#include "simple_consts.hpp"

RequestData* req;

void add_read_request(struct io_uring& ring, int client_socket,
                      RequestData* req) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
  req->event_type = READ_EVENT;
  req->buffer_offset = 0;

  io_uring_prep_read(sqe, client_socket, req->buffer,
                     PAGE_SIZE * sizeof(int32_t), 0);
  io_uring_sqe_set_data(sqe, req);
}

uint64_t event_loop(struct io_uring& ring, int client_socket,
                    std::atomic<uint64_t>& total_bytes_received) {
  std::vector buffer_sizes = {sizeof(RequestData) +
                              PAGE_SIZE * sizeof(int32_t)};
  // BufferPool buffer_pool(&ring, buffer_sizes, BUFFER_POOL_INITIAL_POOL_SIZE);

  for (int i = 0; i < RING_SIZE; i++) {
    // auto* req = (RequestData*)malloc(sizeof(RequestData) +
    // PAGE_SIZE * sizeof(int32_t));
    add_read_request(ring, client_socket, req);
  }
  io_uring_submit(&ring);

  while (true) {
    struct io_uring_cqe* cqe;
    io_uring_wait_cqe(&ring, &cqe);
    auto* req = static_cast<RequestData*>(io_uring_cqe_get_data(cqe));

    if (cqe->res <= 0) {
      if (cqe->res == 0) {
        std::cout << "Client closed connection" << std::endl;
      } else {
        std::cout << "Read error: " << strerror(-cqe->res) << std::endl;
      }
      close(client_socket);
      io_uring_cqe_seen(&ring, cqe);
      return total_bytes_received;
    }

    total_bytes_received += cqe->res;

    add_read_request(ring, client_socket, req);
    io_uring_cqe_seen(&ring, cqe);
    io_uring_submit(&ring);
  }
}

void handle_client(const int client_socket,
                   std::atomic<uint64_t>& total_bytes_received,
                   std::atomic<int>& finished_threads) {
  std::cout << "Handling a new client" << std::endl;
  struct io_uring ring {};
  const int r = io_uring_queue_init(RING_SIZE, &ring, 0);
  if (r < 0) {
    std::cout << "io_uring_queue_init failed: " << strerror(-r) << std::endl;
    exit(EXIT_FAILURE);
  }

  event_loop(ring, client_socket, total_bytes_received);

  io_uring_queue_exit(&ring);
  finished_threads++;
}

void configure_socket_to_not_fragment(int socket) {
  // int val = IP_PMTUDISC_DO;
  // setsockopt(socket, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val));
  // int one = 1;
  // setsockopt(socket, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
}

int main() {
  req = (RequestData*)malloc(sizeof(RequestData) + PAGE_SIZE * sizeof(int32_t));
  if constexpr (ALLOCATE_PIN) {
    if (const int res =
            mlock(req, sizeof(RequestData) + PAGE_SIZE * sizeof(int32_t));
        res != 0) {
      throw std::runtime_error("Failed to pin memory");
    }
  }
  int server_fd;
  sockaddr_in address{};
  int addr_len = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    std::cout << "socket failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) <
      0) {
    std::cout << "bind failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 1024) < 0) {
    std::cout << "listen" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Server started. Listening on port " << PORT << std::endl;

  std::atomic<uint64_t> total_bytes_received = 0;
  std::atomic<int> finished_threads = 0;
  std::vector<std::thread> client_threads;

  bool started = false;
  auto start_time = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < CLIENT_THREADS; ++i) {
    int new_socket;
    if ((new_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&address),
                             reinterpret_cast<socklen_t*>(&addr_len))) < 0) {
      std::cout << "accept" << std::endl;
      exit(EXIT_FAILURE);
    }
    configure_socket_to_not_fragment(new_socket);
    if (!started) {
      start_time = std::chrono::high_resolution_clock::now();
      started = true;
      printf("Started at %ld\n", start_time.time_since_epoch().count());
    }

    client_threads.emplace_back(handle_client, new_socket,
                                std::ref(total_bytes_received),
                                std::ref(finished_threads));
  }

  const uint64_t expected_total_bytes =
      static_cast<int64_t>(NUM_REQUESTS) * PAGE_SIZE * sizeof(int32_t);
  double percentage_received;
  while (total_bytes_received < expected_total_bytes &&
         (percentage_received = (total_bytes_received.load() * 100.0) /
                                expected_total_bytes) < 95) {
    printf("Total bytes received: %lu / %lu (%f)\n",
           total_bytes_received.load(), expected_total_bytes,
           percentage_received);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time);

  const double seconds = duration.count() / 1e6;
  const double gbps = (total_bytes_received * 8.0) / (seconds * 1e9);

  std::cout << "All data received. Total bytes: " << total_bytes_received
            << std::endl;
  std::cout << "Time taken: " << seconds << " seconds" << std::endl;
  std::cout << "Throughput: " << gbps << " Gbps" << std::endl;

  for (auto& thread : client_threads) {
    thread.join();
  }

  close(server_fd);
  std::cout << "Server shutting down" << std::endl;
}
