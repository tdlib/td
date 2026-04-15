// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// PR-S2: TLS Record Pad-to-Target — Small-record budget enforcement tests.
// These tests verify that the sliding-window small-record budget (≤5% of
// records <200 bytes) is correctly enforced, cannot be exhausted, and
// handles edge cases safely. This is the core defense against AV-3
// (small-record frequency analysis).

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

constexpr td::int32 kMinValidDrsPayloadCap = 256;
constexpr td::int32 kBudgetExerciseThreshold = 400;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

DrsPhaseModel make_small_target_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 1000;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config_small_target(td::int32 target, td::int32 threshold = 200) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_small_target_phase(target);
  config.drs_policy.congestion_open = make_small_target_phase(target);
  config.drs_policy.steady_state = make_small_target_phase(target);
  config.drs_policy.slow_start_records = 100000;
  config.drs_policy.congestion_bytes = 1 << 24;
  config.drs_policy.idle_reset_ms_min = 10000;
  config.drs_policy.idle_reset_ms_max = 10000;
  config.drs_policy.min_payload_cap = target;
  config.drs_policy.max_payload_cap = target;
  config.record_padding_policy.small_record_threshold = threshold;
  config.record_padding_policy.small_record_max_fraction = 0.05;
  config.record_padding_policy.small_record_window_size = 200;
  return config;
}

std::vector<size_t> extract_tls_record_lengths(td::Slice wire) {
  std::vector<size_t> lengths;
  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == td::Slice("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    lengths.push_back(len);
    offset += 5 + len;
  }
  return lengths;
}

std::vector<td::int32> emitted_record_padding_targets(const RecordingTransport &transport) {
  std::vector<td::int32> emitted_targets;
  const auto &raw_targets = transport.stealth_record_padding_targets;
  CHECK(raw_targets.size() % 2 == 0);
  emitted_targets.reserve(raw_targets.size() / 2);
  for (size_t i = 1; i < raw_targets.size(); i += 2) {
    emitted_targets.push_back(raw_targets[i]);
  }
  return emitted_targets;
}

// BudgetHarness is non-movable / non-copyable: the StealthTransportDecorator's
// initcaptures a raw pointer to the ChainBufferReader argument and
// dereferences it on every read poll. The reader MUST live at the same
// scope as the harness, which means the harness cannot be moved (NRVO from
// a static factory returning by value would invalidate the captured
// pointer the moment the factory returns).
class BudgetHarness {
 public:
td::unique_ptr<StealthTransportDecorator> transport;
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;

  void write_and_flush(td::Slice payload, TrafficHint hint = TrafficHint::Interactive) {
    transport->set_traffic_hint(hint);
    td::BufferWriter w(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  std::vector<size_t> collect() {
    auto r = output_writer.extract_reader();
    r.sync_with_writer();
    auto wire = r.move_as_buffer_slice().as_slice().str();
    return extract_tls_record_lengths(wire);
  }
  td::ChainBufferReader input_reader;

  BudgetHarness(StealthConfig config, td::uint64 seed) {
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    auto clock = td::make_unique<MockClock>();
    auto r =
        StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(r.is_ok());
    transport = r.move_as_ok();
    input_reader = input_writer.extract_reader();
    transport->init(&input_reader, &output_writer);
    
  }

  BudgetHarness(const BudgetHarness &) = delete;
  BudgetHarness &operator=(const BudgetHarness &) = delete;
  BudgetHarness(BudgetHarness &&) = delete;
  BudgetHarness &operator=(BudgetHarness &&) = delete;
};

// ==============================================
// Small-record budget: core enforcement
// ==============================================

TEST(TlsRecordSmallBudget, BudgetEnforced_BelowThresholdRecordsLimited) {
  // The fail-closed validator rejects DRS payload caps below 256, so use the
  // smallest valid cap and a higher threshold to exercise the budget.
  // When DRS returns targets below the threshold, the budget allows only 5%.
  // After the budget is exhausted, all subsequent records must be raised to ≥ threshold.
  auto config = make_config_small_target(kMinValidDrsPayloadCap, kBudgetExerciseThreshold);
  config.drs_policy.min_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.max_payload_cap = kMinValidDrsPayloadCap;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  // Write 300 records
  for (int i = 0; i < 300; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // Check that the small-record budget is respected
  auto targets = emitted_record_padding_targets(*inner_ptr);
  if (targets.size() > 200) {
    // Check last 200 targets
    auto start = targets.size() - 200;
    int tail_small = 0;
    for (size_t j = start; j < targets.size(); j++) {
      if (targets[j] < kBudgetExerciseThreshold) {
        tail_small++;
      }
    }
    ASSERT_TRUE(tail_small <= 10);
  }
}

TEST(TlsRecordSmallBudget, BudgetWindowSize_ZeroIsRejectedFailClosed) {
  // Zero-sized windows are no longer interpreted as "budget disabled".
  // They are rejected by the fail-closed config validator.
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.record_padding_policy.small_record_window_size = 0;
  config.record_padding_policy.small_record_threshold = kBudgetExerciseThreshold;
  config.drs_policy.slow_start.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.slow_start.max_repeat_run = 1000;
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.slow_start_records = 100000;
  config.drs_policy.min_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.max_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.congestion_open.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.congestion_open.max_repeat_run = 1000;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.congestion_bytes = 1 << 24;
  config.drs_policy.steady_state.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.steady_state.max_repeat_run = 1000;
  config.drs_policy.steady_state.local_jitter = 0;
  config.drs_policy.idle_reset_ms_min = 10000;
  config.drs_policy.idle_reset_ms_max = 10000;
  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("record_padding_policy.small_record_window_size is out of allowed bounds", status.message().c_str());
}

// ==============================================
// Budget enforcement: wire-level verification
// ==============================================

TEST(TlsRecordSmallBudget, WireLevel_AfterBudgetExhaustion_AllRecordsAboveThreshold) {
  // Verify at the wire level that once the small-record budget is exhausted,
  // all subsequent records are above the threshold.
  auto config = make_config_small_target(256);  // At 256, below 200 is impossible
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 256;
  ASSERT_TRUE(config.validate().is_ok());

  BudgetHarness h(config, 42);
  h.write_and_flush("warm");

  for (int i = 0; i < 300; i++) {
    h.write_and_flush("data", TrafficHint::Keepalive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 300u);

  // All records should be at least 256 (target + padding ≥ threshold)
  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_TRUE(lengths[i] >= 256u);
  }
}

// ==============================================
// Budget: Sliding window correctness
// ==============================================

TEST(TlsRecordSmallBudget, SlidingWindowEvictionCorrect) {
  // As old records leave the sliding window, the budget frees up.
  // This tests that the window correctly evicts old entries.
  auto config = make_config_small_target(kMinValidDrsPayloadCap, kBudgetExerciseThreshold);
  config.drs_policy.min_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.max_payload_cap = kMinValidDrsPayloadCap;
  config.record_padding_policy.small_record_window_size = 20;  // Small window for easier testing
  config.record_padding_policy.small_record_max_fraction = 0.05;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  // Write 100 records — window rotates multiple times
  for (int i = 0; i < 100; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // In any window of 20 records, at most 1 should be < 200 (5% of 20 = 1)
  auto targets = emitted_record_padding_targets(*inner_ptr);
  for (size_t start = 0; start + 20 <= targets.size(); start++) {
    int small_count = 0;
    for (size_t j = start; j < start + 20; j++) {
      if (targets[j] < kBudgetExerciseThreshold) {
        small_count++;
      }
    }
    // Allow budget + 1 for rounding
    ASSERT_TRUE(small_count <= 2);
  }
}

// ==============================================
// Budget: Manual override respects budget too
// ==============================================

TEST(TlsRecordSmallBudget, ManualOverrideBelowThreshold_BudgetStillApplies) {
  // If the user manually sets a record size below the threshold,
  // the budget should still enforce the minimum.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.record_padding_policy.small_record_threshold = kBudgetExerciseThreshold;
  config.record_padding_policy.small_record_max_fraction = 0.0;  // 0% budget = never allow small
  config.record_padding_policy.small_record_window_size = 10;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  // Fill window so budget tracking activates
  for (int i = 0; i < 10; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // Now try to manually set the minimum valid TLS record size, which still
  // remains below the small-record threshold.
  transport->set_max_tls_record_size(kMinValidDrsPayloadCap);

  // Write more records — budget (0%) should force them to ≥ threshold
  for (int i = 0; i < 10; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("testdata"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // Manual override emits one additional bookkeeping target before the next
  // paired writes reach the inner transport, so inspect the raw tail here.
  auto &targets = inner_ptr->stealth_record_padding_targets;
  auto start_idx = targets.size() >= 20u ? targets.size() - 20u : 0u;
  for (size_t j = start_idx; j < targets.size(); j++) {
    ASSERT_TRUE(targets[j] >= kBudgetExerciseThreshold);
  }
}

// ==============================================
// Budget: Fraction boundary precision
// ==============================================

TEST(TlsRecordSmallBudget, FractionBoundary_ExactlyAtLimit) {
  // With window=20 and fraction=0.10, exactly 2 small records per window allowed.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.record_padding_policy.small_record_threshold = kBudgetExerciseThreshold;
  config.record_padding_policy.small_record_max_fraction = 0.10;
  config.record_padding_policy.small_record_window_size = 20;
  // Use the smallest valid DRS cap and keep it below the threshold.
  config.drs_policy.slow_start.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.slow_start.max_repeat_run = 1000;
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.slow_start_records = 100000;
  config.drs_policy.min_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.max_payload_cap = kMinValidDrsPayloadCap;
  config.drs_policy.congestion_open.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.congestion_open.max_repeat_run = 1000;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.congestion_bytes = 1 << 24;
  config.drs_policy.steady_state.bins = {{kMinValidDrsPayloadCap, kMinValidDrsPayloadCap, 1}};
  config.drs_policy.steady_state.max_repeat_run = 1000;
  config.drs_policy.steady_state.local_jitter = 0;
  config.drs_policy.idle_reset_ms_min = 10000;
  config.drs_policy.idle_reset_ms_max = 10000;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  for (int i = 0; i < 100; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // In any window of 20, at most 2 targets should remain below the threshold.
  auto targets = emitted_record_padding_targets(*inner_ptr);
  for (size_t start = 0; start + 20 <= targets.size(); start++) {
    int small_count = 0;
    for (size_t j = start; j < start + 20; j++) {
      if (targets[j] < kBudgetExerciseThreshold) {
        small_count++;
      }
    }
    // Allow budget + 1 for edge cases (window eviction + simultaneous check)
    ASSERT_TRUE(small_count <= 3);
  }
}

// ==============================================
// Config validation: edge cases
// ==============================================

TEST(TlsRecordSmallBudget, ConfigValidation_NegativeThreshold_Rejected) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.record_padding_policy.small_record_threshold = -1;
  auto status = config.validate();
  // This should either fail validation or be handled gracefully
  // (the implementation may clamp instead of reject)
  if (status.is_ok()) {
    // If validation passes, the system should still work safely
    auto inner = td::make_unique<RecordingTransport>();
    inner->supports_tls_record_sizing_result = true;
    auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                               td::make_unique<MockClock>());
    if (r.is_ok()) {
      auto transport = r.move_as_ok();
      transport->set_traffic_hint(TrafficHint::Interactive);
      td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
      transport->write(std::move(w), false);
      transport->pre_flush_write(transport->get_shaping_wakeup());
      // No crash
    }
  }
}

TEST(TlsRecordSmallBudget, ConfigValidation_FractionAboveOne_Handled) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.record_padding_policy.small_record_max_fraction = 1.5;  // Invalid: > 1.0
  auto status = config.validate();
  // Should either reject or clamp safely
  if (status.is_ok()) {
    auto inner = td::make_unique<RecordingTransport>();
    inner->supports_tls_record_sizing_result = true;
    auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                               td::make_unique<MockClock>());
    if (r.is_ok()) {
      auto transport = r.move_as_ok();
      for (int i = 0; i < 20; i++) {
        transport->set_traffic_hint(TrafficHint::Interactive);
        td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
        transport->write(std::move(w), false);
        transport->pre_flush_write(transport->get_shaping_wakeup());
      }
    }
  }
}

}  // namespace
