//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/tests_runner.h"

#include "test/TestsRunner.h"

extern "C" {
void tests_runner_init(const char *dir) {
  td::TestsRunner::init(dir);
}
void run_all_tests() {
  td::TestsRunner::run_all_tests();
}
}
