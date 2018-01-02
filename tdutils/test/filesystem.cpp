//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/filesystem.h"
#include "td/utils/tests.h"

TEST(Misc, clean_filename) {
  using td::clean_filename;
  ASSERT_STREQ(clean_filename("-1234567"), "-1234567");
  ASSERT_STREQ(clean_filename(".git"), "git");
  ASSERT_STREQ(clean_filename("../../.git"), "git");
  ASSERT_STREQ(clean_filename(".././.."), "");
  ASSERT_STREQ(clean_filename("../"), "");
  ASSERT_STREQ(clean_filename(".."), "");
  ASSERT_STREQ(clean_filename("test/git/   as   dsa  .   a"), "as   dsa.a");
  ASSERT_STREQ(clean_filename("     .    "), "");
  ASSERT_STREQ(clean_filename("!@#$%^&*()_+-=[]{;|:\"}'<>?,.`~"), "!@#$%^  ()_+-=[]{;   }    ,.~");
  ASSERT_STREQ(clean_filename("!@#$%^&*()_+-=[]{}\\|:\";'<>?,.`~"), ";    ,.~");
  ASSERT_STREQ(clean_filename("عرفها بعد قد. هذا مع تاريخ اليميني واندونيسيا،, لعدم تاريخ لهيمنة الى"),
               "عرفها بعد قد.هذا مع تاريخ اليميني");
  ASSERT_STREQ(
      clean_filename(
          "012345678901234567890123456789012345678901234567890123456789adsasdasdsaa.01234567890123456789asdasdasdasd"),
      "012345678901234567890123456789012345678901234567890123456789.01234567890123456789");
  ASSERT_STREQ(clean_filename("01234567890123456789012345678901234567890123456789<>*?: <>*?:0123456789adsasdasdsaa.   "
                              "0123456789`<><<>><><>0123456789asdasdasdasd"),
               "01234567890123456789012345678901234567890123456789.0123456789");
  ASSERT_STREQ(clean_filename("01234567890123456789012345678901234567890123456789<>*?: <>*?:0123456789adsasdasdsaa.   "
                              "0123456789`<><><>0123456789asdasdasdasd"),
               "01234567890123456789012345678901234567890123456789.0123456789       012");
  ASSERT_STREQ(clean_filename("C:/document.tar.gz"), "document.tar.gz");
  ASSERT_STREQ(clean_filename("test...."), "test");
  ASSERT_STREQ(clean_filename("....test"), "test");
  ASSERT_STREQ(clean_filename("test.exe...."), "test.exe");  // extension has changed
  ASSERT_STREQ(clean_filename("test.exe01234567890123456789...."),
               "test.exe01234567890123456789");  // extension may be more then 20 characters
  ASSERT_STREQ(clean_filename("....test....asdf"), "test.asdf");
}
