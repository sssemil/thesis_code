#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "consts.hpp"
#include "memory_block.hpp"
#include "models/get_page.hpp"
#include "spdlog/spdlog.h"
#include "static_config.hpp"
#include "utils.hpp"
#include "io_uring_utils.hpp"

#define BATCH_SIZE 64

struct custom_request {
  int event_type;
};

enum EventType { SEND, RECEIVE };

int setup_socket(const char* addr, int port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    spdlog::error("Socket creation failed");
    return -1;
  }

  fcntl(sock, F_SETFL, O_NONBLOCK);

  struct sockaddr_in serv_addr {};
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  inet_pton(AF_INET, addr, &serv_addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
    if (errno != EINPROGRESS) {
      spdlog::error("Connection failed");
      close(sock);
      return -1;
    }
  }

  return sock;
}

void send_requests(struct io_uring& ring, int sock,
                   std::vector<GetPageRequest>& requests, size_t start,
                   size_t end) {
  for (size_t j = start; j < end; j++) {
    spdlog::debug("Creating request {} {}", start, j);
    uint32_t request_id = j;

    auto* request = &requests[j];
    request->request_id = request_id;
    request->page_number = j % Config::page_count;
    request->to_network_order();

    struct io_uring_sqe* sqe_send = io_uring_get_sqe(&ring);
    auto* req_send = new custom_request{SEND};
    io_uring_sqe_set_data(sqe_send, req_send);
    io_uring_prep_send(sqe_send, sock, request, sizeof(GetPageRequest), 0);
  }

  io_uring_submit(&ring);
}

void receive_responses(struct io_uring& ring, int sock,
                       std::vector<GetPageResponse*>& responses, size_t start,
                       size_t end) {
  size_t num_responses = end - start;

  for (size_t j = start; j < end; j++) {
    struct io_uring_sqe* sqe_recv = io_uring_get_sqe(&ring);
    auto* req_recv = new custom_request{RECEIVE};
    io_uring_sqe_set_data(sqe_recv, req_recv);
    io_uring_prep_recv(sqe_recv, sock, responses[j], sizeof(GetPageResponse),
                       0);
  }

  io_uring_submit(&ring);

  for (size_t j = 0; j < num_responses; j++) {
    struct io_uring_cqe* cqe;
    auto r = io_uring_wait_cqe(&ring, &cqe);
    if (r < 0) {
      spdlog::error("Wait for response failed: {}", strerror(-r));
      throw std::runtime_error("Wait for response failed");
    }
    auto* req = (custom_request*)io_uring_cqe_get_data(cqe);
    if (req->event_type == SEND) {
      spdlog::debug("Sent request {}", j);
      j--;
    }
    if (((req->event_type == SEND && cqe->res == 140)
        || (req->event_type == RECEIVE && cqe->res == 8))) {
      spdlog::debug("CQE->RES {} ({})", cqe->res, req->event_type);
    }
    if (cqe->res < 0) {
      spdlog::error("IO operation failed: {}", strerror(-cqe->res));
      throw std::runtime_error("IO operation failed");
    }
    io_uring_cqe_seen(&ring, cqe);
  }
}

void verify_responses(std::vector<GetPageResponse*>& responses,
                      MemoryBlockVerifier<PAGE_SIZE>& verifier,
                      uint32_t& correct_responses,
                      uint32_t& incorrect_responses) {
  for (auto* response : responses) {
    if (response) {
      response->to_host_order();
      if (!verifier.verify(response->content, response->header.page_number)) {
        spdlog::error("Response req id {}", response->header.request_id);
        spdlog::debug("Response status {}", response->header.status);
        spdlog::debug("Response page num {}", response->header.page_number);
        spdlog::error("Verification failed for page {}",
                      response->header.page_number);
        debug_print_array((uint8_t*)response, sizeof(GetPageResponse));
        incorrect_responses++;
      } else {
        correct_responses++;
      }
    } else {
      spdlog::error("Response is null");
    }
  }
}

void client_thread(const char* addr, int port, size_t start, size_t end,
                   std::vector<GetPageRequest>& requests,
                   std::vector<GetPageResponse*>& responses) {
  struct io_uring ring {};
  setup_io_uring(ring);

  int sock = setup_socket(addr, port);
  if (sock < 0) return;

  for (size_t i = start; i < end; i += BATCH_SIZE) {
    size_t batch_end = std::min(end, i + BATCH_SIZE);
    send_requests(ring, sock, requests, i, batch_end);
    receive_responses(ring, sock, responses, i, batch_end);
  }

  close(sock);
  io_uring_queue_exit(&ring);
}

int main() {
  Config::load_config();
  MemoryBlockVerifier<PAGE_SIZE> verifier(Config::page_count,
                                          new PseudoRandomFillingStrategy());

  srand(time(nullptr));  // NOLINT(*-msc51-cpp)
  auto start_time = std::chrono::high_resolution_clock::now();

  size_t num_requests = Config::num_requests;
  uint32_t correct_responses = 0;
  uint32_t incorrect_responses = 0;

  std::vector<GetPageRequest> requests(num_requests);
  std::vector<GetPageResponse*> responses(num_requests);
  for (size_t i = 0; i < num_requests; i++) {
    responses[i] = new GetPageResponse();
  }

  std::vector<std::thread> threads;
  size_t requests_per_thread = num_requests / Config::client_threads;
  for (size_t i = 0; i < Config::client_threads; i++) {
    size_t start = i * requests_per_thread;
    size_t end = (i == Config::client_threads - 1)
                     ? num_requests
                     : (i + 1) * requests_per_thread;
    spdlog::info("Starting thread {} for range {} {}", i, start, end);
    threads.emplace_back(client_thread, Config::host.c_str(), Config::port,
                         start, end, std::ref(requests), std::ref(responses));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  double total_time =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::high_resolution_clock::now() - start_time)
          .count() /
      1000000000.0;
  const double avg_rate =
      static_cast<double>(Config::num_requests) / total_time;
  double avg_time = total_time / static_cast<double>(Config::num_requests);
  const double avg_gbps =
      avg_rate * sizeof(GetPageResponse) * 8 / (1000 * 1000 * 1000);

  verify_responses(responses, verifier, correct_responses, incorrect_responses);

  spdlog::info("======================================");
  spdlog::info("Correct responses: {}", correct_responses);
  spdlog::info("Incorrect responses: {}", incorrect_responses);
  spdlog::info("Total time for {} requests: {:.2f} s", Config::num_requests,
               total_time);
  spdlog::info("Average time per request: {:.2f} s", avg_time);
  spdlog::info("Average rate: {:03.2f} req/s", avg_rate);
  spdlog::info("Average throughput: {:03.2f} Gb/s", avg_gbps);
  spdlog::info("======================================");

  return 0;
}
