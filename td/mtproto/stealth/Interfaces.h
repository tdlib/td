// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <cctype>

namespace td {
namespace mtproto {
namespace stealth {

class IRng {
 public:
  virtual ~IRng() = default;

  virtual void fill_secure_bytes(MutableSlice dest) = 0;
  virtual uint32 secure_uint32() = 0;
  virtual uint32 bounded(uint32 n) = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;

  virtual double now() const = 0;
};

enum class TrafficHint : uint8 {
  Unknown = 0,
  Interactive = 1,
  Keepalive = 2,
  BulkData = 3,
  AuthHandshake = 4,
};

inline StringBuilder &operator<<(StringBuilder &sb, TrafficHint hint) {
  switch (hint) {
    case TrafficHint::Unknown:
      return sb << "Unknown";
    case TrafficHint::Interactive:
      return sb << "Interactive";
    case TrafficHint::Keepalive:
      return sb << "Keepalive";
    case TrafficHint::BulkData:
      return sb << "BulkData";
    case TrafficHint::AuthHandshake:
      return sb << "AuthHandshake";
  }
  return sb << static_cast<int>(hint);
}

struct PaddingPolicy final {
  bool enabled{true};

  size_t compute_padding_content_len(size_t unpadded_len) const noexcept {
    if (!enabled) {
      return 0;
    }
    if (unpadded_len <= 0xFF || unpadded_len >= 0x200) {
      return 0;
    }
    auto padding_len = 0x200 - unpadded_len;
    if (padding_len >= 5) {
      return padding_len - 4;
    }
    return 1;
  }
};

inline PaddingPolicy no_padding_policy() {
  PaddingPolicy policy;
  policy.enabled = false;
  return policy;
}

inline size_t resolve_padding_extension_payload_len(const PaddingPolicy &padding_policy, size_t unpadded_len,
                                                    size_t padding_entropy_len) noexcept {
  auto padding_content_len = padding_policy.compute_padding_content_len(unpadded_len);
  if (padding_content_len > 0) {
    return padding_content_len;
  }
  if (padding_entropy_len > 0) {
    return 1 + padding_entropy_len;
  }
  return 0;
}

struct NetworkRouteHints final {
  bool is_known{false};
  bool is_ru{false};
};

inline NetworkRouteHints route_hints_from_country_code(Slice country_code) {
  NetworkRouteHints hints;
  if (country_code.size() != 2) {
    return hints;
  }

  auto first = static_cast<unsigned char>(country_code[0]);
  auto second = static_cast<unsigned char>(country_code[1]);
  if (std::isalpha(first) == 0 || std::isalpha(second) == 0) {
    return hints;
  }

  auto upper_first = static_cast<char>(std::toupper(first));
  auto upper_second = static_cast<char>(std::toupper(second));

  hints.is_known = true;
  hints.is_ru = (upper_first == 'R' && upper_second == 'U');
  return hints;
}

unique_ptr<IRng> make_connection_rng();
unique_ptr<IClock> make_clock();

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
