#include <seastar/core/seastar.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include "simple_consts.hpp"

seastar::future<> handle_connection(seastar::connected_socket s) {
  auto out = s.output();
  auto in = s.input();
  return seastar::do_with(std::move(s),
                          std::move(out),
                          std::move(in),
                          [](auto &s, auto &out, auto &in) {
                            return seastar::repeat([&out, &in]() {
                              return in.read_exactly(sizeof(uint32_t)).then([&out](
                                  seastar::temporary_buffer<char> buf) {
                                if (buf.empty()) {
                                  return seastar::make_ready_future<seastar::stop_iteration>(
                                      seastar::stop_iteration::yes);
                                }
                                uint32_t number =
                                    *reinterpret_cast<const uint32_t *>(buf.get());
#if VERBOSE
                                printf("Received request for page %u\n",
                                       number);
#endif
                                std::vector<uint32_t> response(PAGE_SIZE);
                                for (uint32_t i = 0; i < PAGE_SIZE; ++i) {
                                  response[i] = number;
                                }
                                return out.write(reinterpret_cast<const char *>(response.data()),
                                                 PAGE_SIZE
                                                     * sizeof(uint32_t)).then([&out] { return out.flush(); }).then(
                                    [] {
                                      return seastar::make_ready_future<seastar::stop_iteration>(
                                          seastar::stop_iteration::no);
                                    });
                              });
                            });
                          });
}

seastar::future<> service_loop() {
  seastar::listen_options lo;
  lo.reuse_address = true;
  return seastar::do_with(seastar::listen(seastar::make_ipv4_address({PORT}),
                                          lo), [](auto &listener) {
    return seastar::keep_doing([&listener]() {
      return listener.accept().then([](seastar::accept_result res) {
        (void) handle_connection(std::move(res.connection)).handle_exception([](
            std::exception_ptr ep) {
          fmt::print(stderr, "Could not handle connection: {}\n", ep);
        });
      });
    });
  });
}

int main(int argc, char **argv) {
  seastar::app_template app;
  app.run(argc, argv, service_loop);
  return 0;
}
