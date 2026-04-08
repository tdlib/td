// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
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
