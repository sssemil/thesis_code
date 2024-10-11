#pragma once

#include <array>
#include <stdexcept>
#include <vector>

#include "consts.hpp"

class IFillingStrategy {
 public:
  virtual ~IFillingStrategy() = default;
  [[nodiscard]] virtual uint8_t get_value_at(size_t index) const = 0;
};

class AlphabeticalFillingStrategy final : public IFillingStrategy {
 public:
  [[nodiscard]] uint8_t get_value_at(const size_t index) const override {
    return 'a' + index % 26;
  }
};

class PseudoRandomFillingStrategy final : public IFillingStrategy {
  unsigned int seed;

 public:
  explicit PseudoRandomFillingStrategy(
      const unsigned int seed = PSEUDO_RANDOM_SEED)
      : seed(seed) {}

  [[nodiscard]] uint8_t get_value_at(const size_t index) const override {
//    return static_cast<uint8_t>((index * 2654435761u + seed) % 256);
    return 0xAA;
  }
};

template <size_t PAGE_SIZE>
class MemoryBlock {
 public:
  std::vector<uint8_t> data;
  const IFillingStrategy* strategy;

  MemoryBlock(const size_t page_count, const IFillingStrategy* strategy)
      : data(PAGE_SIZE * page_count, 0), strategy(strategy) {
    if (strategy == nullptr) {
      throw std::runtime_error("Filling strategy is not set");
    }
    fill();
  }

  void fill() {
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = strategy->get_value_at(i);
    }
  }
};

template <size_t PAGE_SIZE>
class MemoryBlockVerifier {
 public:
  size_t page_count;
  const IFillingStrategy* strategy;

  MemoryBlockVerifier(const size_t page_count, const IFillingStrategy* strategy)
      : page_count(page_count), strategy(strategy) {
    if (strategy == nullptr) {
      throw std::runtime_error("Filling strategy is not set");
    }
  }

  [[nodiscard]] bool verify(const std::array<uint8_t, PAGE_SIZE>& page_content,
                            const size_t page_number) const {
    if (page_number >= page_count) {
      return false;
    }
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
      if (page_content[i] !=
          strategy->get_value_at(page_number * PAGE_SIZE + i)) {
        return false;
      }
    }
    return true;
  }
};
