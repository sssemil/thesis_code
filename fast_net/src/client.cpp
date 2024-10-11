#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "consts.hpp"
#include "memory_block.hpp"
#include "models/get_page.hpp"
#include "spdlog/spdlog.h"
#include "static_config.hpp"
#include "utils.hpp"

int main() {
  Config::load_config();

  const MemoryBlockVerifier<PAGE_SIZE> verifier(
      Config::page_count, new PseudoRandomFillingStrategy());

  spdlog::info("Port: {}", Config::port);
  spdlog::info("Number of Requests: {}", Config::num_requests);

  sockaddr_in serv_addr{};
  int sock;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    spdlog::error("Socket creation error");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(Config::port);

  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    spdlog::error("Invalid address / Address not supported");
    return -1;
  }

  if (connect(sock, reinterpret_cast<sockaddr*>(&serv_addr),
              sizeof(serv_addr)) < 0) {
    spdlog::error("Connection failed");
    return -1;
  }

  srand(time(nullptr));  // NOLINT(*-msc51-cpp)
  std::chrono::duration<double, std::milli> total_time(0);

  uint32_t correct_responses = 0;
  uint32_t incorrect_responses = 0;
  GetPageRequest request{};
  GetPageResponse response{};
  for (int i = 0; i < Config::num_requests; ++i) {
    auto start = std::chrono::high_resolution_clock::now();

    request.request_id = i;
    request.page_number = i % Config::page_count;
    request.to_network_order();
    if (send(sock, &request, sizeof(request), 0) != sizeof(request)) {
      spdlog::error("Error sending request to server");
      continue;
    }

    size_t total_read = 0;
    auto* response_ptr = reinterpret_cast<uint8_t*>(&response);
    while (total_read < sizeof(response)) {
      ssize_t bytes_read =
          read(sock, response_ptr + total_read, sizeof(response) - total_read);
      if (bytes_read < 0) {
        spdlog::error("Error reading response from server");
        break;
      }
      if (bytes_read == 0) {
        spdlog::error("Connection closed by server");
        break;
      }
      total_read += bytes_read;
    }

    if (total_read != sizeof(response)) {
      spdlog::error("Incomplete response received");
      continue;
    }

    response.to_host_order();

    if (response.get_status() != SUCCESS) {
      spdlog::error("Server reported error {} for page {}",
                    (uint32_t)response.get_status(),
                    response.header.page_number);
      incorrect_responses++;
      continue;
    }

    auto end = std::chrono::high_resolution_clock::now();
    total_time += end - start;

    if (verifier.verify(response.content, response.header.page_number)) {
      spdlog::debug("Verification passed for page {}",
                    response.header.page_number);
      correct_responses++;
    } else {
      spdlog::error("Verification failed for page {}",
                    response.header.page_number);
      incorrect_responses++;
    }

    if (i % 1000 == 999) {
      const double millis = total_time.count();
      const double rate = 1000 * ((i + 1) / millis);
      const double gbps =
          rate * sizeof(GetPageResponse) * 8 / (1000 * 1000 * 1000);
      spdlog::info("Processed {}/{} requests [{:03.2f} req/s][{:03.2f} Gb/s]",
                   i + 1, Config::num_requests, rate, gbps);
    }
  }

  const double total_time_seconds = total_time.count() / 1000;
  const double avg_time =
      total_time_seconds / static_cast<double>(Config::num_requests);
  const double avg_rate =
      static_cast<double>(Config::num_requests) / total_time_seconds;
  const double avg_gbps =
      avg_rate * sizeof(GetPageResponse) * 8 / (1000 * 1000 * 1000);

  spdlog::info("Correct responses: {}", correct_responses);
  spdlog::info("Incorrect responses: {}", incorrect_responses);
  spdlog::info("Average response time for {} requests: {} s",
               Config::num_requests, avg_time);
  spdlog::info("Average rate: {:03.2f} req/s", avg_rate);
  spdlog::info("Average throughput: {:03.2f} Gb/s", avg_gbps);

  close(sock);
  return 0;
}
