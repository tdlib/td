//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/utils/logging.h"

#include <cstring>

#if TD_EMSCRIPTEN
#include <emscripten.h>
#endif

int main(int argc, char **argv) {
  // TODO port OptionsParser to Windows
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--filter")) {
      CHECK(i + 1 < argc);
      td::Test::add_substr_filter(argv[++i]);
    }
    if (!std::strcmp(argv[i], "--stress")) {
      td::Test::set_stress_flag(true);
    }
  }
#if TD_EMSCRIPTEN
  emscripten_set_main_loop(
      [] {
        if (!td::Test::run_all_step()) {
          emscripten_cancel_main_loop();
        }
      },
      10, 0);
#else
  td::Test::run_all();
#endif
  return 0;
}
