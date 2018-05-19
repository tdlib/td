//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/TestsRunner.h"

#include "td/utils/common.h"
#include "td/utils/FileLog.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

#include <limits>

DESC_TESTS(string_cleaning);
DESC_TESTS(message_entities);
DESC_TESTS(variant);
DESC_TESTS(secret);
DESC_TESTS(actors_main);
DESC_TESTS(actors_simple);
DESC_TESTS(actors_workers);
DESC_TESTS(db);
DESC_TESTS(json);
DESC_TESTS(http);
DESC_TESTS(heap);
DESC_TESTS(pq);
DESC_TESTS(mtproto);

namespace td {

void TestsRunner::run_all_tests() {
  LOAD_TESTS(string_cleaning);
  LOAD_TESTS(message_entities);
  LOAD_TESTS(variant);
  LOAD_TESTS(secret);
  LOAD_TESTS(actors_main);
  LOAD_TESTS(actors_simple);
  LOAD_TESTS(actors_workers);
  LOAD_TESTS(db);
  LOAD_TESTS(json);
  LOAD_TESTS(http);
  LOAD_TESTS(heap);
  LOAD_TESTS(pq);
  LOAD_TESTS(mtproto);
  Test::run_all();
}

static FileLog file_log;
static TsLog ts_log(&file_log);

void TestsRunner::init(string dir) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  chdir(dir).ensure();
  LOG(WARNING) << "Redirect log into " << tag("file", dir + TD_DIR_SLASH + "log.txt");
  if (file_log.init("log.txt", std::numeric_limits<int64>::max())) {
    log_interface = &ts_log;
  }
}

}  // namespace td
