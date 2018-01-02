//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void tests_runner_init(const char *dir);
void run_all_tests();

#ifdef __cplusplus
}
#endif
