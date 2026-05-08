// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_macro_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>

namespace {

using td::logging_macro::test::CountingLog;
using td::logging_macro::test::ScopedLoggingOverride;

std::atomic<int> VERBOSITY_NAME(macro_integration_tag){VERBOSITY_NAME(INFO)};

TEST(LoggingMacroIntegration, AtomicCustomTagAboveRuntimeGateStaysSuppressed) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(WARNING));

  VERBOSITY_NAME(macro_integration_tag).store(VERBOSITY_NAME(INFO), std::memory_order_release);
  for (int i = 0; i < 256; i++) {
    VLOG(macro_integration_tag) << "logging-macro-suppressed";
  }

  ASSERT_EQ(0, sink.writes.load(std::memory_order_relaxed));
}

TEST(LoggingMacroIntegration, AtomicCustomTagAtRuntimeGateEmits) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(INFO));

  VERBOSITY_NAME(macro_integration_tag).store(VERBOSITY_NAME(INFO), std::memory_order_release);
  VLOG(macro_integration_tag) << "logging-macro-emits";

  ASSERT_EQ(1, sink.writes.load(std::memory_order_relaxed));
}

TEST(LoggingMacroIntegration, RuntimeTagUpdatesTakeEffectWithoutRebuild) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(DEBUG));

  VERBOSITY_NAME(macro_integration_tag).store(VERBOSITY_NAME(NEVER), std::memory_order_release);
  VLOG(macro_integration_tag) << "logging-macro-update-suppressed";
  ASSERT_EQ(0, sink.writes.load(std::memory_order_relaxed));

  VERBOSITY_NAME(macro_integration_tag).store(VERBOSITY_NAME(ERROR), std::memory_order_release);
  VLOG(macro_integration_tag) << "logging-macro-update-emits";
  ASSERT_EQ(1, sink.writes.load(std::memory_order_relaxed));
}

}  // namespace
