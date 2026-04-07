//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/IptController.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace {

using td::mtproto::stealth::IptController;
using td::mtproto::stealth::IptParams;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockRng;

double mean_us(const std::vector<td::uint64> &samples) {
  auto sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  return sum / static_cast<double>(samples.size());
}

td::uint64 percentile_us(std::vector<td::uint64> samples, double p) {
  CHECK(!samples.empty());
  std::sort(samples.begin(), samples.end());
  auto index = static_cast<size_t>(p * static_cast<double>(samples.size() - 1));
  return samples[index];
}

std::vector<td::uint64> collect_samples(IptController &controller, size_t count) {
  std::vector<td::uint64> result;
  result.reserve(count);
  for (size_t i = 0; i < count; i++) {
    result.push_back(controller.next_delay_us(true, TrafficHint::Interactive));
  }
  return result;
}

TEST(IptControllerStatistics, BurstDistributionShowsRightSkewWithoutCapCollapse) {
  IptParams params;
  params.burst_mu_ms = std::log(20.0);
  params.burst_sigma = 0.65;
  params.burst_max_ms = 250.0;
  params.idle_alpha = 2.0;
  params.idle_scale_ms = 10.0;
  params.idle_max_ms = 100.0;
  params.p_burst_stay = 1.0;
  params.p_idle_to_burst = 1.0;

  MockRng rng(0xBADC0FFEu);
  IptController controller(params, rng);
  auto samples = collect_samples(controller, 4096);

  std::unordered_set<td::uint64> unique(samples.begin(), samples.end());
  auto p50 = percentile_us(samples, 0.50);
  auto p90 = percentile_us(samples, 0.90);
  auto p99 = percentile_us(samples, 0.99);
  auto avg = mean_us(samples);
  auto cap_us = static_cast<td::uint64>(params.burst_max_ms * 1000.0);
  auto at_cap = static_cast<size_t>(std::count(samples.begin(), samples.end(), cap_us));

  ASSERT_TRUE(unique.size() > 2048u);
  ASSERT_TRUE(avg > static_cast<double>(p50));
  ASSERT_TRUE(p90 > p50 + 10000u);
  ASSERT_TRUE(p99 > p90);
  ASSERT_EQ(0u, at_cap);
  ASSERT_TRUE(p99 <= cap_us);
}

TEST(IptControllerStatistics, IdleDistributionRemainsHeavyTailedWithoutHardCapSpike) {
  IptParams params;
  params.burst_mu_ms = std::log(20.0);
  params.burst_sigma = 0.25;
  params.burst_max_ms = 250.0;
  params.idle_alpha = 1.35;
  params.idle_scale_ms = 8.0;
  params.idle_max_ms = 150.0;
  params.p_burst_stay = 0.0;
  params.p_idle_to_burst = 0.0;

  MockRng rng(0x12345678u);
  IptController controller(params, rng);
  auto samples = collect_samples(controller, 8192);

  std::unordered_set<td::uint64> unique(samples.begin(), samples.end());
  auto p50 = percentile_us(samples, 0.50);
  auto p95 = percentile_us(samples, 0.95);
  auto p99 = percentile_us(samples, 0.99);
  auto max_us = static_cast<td::uint64>(params.idle_max_ms * 1000.0);
  auto near_cap = static_cast<size_t>(
      std::count_if(samples.begin(), samples.end(), [max_us](td::uint64 value) { return value + 1000u >= max_us; }));

  ASSERT_TRUE(unique.size() > 4096u);
  ASSERT_TRUE(p50 >= 8000u);
  ASSERT_TRUE(p95 > p50 * 2u);
  ASSERT_TRUE(p99 > p95);
  ASSERT_TRUE(p99 < max_us);
  ASSERT_TRUE(near_cap < samples.size() / 100);
}

}  // namespace