// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/Client.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>

namespace {

class DiscardLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
  }
};

class ScopedTdJsonLogCallback final {
 public:
  ScopedTdJsonLogCallback(int max_verbosity_level, td::ClientManager::LogMessageCallbackPtr callback) {
    td::ClientManager::set_log_message_callback(max_verbosity_level, callback);
  }

  ~ScopedTdJsonLogCallback() {
    td::ClientManager::set_log_message_callback(VERBOSITY_NAME(FATAL), nullptr);
  }
};

std::atomic<int> g_integration_calls{0};
td::string g_last_message;
std::atomic<int> g_recursive_callback_calls{0};
std::atomic<bool> g_emit_recursive_log{false};

void integration_log_callback(int verbosity_level, const char *message) {
  (void)verbosity_level;
  g_last_message = message == nullptr ? td::string() : td::string(message);
  g_integration_calls.fetch_add(1, std::memory_order_relaxed);
}

void recursive_integration_log_callback(int verbosity_level, const char *message) {
  (void)verbosity_level;
  (void)message;

  const auto call_index = g_recursive_callback_calls.fetch_add(1, std::memory_order_relaxed);
  if (call_index == 0 && g_emit_recursive_log.load(std::memory_order_relaxed)) {
    DiscardLog sink;
    td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << "nested-callback-log";
  }
}

TEST(LoggingMessageCallbackIntegration, TdJsonCallbackReceivesUtf8MessagesThroughClientManagerWrapper) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_integration_calls.store(0, std::memory_order_relaxed);
  g_last_message.clear();

  {
    ScopedTdJsonLogCallback guard(VERBOSITY_NAME(INFO), &integration_log_callback);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << "integration-utf8-message";
  }

  ASSERT_EQ(1, g_integration_calls.load(std::memory_order_relaxed));
  ASSERT_TRUE(g_last_message.find("integration-utf8-message") != td::string::npos);
}

TEST(LoggingMessageCallbackIntegration, TdJsonCallbackPercentEncodesBinaryTailFailClosed) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_integration_calls.store(0, std::memory_order_relaxed);
  g_last_message.clear();

  td::string payload("binary-prefix");
  payload.push_back(static_cast<char>(0xFF));
  payload += "tail";

  {
    ScopedTdJsonLogCallback guard(VERBOSITY_NAME(INFO), &integration_log_callback);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, g_integration_calls.load(std::memory_order_relaxed));
  ASSERT_TRUE(g_last_message.find("binary-prefix") == 0);
  ASSERT_TRUE(g_last_message.find("%FFtail") != td::string::npos || g_last_message.find("%fftail") != td::string::npos);
}

TEST(LoggingMessageCallbackIntegration, TdJsonCallbackPercentEncodesSingleInvalidTrailingByteFailClosed) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_integration_calls.store(0, std::memory_order_relaxed);
  g_last_message.clear();

  td::string payload("binary-prefix");
  payload.push_back(static_cast<char>(0xFF));

  {
    ScopedTdJsonLogCallback guard(VERBOSITY_NAME(INFO), &integration_log_callback);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, g_integration_calls.load(std::memory_order_relaxed));
  ASSERT_TRUE(g_last_message == "binary-prefix%FF" || g_last_message == "binary-prefix%ff");
}

TEST(LoggingMessageCallbackIntegration, TdJsonCallbackRejectsRecursiveReentryFailClosed) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  g_recursive_callback_calls.store(0, std::memory_order_relaxed);
  g_emit_recursive_log.store(true, std::memory_order_relaxed);

  {
    ScopedTdJsonLogCallback guard(VERBOSITY_NAME(INFO), &recursive_integration_log_callback);
    td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
    logger << "outer-callback-log";
  }

  g_emit_recursive_log.store(false, std::memory_order_relaxed);
  ASSERT_EQ(1, g_recursive_callback_calls.load(std::memory_order_relaxed));
}

}  // namespace