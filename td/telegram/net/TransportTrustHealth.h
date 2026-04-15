//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"

#include "td/utils/common.h"

namespace td {
namespace net_health {

enum class NetMonitorState : int8 { Healthy, Degraded, Suspicious };

enum class AuthKeyDestroyReason : int8 { UserLogout, ServerRevoke, SessionKeyCorruption, ProgrammaticApiCall };

struct NetMonitorCounters final {
  uint64 session_param_coerce_attempt_total{0};
  uint64 bind_encrypted_message_invalid_total{0};
  uint64 bind_retry_budget_exhausted_total{0};
  uint64 main_key_set_cardinality_failure_total{0};
  uint64 low_server_fingerprint_count_total{0};
  uint64 auth_key_destroy_total{0};
  uint64 auth_key_destroy_burst_total{0};
};

struct NetMonitorSnapshot final {
  NetMonitorCounters counters;
  NetMonitorState state{NetMonitorState::Healthy};
};

void note_session_param_coerce_attempt() noexcept;
void note_bind_encrypted_message_invalid(int32 dc_id, bool has_immunity) noexcept;
void note_bind_retry_budget_exhausted(int32 dc_id) noexcept;
void note_main_key_set_cardinality_failure(bool is_test, size_t observed_count, size_t expected_count) noexcept;
void note_low_server_fingerprint_count(size_t observed_count) noexcept;
void note_auth_key_destroy(int32 dc_id, AuthKeyDestroyReason reason, double now) noexcept;

double get_reauth_not_before(int32 dc_id) noexcept;
NetMonitorSnapshot get_net_monitor_snapshot() noexcept;
void reset_net_monitor_for_tests() noexcept;

}  // namespace net_health
}  // namespace td