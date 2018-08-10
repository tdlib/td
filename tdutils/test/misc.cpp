//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/base64.h"
#include "td/utils/BigNum.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/EventFd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/path.h"
#include "td/utils/port/sleep.h"
#include "td/utils/port/Stat.h"
#include "td/utils/port/thread.h"
#include "td/utils/port/wstring_convert.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"
#include "td/utils/translit.h"
#include "td/utils/unicode.h"
#include "td/utils/utf8.h"

#include <atomic>
#include <clocale>
#include <limits>
#include <locale>

using namespace td;

#if TD_LINUX || TD_DARWIN
TEST(Misc, update_atime_saves_mtime) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  std::string name = "test_file";
  unlink(name).ignore();
  auto r_file = FileFd::open(name, FileFd::Read | FileFd::Flags::Create | FileFd::Flags::Truncate);
  LOG_IF(ERROR, r_file.is_error()) << r_file.error();
  ASSERT_TRUE(r_file.is_ok());
  r_file.move_as_ok().close();

  auto info = stat(name).ok();
  int32 tests_ok = 0;
  int32 tests_wa = 0;
  for (int i = 0; i < 10000; i++) {
    update_atime(name).ensure();
    auto new_info = stat(name).ok();
    if (info.mtime_nsec_ == new_info.mtime_nsec_) {
      tests_ok++;
    } else {
      tests_wa++;
      info.mtime_nsec_ = new_info.mtime_nsec_;
    }
    ASSERT_EQ(info.mtime_nsec_, new_info.mtime_nsec_);
    usleep_for(Random::fast(0, 1000));
  }
  if (tests_wa > 0) {
    LOG(ERROR) << "Access time was unexpectedly updated " << tests_wa << " times";
  }
  unlink(name).ensure();
}

TEST(Misc, update_atime_change_atime) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  std::string name = "test_file";
  unlink(name).ignore();
  auto r_file = FileFd::open(name, FileFd::Read | FileFd::Flags::Create | FileFd::Flags::Truncate);
  LOG_IF(ERROR, r_file.is_error()) << r_file.error();
  ASSERT_TRUE(r_file.is_ok());
  r_file.move_as_ok().close();
  auto info = stat(name).ok();
  // not enough for fat and e.t.c.
  usleep_for(5000000);
  update_atime(name).ensure();
  auto new_info = stat(name).ok();
  if (info.atime_nsec_ == new_info.atime_nsec_) {
    LOG(ERROR) << "Access time was unexpectedly not changed";
  }
  unlink(name).ensure();
}
#endif

TEST(Misc, errno_tls_bug) {
  // That's a problem that should be avoided
  // errno = 0;
  // impl_.alloc(123);
  // CHECK(errno == 0);

#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  EventFd test_event_fd;
  test_event_fd.init();
  std::atomic<int> s(0);
  s = 1;
  td::thread th([&] {
    while (s != 1) {
    }
    test_event_fd.acquire();
  });
  th.join();

  for (int i = 0; i < 1000; i++) {
    vector<EventFd> events(10);
    vector<td::thread> threads;
    for (auto &event : events) {
      event.init();
      event.release();
    }
    for (auto &event : events) {
      threads.push_back(td::thread([&] {
        {
          EventFd tmp;
          tmp.init();
          tmp.acquire();
        }
        event.acquire();
      }));
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
#endif
}

TEST(Misc, base64) {
  ASSERT_TRUE(is_base64("dGVzdA==") == true);
  ASSERT_TRUE(is_base64("dGVzdB==") == false);
  ASSERT_TRUE(is_base64("dGVzdA=") == false);
  ASSERT_TRUE(is_base64("dGVzdA") == false);
  ASSERT_TRUE(is_base64("dGVz") == true);
  ASSERT_TRUE(is_base64("") == true);
  ASSERT_TRUE(is_base64("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") == true);
  ASSERT_TRUE(is_base64("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") == false);
  ASSERT_TRUE(is_base64("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-/") == false);
  ASSERT_TRUE(is_base64("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == false);
  ASSERT_TRUE(is_base64("====") == false);

  ASSERT_TRUE(is_base64url("dGVzdA==") == true);
  ASSERT_TRUE(is_base64url("dGVzdB==") == false);
  ASSERT_TRUE(is_base64url("dGVzdA=") == false);
  ASSERT_TRUE(is_base64url("dGVzdA") == true);
  ASSERT_TRUE(is_base64url("dGVz") == true);
  ASSERT_TRUE(is_base64url("") == true);
  ASSERT_TRUE(is_base64url("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == true);
  ASSERT_TRUE(is_base64url("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=") == false);
  ASSERT_TRUE(is_base64url("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-/") == false);
  ASSERT_TRUE(is_base64url("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") == false);
  ASSERT_TRUE(is_base64url("====") == false);

  for (int l = 0; l < 300000; l += l / 20 + l / 1000 * 500 + 1) {
    for (int t = 0; t < 10; t++) {
      string s = rand_string(std::numeric_limits<char>::min(), std::numeric_limits<char>::max(), l);
      string encoded = base64url_encode(s);
      auto decoded = base64url_decode(encoded);
      ASSERT_TRUE(decoded.is_ok());
      ASSERT_TRUE(decoded.ok() == s);

      encoded = base64_encode(s);
      decoded = base64_decode(encoded);
      ASSERT_TRUE(decoded.is_ok());
      ASSERT_TRUE(decoded.ok() == s);
    }
  }

  ASSERT_TRUE(base64url_decode("dGVzdA").is_ok());
  ASSERT_TRUE(base64url_decode("dGVzdB").is_error());
  ASSERT_TRUE(base64_encode(base64url_decode("dGVzdA").ok()) == "dGVzdA==");
  ASSERT_TRUE(base64_encode("any carnal pleas") == "YW55IGNhcm5hbCBwbGVhcw==");
  ASSERT_TRUE(base64_encode("any carnal pleasu") == "YW55IGNhcm5hbCBwbGVhc3U=");
  ASSERT_TRUE(base64_encode("any carnal pleasur") == "YW55IGNhcm5hbCBwbGVhc3Vy");
  ASSERT_TRUE(base64_encode("      /'.;.';≤.];,].',[.;/,.;/]/..;!@#!*(%?::;!%\";") ==
              "ICAgICAgLycuOy4nO+KJpC5dOyxdLicsWy47LywuOy9dLy4uOyFAIyEqKCU/"
              "Ojo7ISUiOw==");
}

TEST(Misc, to_integer) {
  ASSERT_EQ(to_integer<int32>("-1234567"), -1234567);
  ASSERT_EQ(to_integer<int64>("-1234567"), -1234567);
  ASSERT_EQ(to_integer<uint32>("-1234567"), 0u);
  ASSERT_EQ(to_integer<int16>("-1234567"), 10617);
  ASSERT_EQ(to_integer<uint16>("-1234567"), 0u);
  ASSERT_EQ(to_integer<int16>("-1254567"), -9383);
  ASSERT_EQ(to_integer<uint16>("1254567"), 9383u);
  ASSERT_EQ(to_integer<int64>("-12345678910111213"), -12345678910111213);
  ASSERT_EQ(to_integer<uint64>("12345678910111213"), 12345678910111213ull);

  ASSERT_EQ(to_integer_safe<int32>("-1234567").ok(), -1234567);
  ASSERT_EQ(to_integer_safe<int64>("-1234567").ok(), -1234567);
  ASSERT_TRUE(to_integer_safe<uint32>("-1234567").is_error());
  ASSERT_TRUE(to_integer_safe<int16>("-1234567").is_error());
  ASSERT_TRUE(to_integer_safe<uint16>("-1234567").is_error());
  ASSERT_TRUE(to_integer_safe<int16>("-1254567").is_error());
  ASSERT_TRUE(to_integer_safe<uint16>("1254567").is_error());
  ASSERT_EQ(to_integer_safe<int64>("-12345678910111213").ok(), -12345678910111213);
  ASSERT_EQ(to_integer_safe<uint64>("12345678910111213").ok(), 12345678910111213ull);
  ASSERT_TRUE(to_integer_safe<uint64>("-12345678910111213").is_error());
}

static void test_to_double_one(CSlice str, Slice expected, int precision = 6) {
  auto result = PSTRING() << td::StringBuilder::FixedDouble(to_double(str), precision);
  if (expected != result) {
    LOG(ERROR) << "To double conversion failed: have " << str << ", expected " << expected << ", parsed "
               << to_double(str) << ", got " << result;
  }
}

static void test_to_double() {
  test_to_double_one("0", "0.000000");
  test_to_double_one("1", "1.000000");
  test_to_double_one("-10", "-10.000000");
  test_to_double_one("1.234", "1.234000");
  test_to_double_one("-1.234e2", "-123.400000");
  test_to_double_one("inf", "inf");
  test_to_double_one("  inF  asdasd", "inf");
  test_to_double_one("  inFasdasd", "0.000000");
  test_to_double_one("  NaN", "nan");
  test_to_double_one("  12345678910111213141516171819  asdasd", "12345678910111213670658736128.000000");
  test_to_double_one("1.234567891011121314E123",
                     "1234567891011121363209105003376291141757777526749278953577304234065881343284952489418916814035346"
                     "625663604561924259911303168.000000");
  test_to_double_one("1.234567891011121314E-9", "0.000000");
  test_to_double_one("123456789", "123456789.000000");
  test_to_double_one("-1,234567891011121314E123", "-1.000000");
  test_to_double_one("123456789", "123456789", 0);
  test_to_double_one("1.23456789", "1", 0);
  test_to_double_one("1.23456789", "1.2", 1);
  test_to_double_one("1.23456789", "1.23", 2);
  test_to_double_one("1.23456789", "1.235", 3);
  test_to_double_one("1.23456789", "1.2346", 4);
  test_to_double_one("1.23456789", "1.23457", 5);
  test_to_double_one("1.23456789", "1.234568", 6);
  test_to_double_one("1.23456789", "1.2345679", 7);
  test_to_double_one("1.23456789", "1.23456789", 8);
  test_to_double_one("1.23456789", "1.234567890", 9);
  test_to_double_one("1.23456789", "1.2345678900", 10);
}

TEST(Misc, to_double) {
  test_to_double();
  const char *locale_name = (std::setlocale(LC_ALL, "fr-FR") == nullptr ? "" : "fr-FR");
  std::locale new_locale(locale_name);
  std::locale::global(new_locale);
  test_to_double();
  std::locale::global(std::locale::classic());
  test_to_double();
}

TEST(Misc, print_int) {
  ASSERT_STREQ("-9223372036854775808", PSLICE() << -9223372036854775807 - 1);
  ASSERT_STREQ("-2147483649", PSLICE() << -2147483649ll);
  ASSERT_STREQ("-2147483648", PSLICE() << -2147483647 - 1);
  ASSERT_STREQ("-2147483647", PSLICE() << -2147483647);
  ASSERT_STREQ("-123456789", PSLICE() << -123456789);
  ASSERT_STREQ("-1", PSLICE() << -1);
  ASSERT_STREQ("0", PSLICE() << 0);
  ASSERT_STREQ("1", PSLICE() << 1);
  ASSERT_STREQ("9", PSLICE() << 9);
  ASSERT_STREQ("10", PSLICE() << 10);
  ASSERT_STREQ("2147483647", PSLICE() << 2147483647);
  ASSERT_STREQ("2147483648", PSLICE() << 2147483648ll);
  ASSERT_STREQ("2147483649", PSLICE() << 2147483649ll);
  ASSERT_STREQ("9223372036854775807", PSLICE() << 9223372036854775807ll);
}

TEST(Misc, print_uint) {
  ASSERT_STREQ("0", PSLICE() << 0u);
  ASSERT_STREQ("1", PSLICE() << 1u);
  ASSERT_STREQ("9", PSLICE() << 9u);
  ASSERT_STREQ("10", PSLICE() << 10u);
  ASSERT_STREQ("2147483647", PSLICE() << 2147483647u);
  ASSERT_STREQ("2147483648", PSLICE() << 2147483648u);
  ASSERT_STREQ("2147483649", PSLICE() << 2147483649u);
  ASSERT_STREQ("9223372036854775807", PSLICE() << 9223372036854775807u);
}

static void test_get_url_query_file_name_one(const char *prefix, const char *suffix, const char *file_name) {
  auto path = string(prefix) + string(file_name) + string(suffix);
  ASSERT_STREQ(file_name, get_url_query_file_name(path));
  ASSERT_STREQ(file_name, get_url_file_name("http://telegram.org" + path));
  ASSERT_STREQ(file_name, get_url_file_name("http://telegram.org:80" + path));
  ASSERT_STREQ(file_name, get_url_file_name("telegram.org" + path));
}

TEST(Misc, get_url_query_file_name) {
  for (auto suffix : {"?t=1#test", "#test?t=1", "#?t=1", "?t=1#", "#test", "?t=1", "#", "?", ""}) {
    test_get_url_query_file_name_one("", suffix, "");
    test_get_url_query_file_name_one("/", suffix, "");
    test_get_url_query_file_name_one("/a/adasd/", suffix, "");
    test_get_url_query_file_name_one("/a/lklrjetn/", suffix, "adasd.asdas");
    test_get_url_query_file_name_one("/", suffix, "a123asadas");
    test_get_url_query_file_name_one("/", suffix, "\\a\\1\\2\\3\\a\\s\\a\\das");
  }
}

static void test_idn_to_ascii_one(string host, string result) {
  if (result != idn_to_ascii(host).ok()) {
    LOG(ERROR) << "Failed to convert " << host << " to " << result << ", got \"" << idn_to_ascii(host).ok() << "\"";
  }
}

TEST(Misc, idn_to_ascii) {
  test_idn_to_ascii_one("::::::::::::::::::::::::::::::::::::::@/", "::::::::::::::::::::::::::::::::::::::@/");
  test_idn_to_ascii_one("", "");
  test_idn_to_ascii_one("%30", "%30");
  test_idn_to_ascii_one("127.0.0.1", "127.0.0.1");
  test_idn_to_ascii_one("fe80::", "fe80::");
  test_idn_to_ascii_one("fe80:0:0:0:200:f8ff:fe21:67cf", "fe80:0:0:0:200:f8ff:fe21:67cf");
  test_idn_to_ascii_one("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d", "2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d");
  test_idn_to_ascii_one("::ffff:192.0.2.1", "::ffff:192.0.2.1");
  test_idn_to_ascii_one("ABCDEF", "abcdef");
  test_idn_to_ascii_one("abcdef", "abcdef");
  test_idn_to_ascii_one("abæcdöef", "xn--abcdef-qua4k");
  test_idn_to_ascii_one("schön", "xn--schn-7qa");
  test_idn_to_ascii_one("ยจฆฟคฏข", "xn--22cdfh1b8fsa");
  test_idn_to_ascii_one("☺", "xn--74h");
  test_idn_to_ascii_one("правда", "xn--80aafi6cg");
  test_idn_to_ascii_one("büücher", "xn--bcher-kvaa");
  test_idn_to_ascii_one("BüüCHER", "xn--bcher-kvaa");
  test_idn_to_ascii_one("bücüher", "xn--bcher-kvab");
  test_idn_to_ascii_one("bücherü", "xn--bcher-kvae");
  test_idn_to_ascii_one("ýbücher", "xn--bcher-kvaf");
  test_idn_to_ascii_one("übücher", "xn--bcher-jvab");
  test_idn_to_ascii_one("bücher.tld", "xn--bcher-kva.tld");
  test_idn_to_ascii_one("кто.рф", "xn--j1ail.xn--p1ai");
  test_idn_to_ascii_one("wіkіреdіа.org", "xn--wkd-8cdx9d7hbd.org");
  test_idn_to_ascii_one("cnwin2k8中国.avol.com", "xn--cnwin2k8-sd0mx14e.avol.com");
  test_idn_to_ascii_one("win-2k12r2-addc.阿伯测阿伯测ad.hai.com", "win-2k12r2-addc.xn--ad-tl3ca3569aba8944eca.hai.com");
  test_idn_to_ascii_one("✌.ws", "xn--7bi.ws");
  //  test_idn_to_ascii_one("✌️.ws", "xn--7bi.ws"); // needs nameprep to succeed
  test_idn_to_ascii_one("⛧", "xn--59h");
  test_idn_to_ascii_one("--рф.рф", "xn-----mmcq.xn--p1ai");
  ASSERT_TRUE(idn_to_ascii("\xc0").is_error());
}

#if TD_WINDOWS
static void test_to_wstring_one(string str) {
  ASSERT_STREQ(str, from_wstring(to_wstring(str).ok()).ok());
}

TEST(Misc, to_wstring) {
  test_to_wstring_one("");
  for (int i = 0; i < 10; i++) {
    test_to_wstring_one("test");
    test_to_wstring_one("тест");
  }
  string str;
  for (uint32 i = 0; i <= 0xD7FF; i++) {
    append_utf8_character(str, i);
  }
  for (uint32 i = 0xE000; i <= 0x10FFFF; i++) {
    append_utf8_character(str, i);
  }
  test_to_wstring_one(str);
  ASSERT_TRUE(to_wstring("\xc0").is_error());
  auto emoji = to_wstring("🏟").ok();
  ASSERT_TRUE(from_wstring(emoji).ok() == "🏟");
  ASSERT_TRUE(emoji.size() == 2);
  auto emoji2 = emoji;
  emoji[0] = emoji[1];
  emoji2[1] = emoji2[0];
  ASSERT_TRUE(from_wstring(emoji).is_error());
  ASSERT_TRUE(from_wstring(emoji2).is_error());
  emoji2[0] = emoji[0];
  ASSERT_TRUE(from_wstring(emoji2).is_error());
}
#endif

static void test_translit(string word, vector<string> result, bool allow_partial = true) {
  ASSERT_EQ(result, get_word_transliterations(word, allow_partial));
}

TEST(Misc, translit) {
  test_translit("word", {"word", "ворд"});
  test_translit("", {});
  test_translit("ььььььььь", {"ььььььььь"});
  test_translit("крыло", {"krylo", "крыло"});
  test_translit("krylo", {"krylo", "крило"});
  test_translit("crylo", {"crylo", "крило"});
  test_translit("cheiia", {"cheiia", "кхеииа", "чейия"});
  test_translit("cheii", {"cheii", "кхеии", "чейи", "чейий", "чейия"});
  test_translit("s", {"s", "с", "ш", "щ"});
  test_translit("y", {"e", "y", "е", "и", "ю", "я"});
  test_translit("j", {"e", "j", "е", "й", "ю", "я"});
  test_translit("yo", {"e", "yo", "е", "ио"});
  test_translit("artjom", {"artem", "artjom", "артем", "артйом"});
  test_translit("artyom", {"artem", "artyom", "артем", "артиом"});
  test_translit("arty", {"arte", "arty", "арте", "арти", "артю", "артя"});
  test_translit("льи", {"li", "lia", "ly", "льи"});
  test_translit("y", {"y", "и"}, false);
  test_translit("yo", {"e", "yo", "е", "ио"}, false);
}

static void test_unicode(uint32 (*func)(uint32)) {
  for (uint32 i = 0; i <= 0x110000; i++) {
    auto res = func(i);
    CHECK(res <= 0x10ffff);
  }
}

TEST(Misc, unicode) {
  test_unicode(prepare_search_character);
  test_unicode(unicode_to_lower);
  test_unicode(remove_diacritics);
}

TEST(BigNum, from_decimal) {
  ASSERT_TRUE(BigNum::from_decimal("").is_error());
  ASSERT_TRUE(BigNum::from_decimal("a").is_error());
  ASSERT_TRUE(BigNum::from_decimal("123a").is_error());
  ASSERT_TRUE(BigNum::from_decimal("-123a").is_error());
  // ASSERT_TRUE(BigNum::from_decimal("-").is_error());
  ASSERT_TRUE(BigNum::from_decimal("123").is_ok());
  ASSERT_TRUE(BigNum::from_decimal("-123").is_ok());
  ASSERT_TRUE(BigNum::from_decimal("0").is_ok());
  ASSERT_TRUE(BigNum::from_decimal("-0").is_ok());
  ASSERT_TRUE(BigNum::from_decimal("-999999999999999999999999999999999999999999999999").is_ok());
  ASSERT_TRUE(BigNum::from_decimal("999999999999999999999999999999999999999999999999").is_ok());
}
