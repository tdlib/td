//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

namespace td {
namespace mtproto {
namespace test {

class MockClock final : public stealth::IClock {
 public:
  double now() const final {
    return time_;
  }

  void advance(double seconds) {
    time_ += seconds;
  }

 private:
  double time_{1000.0};
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
