// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/mtproto/stealth/IptController.h"
#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::mtproto::stealth::IptController;
using td::mtproto::stealth::IptParams;
using td::mtproto::stealth::IRng;

class DeterministicRng final : public IRng {
 public:
  explicit DeterministicRng(td::uint32 seed) : state_(seed) {
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
  td::uint32 state_;

  td::uint32 next_value() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }
};

static IptParams make_test_params() {
  IptParams p;
  p.burst_mu_ms = std::log(50.0);
  p.burst_sigma = 0.0;
  p.burst_max_ms = 50.0;
  p.idle_alpha = 2.0;
  p.idle_scale_ms = 10.0;
  p.idle_max_ms = 100.0;
  p.p_burst_stay = 1.0;
  p.p_idle_to_burst = 1.0;
  return p;
}

static std::vector<td::uint64> collect_idle_samples(IptController &ctrl, int count) {
  std::vector<td::uint64> samples;
  samples.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    samples.push_back(ctrl.sample_idle_delay_us());
  }
  return samples;
}

TEST(IptIdleSamplerAdversarial, TruncationAttack) {
  auto p = make_test_params();
  p.idle_max_ms = 500.0;
  p.idle_scale_ms = 490.0;
  p.idle_alpha = 1.5;

  DeterministicRng rng(1);
  IptController ctrl(p, rng);

  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);
  auto samples = collect_idle_samples(ctrl, 100);

  for (auto s : samples) {
    ASSERT_TRUE(s <= max_us);
    ASSERT_TRUE(s > 0u);
  }
}

TEST(IptIdleSamplerAdversarial, SmallAlphaUniformity) {
  auto p = make_test_params();
  p.idle_alpha = 0.1;
  p.idle_scale_ms = 100.0;
  p.idle_max_ms = 1000.0;

  DeterministicRng rng(2);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 50);
  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);
  auto scale_us = static_cast<td::uint64>(p.idle_scale_ms * 1000.0);

  for (auto s : samples) {
    ASSERT_TRUE(s >= scale_us);
    ASSERT_TRUE(s <= max_us);
  }
}

TEST(IptIdleSamplerAdversarial, LargeAlphaSkewness) {
  auto p = make_test_params();
  p.idle_alpha = 5.0;
  p.idle_scale_ms = 100.0;
  p.idle_max_ms = 500.0;

  DeterministicRng rng(3);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 200);
  auto scale_us = static_cast<td::uint64>(p.idle_scale_ms * 1000.0);

  size_t near_scale_count = 0;
  for (auto s : samples) {
    if (s <= scale_us * 2) {
      near_scale_count++;
    }
  }
  ASSERT_TRUE(near_scale_count * 2 > samples.size());
}

TEST(IptIdleSamplerAdversarial, ScaleEqualsMax) {
  auto p = make_test_params();
  p.idle_scale_ms = 500.0;
  p.idle_max_ms = 500.0;
  p.idle_alpha = 2.0;

  DeterministicRng rng(4);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 50);
  auto scale_us = static_cast<td::uint64>(p.idle_scale_ms * 1000.0);

  for (auto s : samples) {
    ASSERT_EQ(s, scale_us);
  }
}

TEST(IptIdleSamplerAdversarial, VeryLargeScaleValues) {
  auto p = make_test_params();
  p.idle_scale_ms = 1e6;
  p.idle_max_ms = 1e6 + 1000.0;
  p.idle_alpha = 1.5;

  DeterministicRng rng(5);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 20);
  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);

  for (auto s : samples) {
    ASSERT_TRUE(s > 0u);
    ASSERT_TRUE(s <= max_us);
  }
}

TEST(IptIdleSamplerAdversarial, MinimumAlpha) {
  auto p = make_test_params();
  p.idle_alpha = 0.1;
  p.idle_scale_ms = 50.0;
  p.idle_max_ms = 500.0;

  DeterministicRng rng(6);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 100);
  auto scale_us = static_cast<td::uint64>(p.idle_scale_ms * 1000.0);
  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);

  for (auto s : samples) {
    ASSERT_TRUE(s >= scale_us);
    ASSERT_TRUE(s <= max_us);
  }
}

TEST(IptIdleSamplerAdversarial, ExtremelySmallAlphaStillProducesBoundedDelay) {
  auto p = make_test_params();
  p.idle_alpha = 1e-300;
  p.idle_scale_ms = 50.0;
  p.idle_max_ms = 500.0;

  DeterministicRng rng(7);
  IptController ctrl(p, rng);

  auto samples = collect_idle_samples(ctrl, 64);
  auto scale_us = static_cast<td::uint64>(p.idle_scale_ms * 1000.0);
  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);

  for (auto s : samples) {
    ASSERT_TRUE(s >= scale_us);
    ASSERT_TRUE(s <= max_us);
    ASSERT_TRUE(s != std::numeric_limits<td::uint64>::max());
  }
}

TEST(IptIdleSamplerAdversarial, DeterministicReproducibility) {
  auto p = make_test_params();

  DeterministicRng rng1(8);
  IptController ctrl1(p, rng1);
  auto seq1 = collect_idle_samples(ctrl1, 100);

  DeterministicRng rng2(8);
  IptController ctrl2(p, rng2);
  auto seq2 = collect_idle_samples(ctrl2, 100);

  ASSERT_EQ(seq1.size(), seq2.size());
  for (size_t i = 0; i < seq1.size(); ++i) {
    ASSERT_EQ(seq1[i], seq2[i]);
  }
}

TEST(IptIdleSamplerAdversarial, IndependentControllersProduceDifferentSequences) {
  auto p = make_test_params();

  DeterministicRng rng1(9);
  IptController ctrl1(p, rng1);
  auto seq1 = collect_idle_samples(ctrl1, 50);

  DeterministicRng rng2(10);
  IptController ctrl2(p, rng2);
  auto seq2 = collect_idle_samples(ctrl2, 50);

  ASSERT_TRUE(seq1 != seq2);
}

TEST(IptIdleSamplerAdversarial, StressLargeSampleCount) {
  auto p = make_test_params();
  p.idle_alpha = 1.5;
  p.idle_scale_ms = 100.0;
  p.idle_max_ms = 500.0;

  DeterministicRng rng(11);
  IptController ctrl(p, rng);

  auto max_us = static_cast<td::uint64>(p.idle_max_ms * 1000.0);
  constexpr int kLargeSampleCount = 10000;

  for (int i = 0; i < kLargeSampleCount; ++i) {
    auto delay = ctrl.sample_idle_delay_us();
    ASSERT_TRUE(delay > 0u);
    ASSERT_TRUE(delay <= max_us);
  }
}

}  // namespace
