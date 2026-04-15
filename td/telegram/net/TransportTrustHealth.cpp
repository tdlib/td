//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetReliabilityMonitor.h"

#include <algorithm>
#include <array>
#include <mutex>

namespace td {
namespace net_health {
namespace {

constexpr double REAUTH_DELAY = 2.0;
constexpr double DESTROY_BURST_WINDOW = 30.0;

struct Storage final {
  std::mutex mutex;
  NetMonitorCounters counters;
  std::array<double, DcId::MAX_RAW_DC_ID + 1> reauth_not_before{};
  std::array<double, DcId::MAX_RAW_DC_ID + 1> last_destroy_at{};
};

Storage &storage() {
  static Storage result;
  return result;
}

bool is_tracked_dc_id(int32 dc_id) {
  return 1 <= dc_id && dc_id <= DcId::MAX_RAW_DC_ID;
}

NetMonitorState resolve_health_state(const NetMonitorCounters &counters) {
  if (counters.session_param_coerce_attempt_total != 0 || counters.bind_encrypted_message_invalid_total != 0 ||
      counters.main_key_set_cardinality_failure_total != 0 || counters.low_server_fingerprint_count_total != 0 ||
      counters.auth_key_destroy_burst_total != 0) {
    return NetMonitorState::Suspicious;
  }
  if (counters.bind_retry_budget_exhausted_total != 0 || counters.auth_key_destroy_total != 0) {
    return NetMonitorState::Degraded;
  }
  return NetMonitorState::Healthy;
}

}  // namespace

void note_session_param_coerce_attempt() noexcept {
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.session_param_coerce_attempt_total++;
}

void note_bind_encrypted_message_invalid(int32 dc_id, bool has_immunity) noexcept {
  static_cast<void>(dc_id);
  static_cast<void>(has_immunity);
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.bind_encrypted_message_invalid_total++;
}

void note_bind_retry_budget_exhausted(int32 dc_id) noexcept {
  static_cast<void>(dc_id);
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.bind_retry_budget_exhausted_total++;
}

void note_main_key_set_cardinality_failure(bool is_test, size_t observed_count, size_t expected_count) noexcept {
  static_cast<void>(is_test);
  static_cast<void>(observed_count);
  static_cast<void>(expected_count);
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.main_key_set_cardinality_failure_total++;
}

void note_low_server_fingerprint_count(size_t observed_count) noexcept {
  static_cast<void>(observed_count);
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.low_server_fingerprint_count_total++;
}

void note_auth_key_destroy(int32 dc_id, AuthKeyDestroyReason reason, double now) noexcept {
  static_cast<void>(reason);
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters.auth_key_destroy_total++;
  if (!is_tracked_dc_id(dc_id)) {
    return;
  }

  for (int32 other_dc_id = 1; other_dc_id <= DcId::MAX_RAW_DC_ID; other_dc_id++) {
    if (other_dc_id == dc_id) {
      continue;
    }
    if (state.last_destroy_at[other_dc_id] != 0.0 && state.last_destroy_at[other_dc_id] >= now - DESTROY_BURST_WINDOW) {
      state.counters.auth_key_destroy_burst_total++;
      break;
    }
  }

  state.last_destroy_at[dc_id] = now;
  state.reauth_not_before[dc_id] = std::max(state.reauth_not_before[dc_id], now + REAUTH_DELAY);
}

double get_reauth_not_before(int32 dc_id) noexcept {
  if (!is_tracked_dc_id(dc_id)) {
    return 0.0;
  }
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  return state.reauth_not_before[dc_id];
}

NetMonitorSnapshot get_net_monitor_snapshot() noexcept {
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  NetMonitorSnapshot result;
  result.counters = state.counters;
  result.state = resolve_health_state(state.counters);
  return result;
}

void reset_net_monitor_for_tests() noexcept {
  auto &state = storage();
  std::lock_guard<std::mutex> guard(state.mutex);
  state.counters = {};
  state.reauth_not_before.fill(0.0);
  state.last_destroy_at.fill(0.0);
}

}  // namespace net_health
}  // namespace td