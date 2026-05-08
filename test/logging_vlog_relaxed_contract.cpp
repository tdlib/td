// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

class CountingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
    writes.fetch_add(1, std::memory_order_relaxed);
  }

  std::atomic<int> writes{0};
};

class ScopedLoggingOverride final {
 public:
  ScopedLoggingOverride(td::LogInterface *sink, int verbosity_level)
      : old_sink_(td::load_active_log_interface()), old_level_(SET_VERBOSITY_LEVEL(verbosity_level)) {
    td::store_active_log_interface(sink);
  }

  ~ScopedLoggingOverride() {
    td::store_active_log_interface(old_sink_);
    SET_VERBOSITY_LEVEL(old_level_);
  }

 private:
  td::LogInterface *old_sink_;
  int old_level_;
};

std::atomic<int> VERBOSITY_NAME(phase6_vlog_tag){VERBOSITY_NAME(INFO)};

TEST(LoggingVlogRelaxedContract, LoggingHeaderPinsRelaxedAtomicLoadForRuntimeLevelGate) {
  auto source = load_repo_text("tdutils/td/utils/logging.h");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("constexprintload_verbosity_level(intlevel)noexcept{returnlevel;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("inlineintload_verbosity_level(conststd::atomic<int>&level)noexcept{returnlevel.load("
                              "std::memory_order_relaxed);}") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG_IS_STRIPPED(strip_level)||::td::load_verbosity_level(runtime_level)>"
                              "options.get_level()||!(condition)") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG_IS_STRIPPED(strip_level)||runtime_level>options.get_level()||!(condition)") ==
              td::string::npos);
}

TEST(LoggingVlogRelaxedContract, AtomicRuntimeTagAboveGateDoesNotEmit) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(WARNING));

  VERBOSITY_NAME(phase6_vlog_tag).store(VERBOSITY_NAME(INFO), std::memory_order_release);

  for (int i = 0; i < 512; i++) {
    VLOG(phase6_vlog_tag) << "phase6-vlog-relaxed-suppressed";
  }

  ASSERT_EQ(0, sink.writes.load(std::memory_order_relaxed));
}

TEST(LoggingVlogRelaxedContract, AtomicRuntimeTagBelowGateEmits) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(DEBUG));

  VERBOSITY_NAME(phase6_vlog_tag).store(VERBOSITY_NAME(INFO), std::memory_order_release);
  VLOG(phase6_vlog_tag) << "phase6-vlog-relaxed-emits";

  ASSERT_EQ(1, sink.writes.load(std::memory_order_relaxed));
}

}  // namespace
