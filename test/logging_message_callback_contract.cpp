// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

class DiscardLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
  }
};

class ScopedLogMessageCallback final {
 public:
  ScopedLogMessageCallback(int max_verbosity_level, td::OnLogMessageCallback callback) {
    td::set_log_message_callback(max_verbosity_level, callback);
  }

  ~ScopedLogMessageCallback() {
    td::set_log_message_callback(VERBOSITY_NAME(FATAL), nullptr);
  }
};

std::atomic<int> g_contract_calls{0};
std::atomic<int> g_contract_last_level{VERBOSITY_NAME(NEVER)};

void contract_log_callback(int verbosity_level, td::CSlice message) {
  (void)message;
  g_contract_last_level.store(verbosity_level, std::memory_order_relaxed);
  g_contract_calls.fetch_add(1, std::memory_order_relaxed);
}

TEST(LoggingMessageCallbackContract, SourcePinsSingleAtomicSnapshotStateForCallbackConfiguration) {
  auto source = load_repo_text("tdutils/td/utils/logging.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("structLogMessageCallbackState") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::atomic<std::shared_ptr<constLogMessageCallbackState>>log_message_callback_state") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("log_message_callback_state.load(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("log_message_callback_state.store(std::make_shared<constLogMessageCallbackState>") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("std::atomic<int>max_callback_verbosity_level") == td::string::npos);
  ASSERT_TRUE(normalized.find("std::atomic<OnLogMessageCallback>on_log_message_callback") == td::string::npos);
}

TEST(LoggingMessageCallbackContract, NullCallbackDisablesRuntimeDeliveryFailClosed) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_contract_calls.store(0, std::memory_order_relaxed);
  g_contract_last_level.store(VERBOSITY_NAME(NEVER), std::memory_order_relaxed);

  {
    ScopedLogMessageCallback guard(VERBOSITY_NAME(INFO), nullptr);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << "contract-null-callback";
  }

  ASSERT_EQ(0, g_contract_calls.load(std::memory_order_relaxed));
  ASSERT_EQ(VERBOSITY_NAME(NEVER), g_contract_last_level.load(std::memory_order_relaxed));
}

TEST(LoggingMessageCallbackContract, EligibleVerbosityInvokesConfiguredCallbackExactlyOnce) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_contract_calls.store(0, std::memory_order_relaxed);
  g_contract_last_level.store(VERBOSITY_NAME(NEVER), std::memory_order_relaxed);

  {
    ScopedLogMessageCallback guard(VERBOSITY_NAME(INFO), &contract_log_callback);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << "contract-runtime-callback";
  }

  ASSERT_EQ(1, g_contract_calls.load(std::memory_order_relaxed));
  ASSERT_EQ(VERBOSITY_NAME(ERROR), g_contract_last_level.load(std::memory_order_relaxed));
}

}  // namespace