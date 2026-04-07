//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/IptController.h"

#include "td/utils/tests.h"

#include <cmath>
#include <deque>

namespace {

using td::mtproto::stealth::IptController;
using td::mtproto::stealth::IptParams;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::TrafficHint;

class SequenceRng final : public IRng {
 public:
  explicit SequenceRng(std::initializer_list<td::uint32> values) : values_(values) {
  }

  void fill_secure_bytes(td::MutableSlice dest) final {
    for (size_t i = 0; i < dest.size(); i++) {
      dest[i] = static_cast<char>(next_value() & 0xff);
    }
  }

  td::uint32 secure_uint32() final {
    return next_value();
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0);
    return next_value() % n;
  }

 private:
  td::uint32 next_value() {
    if (values_.empty()) {
      return 0;
    }
    auto value = values_.front();
    values_.pop_front();
    return value;
  }

  std::deque<td::uint32> values_;
};

TEST(IptControllerPrecisionAdversarial, TinyPositiveBurstDelayDoesNotCollapseToZeroMicroseconds) {
  IptParams params;
  params.burst_mu_ms = std::log(0.0004);
  params.burst_sigma = 0.0;
  params.burst_max_ms = 0.0004;
  params.idle_alpha = 2.0;
  params.idle_scale_ms = 10.0;
  params.idle_max_ms = 20.0;
  params.p_burst_stay = 1.0;
  params.p_idle_to_burst = 1.0;

  SequenceRng rng({0u, 0u});
  IptController controller(params, rng);

  ASSERT_EQ(1u, controller.next_delay_us(true, TrafficHint::Interactive));
}

TEST(IptControllerPrecisionAdversarial, TinyPositiveIdleDelayDoesNotCollapseToZeroMicroseconds) {
  IptParams params;
  params.burst_mu_ms = std::log(20.0);
  params.burst_sigma = 0.0;
  params.burst_max_ms = 20.0;
  params.idle_alpha = 2.0;
  params.idle_scale_ms = 0.0004;
  params.idle_max_ms = 0.0008;
  params.p_burst_stay = 0.0;
  params.p_idle_to_burst = 0.0;

  SequenceRng rng({0xffffffffu, 0u});
  IptController controller(params, rng);

  ASSERT_EQ(1u, controller.next_delay_us(true, TrafficHint::Interactive));
}

}  // namespace