//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

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

enum class PaddingPolicy : uint8 {
  LegacyFixedTarget,
  ProfileDriven,
  Adaptive,
};

struct NetworkRouteHints final {
  bool is_known{false};
  bool is_ru{false};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
