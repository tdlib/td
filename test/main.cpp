//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/crypto.h"
#include "td/utils/ExitGuard.h"
#include "td/utils/logging.h"
#include "td/utils/OptionParser.h"
#include "td/utils/port/detail/ThreadIdGuard.h"
#include "td/utils/port/stacktrace.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"

#include "td/mtproto/AuthData.h"

#if TD_EMSCRIPTEN
#include <emscripten.h>
#endif

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#include <windows.h>

namespace {

// On Windows, the MSVC Debug CRT pops up modal "abort() has been called" and
// "_CrtDbgReport" dialogs whenever a test triggers an assertion or `LOG(FATAL)`.
// In CI / non-interactive runs these dialogs hang the test process forever and
// also block automated bug discovery — exactly the opposite of what a TDD red
// test suite is supposed to do.
//
// Suppress every modal failure surface so that test failures fall through to
// the standard `abort()` exit code instead of waiting on user input:
//   * `_set_abort_behavior`        — disable the "abort() has been called" dialog
//   * `_CrtSetReportMode`          — route _CrtDbgReport to stderr, not a dialog
//   * `SetErrorMode`               — disable Windows Error Reporting / WER popups
//   * `_set_error_mode`            — disable CRT error message boxes
//
// Each of these is necessary; removing any single one is enough to bring the
// dialogs back on a modern MSVC build.
void disable_windows_modal_failure_dialogs() {
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#if defined(_DEBUG)
  for (int report_type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
  }
#endif

  _set_error_mode(_OUT_TO_STDERR);
#endif

  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

}  // namespace
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
  disable_windows_modal_failure_dialogs();
#endif
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL));
  td::ExitGuard exit_guard;
  td::detail::ThreadIdGuard thread_id_guard;
  td::Stacktrace::init();
  td::init_openssl_threads();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);

  td::TestsRunner &runner = td::TestsRunner::get_default();

  int default_verbosity_level = 1;
  bool list_tests = false;
  td::OptionParser options;
  options.add_option('e', "exact", "run exactly one test with the specified name",
                     [&](td::Slice exact_name) { runner.set_exact_filter(exact_name.str()); });
  options.add_option('f', "filter", "run only specified tests",
                     [&](td::Slice filter) { runner.add_substr_filter(filter.str()); });
  options.add_option('l', "list", "list selected tests without running them", [&] { list_tests = true; });
  options.add_option('o', "offset", "run tests from the specified test",
                     [&](td::Slice offset) { runner.set_offset(offset.str()); });
  options.add_option('s', "stress", "run tests infinitely", [&] { runner.set_stress_flag(true); });
  options.add_checked_option('v', "verbosity", "log verbosity level",
                             td::OptionParser::parse_integer(default_verbosity_level));
  options.add_check([&] {
    if (default_verbosity_level < 0) {
      return td::Status::Error("Wrong verbosity level specified");
    }
    return td::Status::OK();
  });
  auto r_non_options = options.run(argc, argv, 0);
  if (r_non_options.is_error()) {
    LOG(PLAIN) << argv[0] << ": " << r_non_options.error().message();
    LOG(PLAIN) << options;
    return 1;
  }
  SET_VERBOSITY_LEVEL(default_verbosity_level);

  if (list_tests) {
    runner.list_tests();
    return 0;
  }

#if TD_EMSCRIPTEN
  emscripten_set_main_loop(
      [] {
        td::TestsRunner &default_runner = td::TestsRunner::get_default();
        if (!default_runner.run_all_step()) {
          emscripten_cancel_main_loop();
        }
      },
      10, 0);
#else
  runner.run_all();
#endif
}
