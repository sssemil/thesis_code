#include "simple_consts.hpp"
#include <vector>

seastar::future<> start_client(uint32_t start, uint32_t end) {
  seastar::socket_address server_addr(seastar::ipv4_addr(SERVER_ADDR, PORT));
  return seastar::connect(server_addr).then([start, end](seastar::connected_socket s) {
    auto out = s.output();
    auto in = s.input();
    return seastar::do_with(std::move(s),
                            std::move(out),
                            std::move(in),
                            [start, end](auto &s, auto &out, auto &in) {
                              std::vector<seastar::future<>> futures;
                              for (uint32_t request = start; request < end;
                                   ++request) {
                                futures.push_back(out.write(reinterpret_cast<const char *>(&request),
                                                            sizeof(uint32_t)).then(
                                    [&out] {
                                      return out.flush();
                                    }));
                                futures.push_back(in.read_exactly(
                                    PAGE_SIZE * sizeof(uint32_t)).then([](
                                    seastar::temporary_buffer<char> buf) {
#if VERBOSE
                                  debug_print_array((uint8_t *) buf.get(),
                                                    buf.size());
#endif
                                }));
                                futures.push_back(in.read_exactly(
                                    PAGE_SIZE * sizeof(uint32_t)).then([](
                                    seastar::temporary_buffer<char> buf) {
#if VERBOSE
                                  debug_print_array((uint8_t *) buf.get(),
                                                    buf.size());
#endif
                                }));
                              }
                              return seastar::when_all_succeed(futures.begin(),
                                                               futures.end());
                            });
  });
}

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, [&] {
    return start_client(0, 100);
  });
}
