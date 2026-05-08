// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingMessageCallbackAdversarial, SourceRejectsSplitAtomicCallbackConfigurationState) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/logging.cpp"));

  ASSERT_TRUE(normalized.find("std::atomic<int>max_callback_verbosity_level") == td::string::npos);
  ASSERT_TRUE(normalized.find("std::atomic<OnLogMessageCallback>on_log_message_callback") == td::string::npos);
  ASSERT_TRUE(normalized.find("log_message_callback_state.load(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("autocallback_state=load_log_message_callback_state();") != td::string::npos);
}

TEST(LoggingMessageCallbackAdversarial, FatalPathMustUseSameSnapshotStateInsteadOfSplitLoads) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/logging.cpp"));

  ASSERT_TRUE(
      normalized.find("process_fatal_error(CSlicemessage){autocallback_state=load_log_message_callback_state();") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("max_callback_verbosity_level.load") == td::string::npos);
  ASSERT_TRUE(normalized.find("on_log_message_callback.load") == td::string::npos);
}

TEST(LoggingMessageCallbackAdversarial, ClientWrapperMustNotTrimBinaryTailWhenNoNewlineIsPresent) {
  auto normalized = normalize_for_contract(load_repo_text("td/telegram/Client.cpp"));

  ASSERT_TRUE(normalized.find("message.substr(pos,message.size()-pos-1)") == td::string::npos);
  ASSERT_TRUE(normalized.find("constboolhas_newline=!message.empty()&&message.back()=='\\n';") != td::string::npos);
  ASSERT_TRUE(normalized.find("constautotail_size=message.size()-pos-static_cast<size_t>(has_newline);") !=
              td::string::npos);
}

}  // namespace