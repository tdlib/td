// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ProxyRejectionTestHarness.h"
#include "test/stealth/SourceContractFileReader.h"

#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/utils/common.h"
#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cctype>

#if TD_PORT_POSIX

namespace {

struct RetryAttemptState final {
  td::size_t bounded_retry_failures{0};
  td::ConnectionFailureBackoff backoff;
};

struct RetryAttemptOutcome final {
  bool terminal{false};
  double next_wakeup_at{0.0};
  td::Status terminal_error;
};

td::Proxy tls_proxy() {
  td::string raw_secret;
  raw_secret.push_back(static_cast<char>(0xee));
  raw_secret += "0123456789abcdefdomain";
  return td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw(raw_secret));
}

td::size_t connection_creator_retry_cap() {
  auto header = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.h");
  auto marker = td::string("MAX_BOUNDED_RETRY_FAILURES = ");
  auto begin = header.find(marker);
  CHECK(begin != td::string::npos);
  begin += marker.size();

  auto end = begin;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end])) != 0) {
    end++;
  }
  CHECK(end > begin);

  auto cap_text = header.substr(begin, end - begin);
  return static_cast<td::size_t>(td::to_integer<td::int32>(cap_text));
}

RetryAttemptOutcome simulate_connection_creator_sync_failure_attempt(
    RetryAttemptState &state, td::size_t retry_cap, double now,
    const td::ConnectionFailureClassification &classification, const td::Status &failure_status) {
  RetryAttemptOutcome outcome;

  if (classification.apply_exponential_backoff) {
    state.backoff.add_event(static_cast<td::int32>(now));
  }

  if (!classification.bounded_retry) {
    state.bounded_retry_failures = 0;
  } else {
    state.bounded_retry_failures++;
    if (state.bounded_retry_failures >= retry_cap) {
      outcome.terminal = true;
      outcome.terminal_error = td::Status::Error(
          failure_status.code(), PSLICE() << "Connection retry limit reached after " << state.bounded_retry_failures
                                          << " failures; last_error="
                                          << td::sanitize_connection_failure_status_message_for_log(failure_status));
      state.bounded_retry_failures = 0;
      return outcome;
    }
  }

  outcome.next_wakeup_at = now + 0.1;
  if (classification.apply_exponential_backoff) {
    outcome.next_wakeup_at = std::max(outcome.next_wakeup_at, static_cast<double>(state.backoff.get_wakeup_at()));
  }
  return outcome;
}

TEST(ConnectionCreatorRetryCapIntegration,
     PersistentConnectFailuresPropagateTerminalErrorAtCapWithoutEarlyTerminalError) {
  const auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::MalformedTlsResponse);
  ASSERT_TRUE(status.is_error());

  const auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.bounded_retry);
  ASSERT_TRUE(classification.apply_exponential_backoff);

  const auto retry_cap = connection_creator_retry_cap();
  ASSERT_TRUE(retry_cap > 1u);

  RetryAttemptState state;
  double now = 10000.0;

  for (td::size_t attempt = 1; attempt < retry_cap; attempt++) {
    const auto outcome =
        simulate_connection_creator_sync_failure_attempt(state, retry_cap, now, classification, status);

    ASSERT_FALSE(outcome.terminal);
    ASSERT_FALSE(outcome.terminal_error.is_error());
    ASSERT_TRUE(outcome.next_wakeup_at > now);

    now = outcome.next_wakeup_at;
  }

  const auto terminal = simulate_connection_creator_sync_failure_attempt(state, retry_cap, now, classification, status);
  ASSERT_TRUE(terminal.terminal);
  ASSERT_TRUE(terminal.terminal_error.is_error());
  ASSERT_TRUE(terminal.terminal_error.message().str().find("Connection retry limit reached after") != td::string::npos);
  auto retry_cap_text = (PSLICE() << retry_cap).str();
  ASSERT_TRUE(terminal.terminal_error.message().str().find(retry_cap_text) != td::string::npos);
}

TEST(ConnectionCreatorRetryCapIntegration, PersistentConnectFailuresDoNotHotLoopRescheduleBeforeTerminalAttempt) {
  const auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::ImmediateClose);
  ASSERT_TRUE(status.is_error());

  const auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.bounded_retry);
  ASSERT_TRUE(classification.apply_exponential_backoff);

  const auto retry_cap = connection_creator_retry_cap();
  ASSERT_TRUE(retry_cap > 1u);

  RetryAttemptState state;
  double now = 20000.0;
  td::vector<double> wakeups;

  for (td::size_t attempt = 1; attempt <= retry_cap; attempt++) {
    const auto outcome =
        simulate_connection_creator_sync_failure_attempt(state, retry_cap, now, classification, status);

    if (outcome.terminal) {
      ASSERT_EQ(retry_cap, attempt);
      break;
    }

    // Sync failure path should never schedule immediate retries between capped attempts.
    ASSERT_TRUE((outcome.next_wakeup_at - now) >= 1.0);
    wakeups.push_back(outcome.next_wakeup_at);

    now = outcome.next_wakeup_at;
  }

  ASSERT_EQ(retry_cap - 1u, wakeups.size());
  for (td::size_t i = 1; i < wakeups.size(); i++) {
    ASSERT_TRUE(wakeups[i] > wakeups[i - 1]);
  }
}

}  // namespace

#endif  // TD_PORT_POSIX
