#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef PORT
#define PORT 12345
#endif

#ifndef NUM_REQUESTS
#define NUM_REQUESTS (100)
#endif

#ifndef SERVER_ADDR
#define SERVER_ADDR "127.0.0.1"
#endif

#ifndef VERIFY
#define VERIFY 0
#endif

#ifndef VERBOSE
#define VERBOSE 0
#endif

#ifndef CLIENT_THREADS
#define CLIENT_THREADS 1
#endif

#include <iostream>
#include <cctype>
#include <sstream>

#include <seastar/core/when_all.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <iostream>
#include <vector>

void debug_print_array(uint8_t *arr, uint32_t size) {
  std::ostringstream debug_data_first;
  std::ostringstream debug_data_last;

  auto *iov_base_data = static_cast<uint8_t *>(arr);

  int border = 24;
  for (int j = 0; j < border && j < size; ++j) {
    debug_data_first << std::uppercase << std::setw(2) << std::setfill('0')
                     << std::hex << static_cast<int>(iov_base_data[j]) << " ";
  }

  if (size > border) {
    for (int j = size - border; j < size; ++j) {
      debug_data_last << std::uppercase << std::setw(2) << std::setfill('0')
                      << std::hex << static_cast<int>(iov_base_data[j]) << " ";
    }
  }

  std::cout << "[" << static_cast<void *>(arr) << "][" << size << " B]["
            << size / sizeof(int32_t) << " ints] "
            << "First 24 and last 24 bytes: " << debug_data_first.str()
            << " ... " << debug_data_last.str() << std::endl;
}
