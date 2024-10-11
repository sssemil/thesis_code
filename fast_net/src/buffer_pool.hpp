#pragma once

#include <liburing.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "simple_consts.hpp"

enum BufferState { Free, CheckedOut };

struct CheckedOutBuf {
  struct iovec iovec;
  uint16_t index;
};

class BufferPool {
 public:
  explicit BufferPool(struct io_uring* ring,
                      const std::vector<size_t>& buffer_sizes,
                      size_t capacity = BUFFER_POOL_INITIAL_POOL_SIZE)
      : ring(ring), capacity(capacity) {
    if constexpr (STUPID_BUFFER_MODE) {
      constexpr auto stupid_buffer_size = sizeof(RequestData) +
                                                   PAGE_SIZE * sizeof(int32_t);
      stupid_iovec = std::vector<struct iovec>(1);
      stupid_iovec[0] = stupid_buffer.iovec;
      stupid_buffer.iovec.iov_base =
          std::aligned_alloc(4096, stupid_buffer_size);
      stupid_buffer.iovec.iov_len = stupid_buffer_size;

      if constexpr (ALLOCATE_PIN) {
        if (const int res =
                mlock(stupid_buffer.iovec.iov_base, stupid_buffer_size);
            res != 0) {
          free(stupid_buffer.iovec.iov_base);
          throw std::runtime_error("Failed to pin memory");
        }
      }

      if constexpr (ALLOCATE_REGISTERED_BUFFERS) {
        const int register_result =
            io_uring_register_buffers(ring, stupid_iovec.data(), 1);
        if (register_result != 0) {
          throw std::runtime_error(
              "Failed to register buffers with io_uring");
        }
        std::cout << "Successfully registered buffers with io_uring"
                  << std::endl;
      }
    } else {
      size_t total_size = 0;
      for (const size_t size : buffer_sizes) {
        total_size += size * capacity;
      }
      std::cout << "Total allocated size: " << total_size << std::endl;

      std::vector<iovec> all_iovecs;
      for (size_t size : buffer_sizes) {
        pools[size] = new iovec*[capacity];
        states[size] = new BufferState[capacity];
        for (size_t j = 0; j < capacity; ++j) {
          auto* buffer = static_cast<char*>(std::aligned_alloc(4096, size));
          if (!buffer) {
            throw std::bad_alloc();
          }
          if constexpr (ALLOCATE_PIN) {
            if (const int res = mlock(buffer, size); res != 0) {
              std::free(buffer);
              throw std::runtime_error("Failed to pin memory");
            }
          }
          auto* iov = new iovec();
          iov->iov_base = buffer;
          iov->iov_len = size;
          pools[size][j] = iov;
          states[size][j] = BufferState::Free;
          all_iovecs.push_back(*iov);
        }
        next_index[size] = 0;
      }

      if constexpr (ALLOCATE_REGISTERED_BUFFERS) {
        std::cout << "Registering " << all_iovecs.size()
                  << " buffers with io_uring" << std::endl;
        const int register_result =
            io_uring_register_buffers(ring, all_iovecs.data(),
                                      all_iovecs.size());
        if (register_result != 0) {
          throw std::runtime_error("Failed to register buffers with io_uring");
        }
        std::cout << "Successfully registered buffers with io_uring"
                  << std::endl;
      }
    }
  }

  ~BufferPool() {
    if constexpr (STUPID_BUFFER_MODE) {
      return;
    }

    if constexpr (ALLOCATE_REGISTERED_BUFFERS) {
      io_uring_unregister_buffers(ring);
    }

    for (auto& [size, pool] : pools) {
      for (size_t i = 0; i < capacity; ++i) {
        auto* buffer = static_cast<char*>(pool[i]->iov_base);
        if constexpr (ALLOCATE_PIN) {
          munlock(buffer, size);
        }
        std::free(buffer);
        delete pool[i];
      }
      delete[] pool;
      delete[] states[size];
    }
  }

  CheckedOutBuf check_out(const size_t size) {
    if constexpr (STUPID_BUFFER_MODE) {
      return stupid_buffer;
    }

    if (const auto it = pools.find(size); it != pools.end()) {
      const auto& pool = it->second;
      const auto& state = states[size];
      for (size_t i = 0; i < capacity; ++i) {
        if (const size_t index = (next_index[size] + i) % capacity;
            state[index] == BufferState::Free) {
          state[index] = BufferState::CheckedOut;
          next_index[size] = (index + 1) % capacity;
          return {*pool[index], static_cast<uint16_t>(index)};
        }
      }
    }
    throw std::bad_alloc();
  }

  void check_in(size_t size, uint16_t index) {
    if constexpr (STUPID_BUFFER_MODE) {
      return;
    }

    const auto it = pools.find(size);
    if (it != pools.end()) {
      const auto& state = states[size];
      state[index] = BufferState::Free;
    }
  }

 private:
  struct io_uring* ring;
  size_t capacity;
  std::unordered_map<size_t, iovec**> pools;
  std::unordered_map<size_t, BufferState*> states;
  std::unordered_map<size_t, size_t> next_index;
  std::vector<struct iovec> stupid_iovec;
  CheckedOutBuf stupid_buffer;
};
