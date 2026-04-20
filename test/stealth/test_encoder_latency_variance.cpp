// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Timing-side-channel guard tests for TLS hello builder paths.

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;

double mean_of(const std::vector<td::int64> &values) {
  if (values.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (auto value : values) {
    sum += static_cast<double>(value);
  }
  return sum / static_cast<double>(values.size());
}

double relative_range(const std::vector<td::int64> &values) {
  if (values.empty()) {
    return 0.0;
  }
  auto minmax = std::minmax_element(values.begin(), values.end());
  auto mean = mean_of(values);
  if (mean <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(*minmax.second - *minmax.first) / mean;
}

NetworkRouteHints make_non_ru_hints() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return hints;
}

TEST(EncoderLatencyVariance, BuilderLatencyRangeStaysBoundedForRepeatedInputs) {
  using clock_t = std::chrono::high_resolution_clock;

  std::vector<td::int64> timings;
  timings.reserve(80);

  auto hints = make_non_ru_hints();
  for (td::int32 i = 0; i < 80; i++) {
    auto start = clock_t::now();
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    auto end = clock_t::now();
    ASSERT_FALSE(wire.empty());
    timings.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  }

  ASSERT_FALSE(timings.empty());
  // Keep broad bound to avoid flaky failures on loaded CI hosts.
  ASSERT_TRUE(relative_range(timings) < 4.0);
}

TEST(EncoderLatencyVariance, DomainLengthDoesNotCauseExtremeTimingDrift) {
  using clock_t = std::chrono::high_resolution_clock;

  std::vector<td::int64> short_timings;
  std::vector<td::int64> long_timings;
  short_timings.reserve(60);
  long_timings.reserve(60);

  const td::string short_domain = "a.com";
  const td::string long_domain =
      "subdomain1.subdomain2.subdomain3.subdomain4.subdomain5."
      "subdomain6.subdomain7.subdomain8.subdomain9.subdomain10."
      "subdomain11.subdomain12.subdomain13.subdomain14.subdomain15."
      "example.telecommunications.infrastructure.security.research.com";

  auto hints = make_non_ru_hints();
  for (td::int32 i = 0; i < 60; i++) {
    auto start_short = clock_t::now();
    auto short_wire = build_default_tls_client_hello(short_domain, "0123456789secret", 1712345678 + i, hints);
    auto end_short = clock_t::now();
    ASSERT_FALSE(short_wire.empty());
    short_timings.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end_short - start_short).count());

    auto start_long = clock_t::now();
    auto long_wire = build_default_tls_client_hello(long_domain, "0123456789secret", 1712345678 + i, hints);
    auto end_long = clock_t::now();
    ASSERT_FALSE(long_wire.empty());
    long_timings.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end_long - start_long).count());
  }

  auto short_mean = mean_of(short_timings);
  auto long_mean = mean_of(long_timings);
  auto denom = std::max(short_mean, long_mean);
  ASSERT_TRUE(denom > 0.0);

  auto drift = std::abs(short_mean - long_mean) / denom;
  // Broad defensive bound: catches pathological dependence while remaining stable.
  ASSERT_TRUE(drift < 0.85);
}

TEST(EncoderLatencyVariance, ProfileRoutingDoesNotCreatePathologicalOutliers) {
  using clock_t = std::chrono::high_resolution_clock;

  std::vector<td::int64> ru_timings;
  std::vector<td::int64> non_ru_timings;
  ru_timings.reserve(60);
  non_ru_timings.reserve(60);

  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  auto non_ru_hints = make_non_ru_hints();

  for (td::int32 i = 0; i < 60; i++) {
    auto start_ru = clock_t::now();
    auto ru_wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, ru_hints);
    auto end_ru = clock_t::now();
    ASSERT_FALSE(ru_wire.empty());
    ru_timings.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end_ru - start_ru).count());

    auto start_non_ru = clock_t::now();
    auto non_ru_wire =
        build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, non_ru_hints);
    auto end_non_ru = clock_t::now();
    ASSERT_FALSE(non_ru_wire.empty());
    non_ru_timings.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end_non_ru - start_non_ru).count());
  }

  ASSERT_TRUE(relative_range(ru_timings) < 4.0);
  ASSERT_TRUE(relative_range(non_ru_timings) < 4.0);
}

}  // namespace
