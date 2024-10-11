#pragma once

#include <array>
#include <cstdint>

#include "../consts.hpp"

enum GetPageStatus : uint32_t {
  SUCCESS = 200,
  INVALID_PAGE_NUMBER = 400,
};

#pragma pack(push, 1)
struct GetPageRequest {
  uint32_t request_id;
  uint32_t page_number;

  void to_network_order() {
    request_id = htonl(request_id);
    page_number = htonl(page_number);
  }

  void to_host_order() {
    request_id = ntohl(request_id);
    page_number = ntohl(page_number);
  }
};
#pragma pack(pop)

#pragma pack(push, 1)
struct GetPageResponseHeader {
  uint32_t request_id;
  uint32_t status;
  uint32_t page_number;

  void to_network_order() {
    request_id = htonl(request_id);
    status = htonl(status);
    page_number = htonl(page_number);
  }

  void to_host_order() {
    request_id = ntohl(request_id);
    status = ntohl(status);
    page_number = ntohl(page_number);
  }

  [[nodiscard]] GetPageStatus get_status() const {
    return static_cast<GetPageStatus>(status);
  }
};
#pragma pack(pop)

#pragma pack(push, 1)
struct GetPageResponse {
  GetPageResponseHeader header;
  std::array<uint8_t, PAGE_SIZE> content;

  [[nodiscard]] GetPageStatus get_status() const { return header.get_status(); }

  void to_network_order() { header.to_network_order(); }

  void to_host_order() { header.to_host_order(); }
};
#pragma pack(pop)
