//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/tests.h"

static void test_get_url_query_file_name(const char *prefix, const char *suffix, const char *file_name) {
  auto path = td::string(prefix) + td::string(file_name) + td::string(suffix);
  ASSERT_STREQ(file_name, td::get_url_query_file_name(path));
  ASSERT_STREQ(file_name, td::get_url_file_name("http://telegram.org" + path));
  ASSERT_STREQ(file_name, td::get_url_file_name("http://telegram.org:80" + path));
  ASSERT_STREQ(file_name, td::get_url_file_name("telegram.org" + path));
}

TEST(HttpUrl, get_url_query_file_name) {
  for (auto suffix : {"?t=1#test", "#test?t=1", "#?t=1", "?t=1#", "#test", "?t=1", "#", "?", ""}) {
    test_get_url_query_file_name("", suffix, "");
    test_get_url_query_file_name("/", suffix, "");
    test_get_url_query_file_name("/a/adasd/", suffix, "");
    test_get_url_query_file_name("/a/lklrjetn/", suffix, "adasd.asdas");
    test_get_url_query_file_name("/", suffix, "a123asadas");
    test_get_url_query_file_name("/", suffix, "\\a\\1\\2\\3\\a\\s\\a\\das");
  }
}
