#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "memory_block.hpp"
#include "models/get_page.hpp"
#include "spdlog/spdlog.h"
#include "static_config.hpp"
#include "utils.hpp"
#include "io_uring_utils.hpp"

struct custom_request {
  int event_type;
  struct iovec iov;
};

enum EventType { READ, WRITE };

void add_read_request(struct io_uring& ring, int client_socket) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
  auto* req = new custom_request{READ};
  req->iov.iov_base = malloc(sizeof(GetPageRequest));
  req->iov.iov_len = sizeof(GetPageRequest);

  io_uring_prep_readv(sqe, client_socket, &req->iov, 1, 0);
  io_uring_sqe_set_data(sqe, req);
}

void add_write_request(struct io_uring& ring, int client_socket,
                       const iovec* iovs, int iov_count) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
  auto* req = new custom_request{WRITE};
  req->iov.iov_base = nullptr;
  req->iov.iov_len = 0;

  io_uring_prep_writev(sqe, client_socket, iovs, iov_count, 0);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);
}

void event_loop(struct io_uring& ring, int client_socket,
                const MemoryBlock<PAGE_SIZE>& memory_block) {
  configure_socket_to_not_fragment(client_socket);
  for (int i = 0; i < 64; i++) {
    add_read_request(ring, client_socket);
  }
  io_uring_submit(&ring);

  while (true) {
    struct io_uring_cqe* cqe;
    io_uring_wait_cqe(&ring, &cqe);
    auto* req = (custom_request*)io_uring_cqe_get_data(cqe);

    switch (req->event_type) {
      case READ: {
        if (cqe->res == 0) {
          spdlog::info("Client closed connection");
          close(client_socket);
          return;
        }
        spdlog::debug("Read data from client");

        auto* request = reinterpret_cast<GetPageRequest*>(req->iov.iov_base);
        request->to_host_order();
        spdlog::debug("Requested page number: {}", request->page_number);

        GetPageResponse response{};
        response.header.request_id = request->request_id;
        response.header.page_number = request->page_number;
        if (request->page_number >= Config::page_count) {
          spdlog::error("Invalid page number: {0:#x}", request->page_number);
          response.header.status = INVALID_PAGE_NUMBER;
          response.header.to_network_order();
          memset(response.content.data(), 0xFA, response.content.size());
          iovec iov[1];
          iov[0].iov_base = &response;
          iov[0].iov_len = sizeof(response);
          add_write_request(ring, client_socket, iov, 1);
        } else {
          constexpr GetPageStatus status = SUCCESS;
          response.header.status = status;
          response.header.to_network_order();

          iovec iov[2];
          iov[0].iov_base = &response.header;
          iov[0].iov_len = sizeof(response.header);
          iov[1].iov_base = const_cast<uint8_t*>(memory_block.data.data()) +
                            request->page_number * PAGE_SIZE;
          iov[1].iov_len = PAGE_SIZE;

          add_write_request(ring, client_socket, iov, 2);

          debug_print_array(static_cast<uint8_t*>(iov[1].iov_base),
                            iov[1].iov_len);
        }
        free(req->iov.iov_base);
        add_read_request(ring, client_socket);
        io_uring_submit(&ring);
        break;
      }
      case WRITE:
        spdlog::debug("Write complete, keeping connection open");
        free(req->iov.iov_base);
        break;
    }

    io_uring_cqe_seen(&ring, cqe);
    delete req;
  }
}

void handle_client(const int client_socket,
                   const MemoryBlock<PAGE_SIZE>& memory_block) {
  spdlog::info("Handling a new client");
  struct io_uring ring {};
  setup_io_uring(ring);
  event_loop(ring, client_socket, memory_block);
  io_uring_queue_exit(&ring);
}

int main() {
  Config::load_config();

  MemoryBlock<PAGE_SIZE> memory_block(Config::page_count,
                                      new PseudoRandomFillingStrategy());

  spdlog::info("Port: {}", Config::port);

  int server_fd;
  sockaddr_in address{};
  int addr_len = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    spdlog::critical("socket failed");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(Config::port);

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) <
      0) {
    spdlog::critical("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, MAX_QUEUE) < 0) {
    spdlog::critical("listen");
    exit(EXIT_FAILURE);
  }

  spdlog::info("Server started. Listening on port {}", Config::port);

  while (true) {
    int new_socket;
    if ((new_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&address),
                             reinterpret_cast<socklen_t*>(&addr_len))) < 0) {
      spdlog::critical("accept");
      exit(EXIT_FAILURE);
    }

    std::thread client_thread(handle_client, new_socket,
                              std::ref(memory_block));
    client_thread.detach();
  }
}
