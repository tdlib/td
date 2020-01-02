//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageEntity.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"
#include "td/utils/utf8.h"

#include <algorithm>

REGISTER_TESTS(message_entities);

static void check_mention(const td::string &str, const td::vector<td::string> &expected) {
  auto result_slice = td::find_mentions(str);
  td::vector<td::string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result))
               << td::tag("expected", td::format::as_array(expected));
  }
}

TEST(MessageEntities, mention) {
  check_mention("@mention", {"@mention"});
  check_mention("@mention ", {"@mention"});
  check_mention(" @mention", {"@mention"});
  check_mention(" @mention ", {"@mention"});
  check_mention("@abc @xyz @abc @xyz @xxx@yyy @ttt", {});
  check_mention("@abcde @xyzxy @abcde @xyzxy @xxxxx@yyyyy @ttttt",
                {"@abcde", "@xyzxy", "@abcde", "@xyzxy", "@xxxxx", "@ttttt"});
  check_mention("no@mention", {});
  check_mention("@n", {});
  check_mention("@abcdefghijklmnopqrstuvwxyz123456", {"@abcdefghijklmnopqrstuvwxyz123456"});
  check_mention("@abcdefghijklmnopqrstuvwxyz1234567", {});
  check_mention("нет@mention", {});
  check_mention("@ya @gif @wiki @vid @bing @pic @bold @imdb @coub @like @vote @giff @cap ya cap @y @yar @bingg @bin",
                {"@gif", "@wiki", "@vid", "@bing", "@pic", "@bold", "@imdb", "@coub", "@like", "@vote", "@bingg"});
};

static void check_bot_command(const td::string &str, const td::vector<td::string> &expected) {
  auto result_slice = td::find_bot_commands(str);
  td::vector<td::string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result))
               << td::tag("expected", td::format::as_array(expected));
  }
}

TEST(MessageEntities, bot_command) {
  // 1..64@3..32
  check_bot_command("/abc", {"/abc"});
  check_bot_command(" /abc", {"/abc"});
  check_bot_command("/abc ", {"/abc"});
  check_bot_command(" /abc ", {"/abc"});
  check_bot_command("/a@abc", {"/a@abc"});
  check_bot_command("/a@b", {});
  check_bot_command("/@bfdsa", {});
  check_bot_command("/test/", {});
}

static void check_hashtag(const td::string &str, const td::vector<td::string> &expected) {
  auto result_slice = td::find_hashtags(str);
  td::vector<td::string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result))
               << td::tag("expected", td::format::as_array(expected));
  }
}

TEST(MessageEntities, hashtag) {
  check_hashtag("", {});
  check_hashtag("#", {});
  check_hashtag("##", {});
  check_hashtag("###", {});
  check_hashtag("#a", {"#a"});
  check_hashtag(" #a", {"#a"});
  check_hashtag("#a ", {"#a"});
  check_hashtag(" #я ", {"#я"});
  check_hashtag(" я#a ", {});
  check_hashtag(" #a# ", {});
  check_hashtag(" #123 ", {});
  check_hashtag(" #123a ", {"#123a"});
  check_hashtag(" #a123 ", {"#a123"});
  check_hashtag(" #123a# ", {});
  check_hashtag(" #" + td::string(300, '1'), {});
  check_hashtag(" #" + td::string(256, '1'), {});
  check_hashtag(" #" + td::string(256, '1') + "a ", {});
  check_hashtag(" #" + td::string(255, '1') + "a", {"#" + td::string(255, '1') + "a"});
  check_hashtag(" #" + td::string(255, '1') + "Я", {"#" + td::string(255, '1') + "Я"});
  check_hashtag(" #" + td::string(255, '1') + "a" + td::string(255, 'b') + "# ", {});
  check_hashtag("#a#b #c #d", {"#c", "#d"});
  check_hashtag("#test", {"#test"});
  check_hashtag(u8"\U0001F604\U0001F604\U0001F604\U0001F604 \U0001F604\U0001F604\U0001F604#" + td::string(200, '1') +
                    "ООО" + td::string(200, '2'),
                {"#" + td::string(200, '1') + "ООО" + td::string(53, '2')});
  check_hashtag(u8"#a\u2122", {"#a"});
}

static void check_cashtag(const td::string &str, const td::vector<td::string> &expected) {
  auto result_slice = td::find_cashtags(str);
  td::vector<td::string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result))
               << td::tag("expected", td::format::as_array(expected));
  }
}

TEST(MessageEntities, cashtag) {
  check_cashtag("", {});
  check_cashtag("$", {});
  check_cashtag("$$", {});
  check_cashtag("$$$", {});
  check_cashtag("$a", {});
  check_cashtag(" $a", {});
  check_cashtag("$a ", {});
  check_cashtag(" $я ", {});
  check_cashtag("$ab", {});
  check_cashtag("$abc", {});
  check_cashtag("$", {});
  check_cashtag("$A", {});
  check_cashtag("$AB", {});
  check_cashtag("$АBC", {});
  check_cashtag("$АВС", {});
  check_cashtag("$ABC", {"$ABC"});
  check_cashtag("$ABCD", {"$ABCD"});
  check_cashtag("$ABCDE", {"$ABCDE"});
  check_cashtag("$ABCDEF", {"$ABCDEF"});
  check_cashtag("$ABCDEFG", {"$ABCDEFG"});
  check_cashtag("$ABCDEFGH", {"$ABCDEFGH"});
  check_cashtag("$ABCDEFGHJ", {});
  check_cashtag("$ABCDEFGH1", {});
  check_cashtag(" $XYZ", {"$XYZ"});
  check_cashtag("$XYZ ", {"$XYZ"});
  check_cashtag(" $XYZ ", {"$XYZ"});
  check_cashtag(" $$XYZ ", {});
  check_cashtag(" $XYZ$ ", {});
  check_cashtag(" $ABC1 ", {});
  check_cashtag(" $1ABC ", {});
  check_cashtag(" 1$ABC ", {});
  check_cashtag(" А$ABC ", {});
  check_cashtag("$ABC$DEF $GHI $KLM", {"$GHI", "$KLM"});
  check_cashtag("$TEST", {"$TEST"});
  check_cashtag(u8"$ABC\u2122", {"$ABC"});
  check_cashtag(u8"\u2122$ABC", {"$ABC"});
  check_cashtag(u8"\u2122$ABC\u2122", {"$ABC"});
}

static void check_is_email_address(const td::string &str, bool expected) {
  bool result = td::is_email_address(str);
  LOG_IF(FATAL, result != expected) << "Expected " << expected << " as result of is_email_address(" << str << ")";
}

TEST(MessageEntities, is_email_address) {
  check_is_email_address("telegram.org", false);
  check_is_email_address("security@telegram.org", true);
  check_is_email_address("security.telegram.org", false);
  check_is_email_address("", false);
  check_is_email_address("@", false);
  check_is_email_address("A@a.a.a.ab", true);
  check_is_email_address("A@a.ab", true);
  check_is_email_address("Test@aa.aa.aa.aa", true);
  check_is_email_address("Test@test.abd", true);
  check_is_email_address("a@a.a.a.ab", true);
  check_is_email_address("test@test.abd", true);
  check_is_email_address("test@test.com", true);
  check_is_email_address("test.abd", false);
  check_is_email_address("a.ab", false);
  check_is_email_address("a.bc@d.ef", true);

  td::vector<td::string> bad_userdatas = {"",
                                          "a.a.a.a.a.a.a.a.a.a.a.a",
                                          "+.+.+.+.+.+",
                                          "*.a.a",
                                          "a.*.a",
                                          "a.a.*",
                                          "a.a.",
                                          "a.a.abcdefghijklmnopqrstuvwxyz0123456789",
                                          "a.abcdefghijklmnopqrstuvwxyz0.a",
                                          "abcdefghijklmnopqrstuvwxyz0.a.a"};
  td::vector<td::string> good_userdatas = {"a.a.a.a.a.a.a.a.a.a.a",
                                           "a+a+a+a+a+a+a+a+a+a+a",
                                           "+.+.+.+.+._",
                                           "aozAQZ0-5-9_+-aozAQZ0-5-9_.aozAQZ0-5-9_.-._.+-",
                                           "a.a.a",
                                           "a.a.abcdefghijklmnopqrstuvwxyz012345678",
                                           "a.abcdefghijklmnopqrstuvwxyz.a",
                                           "a..a",
                                           "abcdefghijklmnopqrstuvwxyz.a.a",
                                           ".a.a"};

  td::vector<td::string> bad_domains = {"",
                                        ".",
                                        "abc",
                                        "localhost",
                                        "a.a.a.a.a.a.a.ab",
                                        ".......",
                                        "a.a.a.a.a.a+ab",
                                        "a+a.a.a.a.a.ab",
                                        "a.a.a.a.a.a.a",
                                        "a.a.a.a.a.a.abcdefg",
                                        "a.a.a.a.a.a.ab0yz",
                                        "a.a.a.a.a.a.ab9yz",
                                        "a.a.a.a.a.a.ab-yz",
                                        "a.a.a.a.a.a.ab_yz",
                                        "a.a.a.a.a.a.ab*yz",
                                        ".ab",
                                        ".a.ab",
                                        "a..ab",
                                        "a.a.a..a.ab",
                                        ".a.a.a.a.ab",
                                        "abcdefghijklmnopqrstuvwxyz01234.ab",
                                        "ab0cd.abd.aA*sd.0.9.0-9.ABOYZ",
                                        "ab*cd.abd.aAasd.0.9.0-9.ABOYZ",
                                        "ab0cd.abd.aAasd.0.9.0*9.ABOYZ",
                                        "*b0cd.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0c*.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.0-*.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.*-9.ABOYZ",
                                        "-b0cd.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0c-.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.-.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.--9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.0--.ABOYZ",
                                        "_b0cd.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0c_.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd._.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9._-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.0-_.ABOYZ",
                                        "-.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.-.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9.-.ABOYZ",
                                        "_.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d._.0.9.0-9.ABOYZ",
                                        "ab0cd.ab_d.aA-sd.0.9._.ABOYZ"};
  td::vector<td::string> good_domains = {"a.a.a.a.a.a.ab",
                                         "a.a.a.a.a.a.abcdef",
                                         "a.a.a.a.a.a.aboyz",
                                         "a.a.a.a.a.a.ABOYZ",
                                         "a.a.a.a.a.a.AbOyZ",
                                         "abcdefghijklmnopqrstuvwxyz0123.ab",
                                         "ab0cd.ab_d.aA-sd.0.9.0-9.ABOYZ",
                                         "A.Z.aA-sd.a.z.0-9.ABOYZ"};

  for (auto &userdata : bad_userdatas) {
    for (auto &domain : bad_domains) {
      check_is_email_address(userdata + '@' + domain, false);
      check_is_email_address(userdata + domain, false);
    }
    for (auto &domain : good_domains) {
      check_is_email_address(userdata + '@' + domain, false);
      check_is_email_address(userdata + domain, false);
    }
  }
  for (auto &userdata : good_userdatas) {
    for (auto &domain : bad_domains) {
      check_is_email_address(userdata + '@' + domain, false);
      check_is_email_address(userdata + domain, false);
    }
    for (auto &domain : good_domains) {
      check_is_email_address(userdata + '@' + domain, true);
      check_is_email_address(userdata + domain, false);
    }
  }
}

static void check_url(const td::string &str, const td::vector<td::string> &expected_urls,
                      td::vector<td::string> expected_email_addresses = {}) {
  auto result_slice = td::find_urls(str);
  td::vector<td::string> result_urls;
  td::vector<td::string> result_email_addresses;
  for (auto &it : result_slice) {
    if (!it.second) {
      result_urls.push_back(it.first.str());
    } else {
      result_email_addresses.push_back(it.first.str());
    }
  }
  if (result_urls != expected_urls) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result_urls))
               << td::tag("expected", td::format::as_array(expected_urls));
  }
  if (result_email_addresses != expected_email_addresses) {
    LOG(FATAL) << td::tag("text", str) << td::tag("got", td::format::as_array(result_email_addresses))
               << td::tag("expected", td::format::as_array(expected_email_addresses));
  }
}

TEST(MessageEntities, url) {
  check_url("telegram.org", {"telegram.org"});
  check_url("(telegram.org)", {"telegram.org"});
  check_url("\ntelegram.org)", {"telegram.org"});
  check_url(" telegram.org)", {"telegram.org"});
  check_url(".telegram.org)", {});
  check_url("()telegram.org/?q=()", {"telegram.org/?q=()"});
  check_url("\"telegram.org\"", {"telegram.org"});
  check_url(" telegram. org. www. com... telegram.org... ...google.com...", {"telegram.org"});
  check_url(" telegram.org ", {"telegram.org"});
  check_url("Такой сайт: http://www.google.com или такой telegram.org ", {"http://www.google.com", "telegram.org"});
  check_url(" telegram.org. ", {"telegram.org"});
  check_url("http://google,.com", {});
  check_url("http://telegram.org/?asd=123#123.", {"http://telegram.org/?asd=123#123"});
  check_url("[http://google.com](test)", {"http://google.com"});
  check_url("", {});
  check_url(".", {});
  check_url("http://@google.com", {});
  check_url("http://@goog.com", {});  // TODO: server fix
  check_url("http://@@google.com", {"http://@@google.com"});
  check_url("http://a@google.com", {"http://a@google.com"});
  check_url("http://test@google.com", {"http://test@google.com"});
  check_url("google.com:᪉᪉᪉᪉᪉", {"google.com"});
  check_url("https://telegram.org", {"https://telegram.org"});
  check_url("http://telegram.org", {"http://telegram.org"});
  check_url("ftp://telegram.org", {"ftp://telegram.org"});
  check_url("ftps://telegram.org", {});
  check_url("sftp://telegram.org", {"sftp://telegram.org"});
  check_url("hTtPs://telegram.org", {"hTtPs://telegram.org"});
  check_url("HTTP://telegram.org", {"HTTP://telegram.org"});
  check_url("аHTTP://telegram.org", {"HTTP://telegram.org"});
  check_url("sHTTP://telegram.org", {});
  check_url("://telegram.org", {});
  check_url("google.com:᪀᪀", {"google.com"});
  check_url(
      "http://"
      "abcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkab"
      "cdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcd"
      "efghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdef"
      "ghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefgh"
      "ijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghij"
      "kabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijka"
      "bcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabc"
      "defghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijkabcdefghijk.com",
      {});
  check_url("http://  .com", {});
  check_url("URL:     .com", {});
  check_url("URL: .com", {});
  check_url(".com", {});
  check_url("http://  .", {});
  check_url("http://.", {});
  check_url("http://.com", {});
  check_url("http://  .", {});
  check_url(",ahttp://google.com", {"http://google.com"});
  check_url(".ahttp://google.com", {});
  check_url("http://1.0", {});
  check_url("http://a.0", {});
  check_url("http://a.a", {});
  check_url("google.com:1#ab c", {"google.com:1#ab"});
  check_url("google.com:1#", {"google.com:1"});
  check_url("google.com:1#1", {"google.com:1#1"});
  check_url("google.com:00000001/abs", {"google.com:00000001/abs"});
  check_url("google.com:000000065535/abs", {"google.com:000000065535/abs"});
  check_url("google.com:000000065536/abs", {"google.com"});
  check_url("google.com:000000080/abs", {"google.com:000000080/abs"});
  check_url("google.com:0000000/abs", {"google.com"});
  check_url("google.com:0/abs", {"google.com"});
  check_url("google.com:/abs", {"google.com"});
  check_url("google.com:65535", {"google.com:65535"});
  check_url("google.com:65536", {"google.com"});
  check_url("google.com:99999", {"google.com"});
  check_url("google.com:100000", {"google.com"});
  check_url("127.001", {});
  check_url("127.0.0.1", {"127.0.0.1"});
  check_url("127.0.0.01", {});
  check_url("127.0.0.256", {});
  check_url("127.0.0.300", {});
  check_url("127.0.0.1000", {});
  check_url("127.0.0.260", {});
  check_url("1.0", {});
  check_url("www.🤙.tk", {"www.🤙.tk"});
  check_url("a.ab", {});
  check_url("test.abd", {});
  check_url("ТеСт.Москва", {});
  check_url("ТеСт.МоСкВΑ", {});
  check_url("ТеСт.МоСкВа", {"ТеСт.МоСкВа"});
  check_url("ТеСт.МоСкВач", {});
  check_url("http://ÀТеСт.МоСкВач", {"http://ÀТеСт.МоСкВач"});
  check_url("ÀÁ.com. ÀÁ.com.", {"ÀÁ.com", "ÀÁ.com"});
  check_url("ÀÁ.com,ÀÁ.com.", {"ÀÁ.com", "ÀÁ.com"});
  check_url("teiegram.org", {});
  check_url("http://test.google.com/?q=abc()}[]def", {"http://test.google.com/?q=abc()"});
  check_url("http://test.google.com/?q=abc([{)]}def", {"http://test.google.com/?q=abc([{)]}def"});
  check_url("http://test.google.com/?q=abc(){}]def", {"http://test.google.com/?q=abc(){}"});
  check_url("http://test.google.com/?q=abc){}[]def", {"http://test.google.com/?q=abc"});
  check_url("http://test.google.com/?q=abc(){}[]def", {"http://test.google.com/?q=abc(){}[]def"});
  check_url("http://test-.google.com", {});
  check_url("http://test_.google.com", {"http://test_.google.com"});
  check_url("http://google_.com", {});
  check_url("http://google._com_", {});
  check_url("http://[2001:4860:0:2001::68]/", {});  // TODO
  check_url("test.abd", {});
  check_url("/.b/..a    @.....@/. a.ba", {"a.ba"});
  check_url("bbbbbbbbbbbbbb.@.@", {});
  check_url("http://google.com/", {"http://google.com/"});
  check_url("http://google.com?", {"http://google.com"});
  check_url("http://google.com#", {"http://google.com"});
  check_url("http://google.com##", {"http://google.com##"});
  check_url("http://google.com/?", {"http://google.com/"});
  check_url("https://www.google.com/ab,", {"https://www.google.com/ab"});
  check_url("@.", {});
  check_url(
      "a.b.google.com dfsknnfs gsdfgsg http://códuia.de/ dffdg,\" 12)(cpia.de/())(\" http://гришка.рф/ sdufhdf "
      "http://xn--80afpi2a3c.xn--p1ai/ I have a good time.Thanks, guys!\n\n(hdfughidufhgdis)go#ogle.com гришка.рф "
      "hsighsdf gi почта.рф\n\n✪df.ws/123      "
      "xn--80afpi2a3c.xn--p1ai\n\nhttp://foo.com/blah_blah\nhttp://foo.com/blah_blah/\n(Something like "
      "http://foo.com/blah_blah)\nhttp://foo.com/blah_blah_(wikipedi8989a_Вася)\n(Something like "
      "http://foo.com/blah_blah_(Стакан_007))\nhttp://foo.com/blah_blah.\nhttp://foo.com/blah_blah/.\n<http://foo.com/"
      "blah_blah>\n<http://fo@@@@@@@@@^%#*@^&@$#*@#%^*&!^#o.com/blah_blah/>\nhttp://foo.com/blah_blah,\nhttp://"
      "www.example.com/wpstyle/?p=364.\nhttp://✪df.ws/123\nrdar://1234\nrdar:/1234\nhttp://"
      "userid:password@example.com:8080\nhttp://userid@example.com\nhttp://userid@example.com:8080\nhttp://"
      "userid:password@example.com\nhttp://example.com:8080 "
      "x-yojimbo-item://6303E4C1-xxxx-45A6-AB9D-3A908F59AE0E\nmessage://"
      "%3c330e7f8409726r6a4ba78dkf1fd71420c1bf6ff@mail.gmail.com%3e\nhttp://➡️.ws/䨹\nwww.➡️.ws/"
      "䨹\n<tag>http://example.com</tag>\nJust a www.example.com "
      "link.\n\n➡️.ws/"
      "䨹\n\nabcdefghijklmnopqrstuvwxyz0123456789qwe_sdfsdf.aweawe-sdfs.com\nwww.🤙.tk:1\ngoogle.com:"
      "᪉᪉᪉᪉\ngoogle."
      "com:᪀᪀\nhttp://  .com\nURL:     .com\nURL: "
      ".com\n\ngoogle.com?qwe\ngoogle.com#qwe\ngoogle.com/?\ngoogle.com/#\ngoogle.com?\ngoogle.com#\n",
      {"a.b.google.com",
       "http://códuia.de/",
       "cpia.de/()",
       "http://гришка.рф/",
       "http://xn--80afpi2a3c.xn--p1ai/",
       "гришка.рф",
       "почта.рф",
       "✪df.ws/123",
       "xn--80afpi2a3c.xn--p1ai",
       "http://foo.com/blah_blah",
       "http://foo.com/blah_blah/",
       "http://foo.com/blah_blah",
       "http://foo.com/blah_blah_(wikipedi8989a_Вася)",
       "http://foo.com/blah_blah_(Стакан_007)",
       "http://foo.com/blah_blah",
       "http://foo.com/blah_blah/",
       "http://foo.com/blah_blah",
       "http://foo.com/blah_blah",
       "http://www.example.com/wpstyle/?p=364",
       "http://✪df.ws/123",
       "http://userid:password@example.com:8080",
       "http://userid@example.com",
       "http://userid@example.com:8080",
       "http://userid:password@example.com",
       "http://example.com:8080",
       "http://➡️.ws/䨹",
       "www.➡️.ws/䨹",
       "http://example.com",
       "www.example.com",
       "➡️.ws/䨹",
       "abcdefghijklmnopqrstuvwxyz0123456789qwe_sdfsdf.aweawe-sdfs.com",
       "www.🤙.tk:1",
       "google.com",
       "google.com",
       "google.com?qwe",
       "google.com#qwe",
       "google.com/",
       "google.com/#",
       "google.com",
       "google.com"});
  check_url("https://t.…", {});
  check_url("('http://telegram.org/a-b/?br=ie&lang=en',)", {"http://telegram.org/a-b/?br=ie&lang=en"});
  check_url("https://ai.telegram.org/bot%20bot/test-...", {"https://ai.telegram.org/bot%20bot/test-"});
  check_url("a.bc@c.com", {}, {"a.bc@c.com"});
  check_url("https://a.bc@c.com", {"https://a.bc@c.com"});
  check_url("https://a.de[bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de/bc@c.com", {"https://a.de/bc@c.com"});
  check_url("https://a.de]bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de{bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de}bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de(bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de)bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de\\bc@c.com", {"https://a.de\\bc@c.com"});
  check_url("https://a.de'bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.de`bc@c.com", {"https://a.de"}, {"bc@c.com"});
  check_url("https://a.bc:de.fg@c.com", {"https://a.bc:de.fg@c.com"});
  check_url("https://a:h.bc:de.fg@c.com", {"https://a:h.bc:de.fg@c.com"});
  check_url("https://abc@c.com", {"https://abc@c.com"});
  check_url("https://de[bc@c.com", {}, {"bc@c.com"});
  check_url("https://de/bc@c.com", {});
  check_url("https://de]bc@c.com", {}, {"bc@c.com"});
  check_url("https://de{bc@c.com", {}, {"bc@c.com"});
  check_url("https://de}bc@c.com", {}, {"bc@c.com"});
  check_url("https://de(bc@c.com", {}, {"bc@c.com"});
  check_url("https://de)bc@c.com", {}, {"bc@c.com"});
  check_url("https://de\\bc@c.com", {"https://de\\bc@c.com"});
  check_url("https://de'bc@c.com", {}, {"bc@c.com"});
  check_url("https://de`bc@c.com", {}, {"bc@c.com"});
  check_url("https://bc:defg@c.com", {"https://bc:defg@c.com"});
  check_url("https://a:hbc:defg@c.com", {"https://a:hbc:defg@c.com"});
  check_url("https://a.bc@test.com:cd.com", {"https://a.bc@test.com", "cd.com"});
  check_url("telegram.Org", {});
  check_url("telegram.ORG", {"telegram.ORG"});
  check_url("a.b.c.com.a.b.c", {});
  check_url("File '/usr/views.py'", {});  // TODO server fix
  check_url("@views.py'", {});            // TODO server fix
  check_url("#views.py'", {});            // TODO server fix
  check_url("/views.py'", {});            // TODO server fix
  check_url(".views.py", {});
  check_url("'views.py'", {"views.py"});
  check_url("bug.http://test.com/test/+#", {});  // TODO {"http://test.com/test/+#"}
  check_url("//AB.C.D.E.F.GH//", {});
  check_url("<http://www.ics.uci.edu/pub/ietf/uri/historical.html#WARNING>",
            {"http://www.ics.uci.edu/pub/ietf/uri/historical.html#WARNING"});
  check_url("Look :test@example.com", {":test@example.com"}, {});  // TODO {}, {"test@example.com"}
  check_url("Look mailto:test@example.com", {}, {"test@example.com"});
  check_url("http://test.com#a", {"http://test.com#a"});
  check_url("http://test.com#", {"http://test.com"});
  check_url("http://test.com?#", {"http://test.com?#"});
  check_url("http://test.com/?#", {"http://test.com/?#"});
  check_url("https://t.me/abcdef…", {"https://t.me/abcdef"});
  check_url("https://t.me…", {"https://t.me"});
  check_url("https://t.m…", {});
  check_url("https://t.…", {});
  check_url("https://t…", {});
  check_url("👉http://ab.com/cdefgh-1IJ", {"http://ab.com/cdefgh-1IJ"});
  check_url("...👉http://ab.com/cdefgh-1IJ", {});  // TODO
}

static void check_fix_formatted_text(td::string str, td::vector<td::MessageEntity> entities,
                                     const td::string &expected_str,
                                     const td::vector<td::MessageEntity> &expected_entities, bool allow_empty,
                                     bool skip_new_entities, bool skip_bot_commands, bool for_draft) {
  ASSERT_TRUE(
      td::fix_formatted_text(str, entities, allow_empty, skip_new_entities, skip_bot_commands, for_draft).is_ok());
  ASSERT_STREQ(expected_str, str);
  ASSERT_EQ(expected_entities, entities);
}

static void check_fix_formatted_text(td::string str, td::vector<td::MessageEntity> entities, bool allow_empty,
                                     bool skip_new_entities, bool skip_bot_commands, bool for_draft) {
  ASSERT_TRUE(
      fix_formatted_text(str, entities, allow_empty, skip_new_entities, skip_bot_commands, for_draft).is_error());
}

TEST(MessageEntities, fix_formatted_text) {
  td::string str;
  td::string fixed_str;
  for (auto i = 0; i <= 32; i++) {
    str += static_cast<char>(i);
    if (i != 13) {
      if (i != 10) {
        fixed_str += ' ';
      } else {
        fixed_str += str.back();
      }
    }
  }

  check_fix_formatted_text(str, {}, "", {}, true, true, true, true);
  check_fix_formatted_text(str, {}, "", {}, true, true, false, true);
  check_fix_formatted_text(str, {}, "", {}, true, false, true, true);
  check_fix_formatted_text(str, {}, "", {}, true, false, false, true);
  check_fix_formatted_text(str, {}, "", {}, true, false, false, false);
  check_fix_formatted_text(str, {}, false, false, false, false);
  check_fix_formatted_text(str, {}, false, false, false, true);

  str += "a  \r\n  ";
  fixed_str += "a  \n  ";

  for (td::int32 i = 33; i <= 35; i++) {
    td::vector<td::MessageEntity> entities;
    entities.emplace_back(td::MessageEntity::Type::Bold, 0, i);

    td::vector<td::MessageEntity> fixed_entities;
    if (i != 33) {
      fixed_entities = entities;
      fixed_entities.back().length = i - 1;
    }
    check_fix_formatted_text(str, entities, fixed_str, fixed_entities, true, false, false, true);

    td::string expected_str;
    if (i != 33) {
      fixed_entities = entities;
      fixed_entities.back().length = 33;
      expected_str = fixed_str.substr(0, 33);
    } else {
      fixed_entities.clear();
      expected_str = "a";
    }
    check_fix_formatted_text(str, entities, expected_str, fixed_entities, false, false, false, false);
  }

  str = "👉 👉  ";
  for (int i = 0; i < 10; i++) {
    td::vector<td::MessageEntity> entities;
    entities.emplace_back(td::MessageEntity::Type::Bold, i, 1);
    if (i != 2 && i != 5 && i != 6) {
      check_fix_formatted_text(str, entities, true, true, true, true);
      check_fix_formatted_text(str, entities, false, false, false, false);
    } else {
      check_fix_formatted_text(str, entities, str, {}, true, true, true, true);
      check_fix_formatted_text(str, entities, str.substr(0, str.size() - 2), {}, false, false, false, false);
    }
  }

  str = "  /test @abaca #ORD $ABC  telegram.org ";
  for (auto for_draft : {false, true}) {
    td::int32 shift = for_draft ? 2 : 0;
    td::string expected_str = for_draft ? str : str.substr(2, str.size() - 3);

    for (auto skip_new_entities : {false, true}) {
      for (auto skip_bot_commands : {false, true}) {
        td::vector<td::MessageEntity> entities;
        if (!skip_new_entities) {
          if (!skip_bot_commands) {
            entities.emplace_back(td::MessageEntity::Type::BotCommand, shift, 5);
          }
          entities.emplace_back(td::MessageEntity::Type::Mention, shift + 6, 6);
          entities.emplace_back(td::MessageEntity::Type::Hashtag, shift + 13, 4);
          entities.emplace_back(td::MessageEntity::Type::Cashtag, shift + 18, 4);
          entities.emplace_back(td::MessageEntity::Type::Url, shift + 24, 12);
        }

        check_fix_formatted_text(str, {}, expected_str, entities, true, skip_new_entities, skip_bot_commands,
                                 for_draft);
        check_fix_formatted_text(str, {}, expected_str, entities, false, skip_new_entities, skip_bot_commands,
                                 for_draft);
      }
    }
  }

  str = "aba \r\n caba ";
  for (td::int32 length = 1; length <= 3; length++) {
    for (td::int32 offset = 0; static_cast<size_t>(offset + length) <= str.size(); offset++) {
      for (auto type : {td::MessageEntity::Type::Bold, td::MessageEntity::Type::Url, td::MessageEntity::Type::TextUrl,
                        td::MessageEntity::Type::MentionName}) {
        for (auto for_draft : {false, true}) {
          fixed_str = for_draft ? "aba \n caba " : "aba \n caba";
          auto fixed_length = offset <= 4 && offset + length >= 5 ? length - 1 : length;
          auto fixed_offset = offset >= 5 ? offset - 1 : offset;
          if (static_cast<size_t>(fixed_offset) >= fixed_str.size()) {
            fixed_length = 0;
          }
          while (static_cast<size_t>(fixed_offset + fixed_length) > fixed_str.size()) {
            fixed_length--;
          }

          td::vector<td::MessageEntity> entities;
          entities.emplace_back(type, offset, length);
          td::vector<td::MessageEntity> fixed_entities;
          if (fixed_length > 0) {
            for (auto i = 0; i < length; i++) {
              if (str[offset + i] != '\r' && str[offset + i] != '\n' &&
                  (str[offset + i] != ' ' || type == td::MessageEntity::Type::TextUrl ||
                   type == td::MessageEntity::Type::MentionName)) {
                fixed_entities.emplace_back(type, fixed_offset, fixed_length);
                break;
              }
            }
          }
          check_fix_formatted_text(str, entities, fixed_str, fixed_entities, true, false, false, for_draft);
        }
      }
    }
  }

  str = "aba caba";
  for (td::int32 length = -10; length <= 10; length++) {
    for (td::int32 offset = -10; offset <= 10; offset++) {
      td::vector<td::MessageEntity> entities;
      entities.emplace_back(td::MessageEntity::Type::Bold, offset, length);
      td::vector<td::MessageEntity> fixed_entities;
      if (length > 0 && offset >= 0 && static_cast<size_t>(length + offset) > str.size()) {
        check_fix_formatted_text(str, entities, true, false, false, false);
        check_fix_formatted_text(str, entities, false, false, false, true);
        continue;
      }

      if (length > 0 && offset >= 0 && (length >= 2 || offset != 3)) {
        fixed_entities.emplace_back(td::MessageEntity::Type::Bold, offset, length);
      }
      check_fix_formatted_text(str, entities, str, fixed_entities, true, false, false, false);
      check_fix_formatted_text(str, entities, str, fixed_entities, false, false, false, true);
    }
  }

  str = "aba caba";
  for (td::int32 length = 1; length <= 7; length++) {
    for (td::int32 offset = 0; offset <= 8 - length; offset++) {
      for (td::int32 length2 = 1; length2 <= 7; length2++) {
        for (td::int32 offset2 = 0; offset2 <= 8 - length2; offset2++) {
          if (offset != offset2) {
            td::vector<td::MessageEntity> entities;
            entities.emplace_back(td::MessageEntity::Type::TextUrl, offset, length);
            entities.emplace_back(td::MessageEntity::Type::TextUrl, offset2, length2);
            td::vector<td::MessageEntity> fixed_entities = entities;
            std::sort(fixed_entities.begin(), fixed_entities.end());
            if (fixed_entities[0].offset + fixed_entities[0].length > fixed_entities[1].offset) {
              fixed_entities.pop_back();
            }
            check_fix_formatted_text(str, entities, str, fixed_entities, false, false, false, false);
          }
        }
      }
    }
  }

  for (auto text : {" \n ➡️ ➡️ ➡️ ➡️  \n ", "\n\n\nab cd ef gh        "}) {
    str = text;
    td::vector<td::MessageEntity> entities;
    td::vector<td::MessageEntity> fixed_entities;

    auto length = td::narrow_cast<int>(td::utf8_utf16_length(str));
    for (int i = 0; i < 10; i++) {
      if ((i + 1) * 3 + 2 <= length) {
        entities.emplace_back(td::MessageEntity::Type::Bold, (i + 1) * 3, 2);
      }
      if ((i + 2) * 3 <= length) {
        entities.emplace_back(td::MessageEntity::Type::Italic, (i + 1) * 3 + 2, 1);
      }

      if (i < 4) {
        fixed_entities.emplace_back(td::MessageEntity::Type::Bold, i * 3, 2);
      }
    }

    check_fix_formatted_text(str, entities, td::utf8_utf16_substr(str, 3, 11), fixed_entities, false, false, false,
                             false);
  }

  for (td::string text : {"\t", "\r", "\n", "\t ", "\r ", "\n "}) {
    for (auto type : {td::MessageEntity::Type::Bold, td::MessageEntity::Type::TextUrl}) {
      check_fix_formatted_text(text, {{type, 0, 1, "http://telegram.org/"}}, "", {}, true, false, false, true);
    }
  }
}

static void check_parse_html(td::string text, const td::string &result, const td::vector<td::MessageEntity> &entities) {
  auto r_entities = td::parse_html(text);
  ASSERT_TRUE(r_entities.is_ok());
  ASSERT_EQ(entities, r_entities.ok());
  ASSERT_STREQ(result, text);
}

static void check_parse_html(td::string text, td::Slice error_message) {
  auto r_entities = td::parse_html(text);
  ASSERT_TRUE(r_entities.is_error());
  ASSERT_EQ(400, r_entities.error().code());
  ASSERT_STREQ(error_message, r_entities.error().message());
}

TEST(MessageEntities, parse_html) {
  td::string invalid_surrogate_pair_error_message =
      "Text contains invalid Unicode characters after decoding HTML entities, check for unmatched surrogate code units";
  check_parse_html("&#57311;", invalid_surrogate_pair_error_message);
  check_parse_html("&#xDFDF;", invalid_surrogate_pair_error_message);
  check_parse_html("&#xDFDF", invalid_surrogate_pair_error_message);
  check_parse_html("🏟 🏟&lt;<abacaba", "Unclosed start tag at byte offset 13");
  check_parse_html("🏟 🏟&lt;<abac aba>", "Unsupported start tag \"abac\" at byte offset 13");
  check_parse_html("🏟 🏟&lt;<abac>", "Unsupported start tag \"abac\" at byte offset 13");
  check_parse_html("🏟 🏟&lt;<i   =aba>", "Empty attribute name in the tag \"i\" at byte offset 13");
  check_parse_html("🏟 🏟&lt;<i    aba>",
                   "Expected equal sign in declaration of an attribute of the tag \"i\" at byte offset 13");
  check_parse_html("🏟 🏟&lt;<i    aba  =  ", "Unclosed start tag \"i\" at byte offset 13");
  check_parse_html("🏟 🏟&lt;<i    aba  =  190azAz-.,", "Unexpected end of name token at byte offset 27");
  check_parse_html("🏟 🏟&lt;<i    aba  =  \"&lt;&gt;&quot;>", "Unclosed start tag at byte offset 13");
  check_parse_html("🏟 🏟&lt;<i    aba  =  \'&lt;&gt;&quot;>", "Unclosed start tag at byte offset 13");
  check_parse_html("🏟 🏟&lt;</", "Unexpected end tag at byte offset 13");
  check_parse_html("🏟 🏟&lt;<b></b></", "Unexpected end tag at byte offset 20");
  check_parse_html("🏟 🏟&lt;<i>a</i   ", "Unclosed end tag at byte offset 17");
  check_parse_html("🏟 🏟&lt;<i>a</em   >",
                   "Unmatched end tag at byte offset 17, expected \"</i>\", found \"</em>\"");

  check_parse_html("", "", {});
  check_parse_html("➡️ ➡️", "➡️ ➡️", {});
  check_parse_html("&lt;&gt;&amp;&quot;&laquo;&raquo;&#12345678;", "<>&\"&laquo;&raquo;&#12345678;", {});
  check_parse_html("➡️ ➡️<i>➡️ ➡️</i>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Italic, 5, 5}});
  check_parse_html("➡️ ➡️<em>➡️ ➡️</em>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Italic, 5, 5}});
  check_parse_html("➡️ ➡️<b>➡️ ➡️</b>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Bold, 5, 5}});
  check_parse_html("➡️ ➡️<strong>➡️ ➡️</strong>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Bold, 5, 5}});
  check_parse_html("➡️ ➡️<u>➡️ ➡️</u>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Underline, 5, 5}});
  check_parse_html("➡️ ➡️<ins>➡️ ➡️</ins>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Underline, 5, 5}});
  check_parse_html("➡️ ➡️<s>➡️ ➡️</s>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Strikethrough, 5, 5}});
  check_parse_html("➡️ ➡️<strike>➡️ ➡️</strike>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Strikethrough, 5, 5}});
  check_parse_html("➡️ ➡️<del>➡️ ➡️</del>", "➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Strikethrough, 5, 5}});
  check_parse_html("➡️ ➡️<i>➡️ ➡️</i><b>➡️ ➡️</b>", "➡️ ➡️➡️ ➡️➡️ ➡️",
                   {{td::MessageEntity::Type::Italic, 5, 5}, {td::MessageEntity::Type::Bold, 10, 5}});
  check_parse_html("🏟 🏟<i>🏟 &lt🏟</i>", "🏟 🏟🏟 <🏟", {{td::MessageEntity::Type::Italic, 5, 6}});
  check_parse_html("🏟 🏟<i>🏟 &gt;<b aba   =   caba>&lt🏟</b></i>", "🏟 🏟🏟 ><🏟",
                   {{td::MessageEntity::Type::Italic, 5, 7}, {td::MessageEntity::Type::Bold, 9, 3}});
  check_parse_html("🏟 🏟&lt;<i    aba  =  190azAz-.   >a</i>", "🏟 🏟<a",
                   {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i    aba  =  190azAz-.>a</i>", "🏟 🏟<a",
                   {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i    aba  =  \"&lt;&gt;&quot;\">a</i>", "🏟 🏟<a",
                   {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i    aba  =  '&lt;&gt;&quot;'>a</i>", "🏟 🏟<a",
                   {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i    aba  =  '&lt;&gt;&quot;'>a</>", "🏟 🏟<a",
                   {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i>🏟 🏟&lt;</>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::Italic, 6, 6}});
  check_parse_html("🏟 🏟&lt;<i>a</    >", "🏟 🏟<a", {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<i>a</i   >", "🏟 🏟<a", {{td::MessageEntity::Type::Italic, 6, 1}});
  check_parse_html("🏟 🏟&lt;<b></b>", "🏟 🏟<", {});
  check_parse_html("<i>\t</i>", "\t", {{td::MessageEntity::Type::Italic, 0, 1}});
  check_parse_html("<i>\r</i>", "\r", {{td::MessageEntity::Type::Italic, 0, 1}});
  check_parse_html("<i>\n</i>", "\n", {{td::MessageEntity::Type::Italic, 0, 1}});
  check_parse_html("<a href=telegram.org>\t</a>", "\t",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<a href=telegram.org>\r</a>", "\r",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<a href=telegram.org>\n</a>", "\n",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<code><i><b> </b></i></code><i><b><code> </code></b></i>", "  ",
                   {{td::MessageEntity::Type::Code, 0, 1},
                    {td::MessageEntity::Type::Bold, 0, 1},
                    {td::MessageEntity::Type::Italic, 0, 1},
                    {td::MessageEntity::Type::Code, 1, 1},
                    {td::MessageEntity::Type::Bold, 1, 1},
                    {td::MessageEntity::Type::Italic, 1, 1}});
  check_parse_html("<i><b> </b> <code> </code></i>", "   ",
                   {{td::MessageEntity::Type::Italic, 0, 3},
                    {td::MessageEntity::Type::Bold, 0, 1},
                    {td::MessageEntity::Type::Code, 2, 1}});
  check_parse_html("<a href=telegram.org> </a>", " ",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<a href  =\"telegram.org\"   > </a>", " ",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<a   href=  'telegram.org'   > </a>", " ",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_html("<a   href=  'telegram.org?&lt;'   > </a>", " ",
                   {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/?<"}});
  check_parse_html("<a> </a>", " ", {});
  check_parse_html("<a>telegram.org </a>", "telegram.org ", {});
  check_parse_html("<a>telegram.org</a>", "telegram.org",
                   {{td::MessageEntity::Type::TextUrl, 0, 12, "http://telegram.org/"}});
  check_parse_html("<a>https://telegram.org/asdsa?asdasdwe#12e3we</a>", "https://telegram.org/asdsa?asdasdwe#12e3we",
                   {{td::MessageEntity::Type::TextUrl, 0, 42, "https://telegram.org/asdsa?asdasdwe#12e3we"}});
  check_parse_html("🏟 🏟&lt;<pre  >🏟 🏟&lt;</>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::Pre, 6, 6}});
  check_parse_html("🏟 🏟&lt;<code >🏟 🏟&lt;</>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::Code, 6, 6}});
  check_parse_html("🏟 🏟&lt;<pre><code>🏟 🏟&lt;</code></>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::Pre, 6, 6}, {td::MessageEntity::Type::Code, 6, 6}});
  check_parse_html("🏟 🏟&lt;<pre><code class=\"language-\">🏟 🏟&lt;</code></>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::Pre, 6, 6}, {td::MessageEntity::Type::Code, 6, 6}});
  check_parse_html("🏟 🏟&lt;<pre><code class=\"language-fift\">🏟 🏟&lt;</></>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::PreCode, 6, 6, "fift"}});
  check_parse_html("🏟 🏟&lt;<code class=\"language-fift\"><pre>🏟 🏟&lt;</></>", "🏟 🏟<🏟 🏟<",
                   {{td::MessageEntity::Type::PreCode, 6, 6, "fift"}});
  check_parse_html("🏟 🏟&lt;<pre><code class=\"language-fift\">🏟 🏟&lt;</> </>", "🏟 🏟<🏟 🏟< ",
                   {{td::MessageEntity::Type::Pre, 6, 7}, {td::MessageEntity::Type::Code, 6, 6}});
  check_parse_html("🏟 🏟&lt;<pre> <code class=\"language-fift\">🏟 🏟&lt;</></>", "🏟 🏟< 🏟 🏟<",
                   {{td::MessageEntity::Type::Pre, 6, 7}, {td::MessageEntity::Type::Code, 7, 6}});
}

static void check_parse_markdown(td::string text, const td::string &result,
                                 const td::vector<td::MessageEntity> &entities) {
  auto r_entities = td::parse_markdown_v2(text);
  ASSERT_TRUE(r_entities.is_ok());
  ASSERT_EQ(entities, r_entities.ok());
  ASSERT_STREQ(result, text);
}

static void check_parse_markdown(td::string text, td::Slice error_message) {
  auto r_entities = td::parse_markdown_v2(text);
  ASSERT_TRUE(r_entities.is_error());
  ASSERT_EQ(400, r_entities.error().code());
  ASSERT_STREQ(error_message, r_entities.error().message());
}

TEST(MessageEntities, parse_markdown) {
  td::Slice reserved_characters("]()>#+-=|{}.!");
  td::Slice begin_characters("_*[~`");
  for (char c = 1; c < 126; c++) {
    if (begin_characters.find(c) != td::Slice::npos) {
      continue;
    }

    td::string text(1, c);
    if (reserved_characters.find(c) == td::Slice::npos) {
      check_parse_markdown(text, text, {});
    } else {
      check_parse_markdown(
          text, PSLICE() << "Character '" << c << "' is reserved and must be escaped with the preceding '\\'");

      td::string escaped_text = "\\" + text;
      check_parse_markdown(escaped_text, text, {});
    }
  }

  check_parse_markdown("🏟 🏟_abacaba", "Can't find end of Italic entity at byte offset 9");
  check_parse_markdown("🏟 🏟_abac * asd ", "Can't find end of Bold entity at byte offset 15");
  check_parse_markdown("🏟 🏟_abac * asd _", "Can't find end of Italic entity at byte offset 21");
  check_parse_markdown("🏟 🏟`", "Can't find end of Code entity at byte offset 9");
  check_parse_markdown("🏟 🏟```", "Can't find end of Pre entity at byte offset 9");
  check_parse_markdown("🏟 🏟```a", "Can't find end of Pre entity at byte offset 9");
  check_parse_markdown("🏟 🏟```a ", "Can't find end of PreCode entity at byte offset 9");
  check_parse_markdown("🏟 🏟__🏟 🏟_", "Can't find end of Italic entity at byte offset 20");
  check_parse_markdown("🏟 🏟_🏟 🏟__", "Can't find end of Underline entity at byte offset 19");
  check_parse_markdown("🏟 🏟```🏟 🏟`", "Can't find end of Code entity at byte offset 21");
  check_parse_markdown("🏟 🏟```🏟 🏟_", "Can't find end of PreCode entity at byte offset 9");
  check_parse_markdown("🏟 🏟```🏟 🏟\\`", "Can't find end of PreCode entity at byte offset 9");
  check_parse_markdown("[telegram\\.org](asd\\)", "Can't find end of a URL at byte offset 16");
  check_parse_markdown("[telegram\\.org](", "Can't find end of a URL at byte offset 16");
  check_parse_markdown("[telegram\\.org](asd", "Can't find end of a URL at byte offset 16");
  check_parse_markdown("🏟 🏟__🏟 _🏟___", "Can't find end of Italic entity at byte offset 23");
  check_parse_markdown("🏟 🏟__", "Can't find end of Underline entity at byte offset 9");

  check_parse_markdown("", "", {});
  check_parse_markdown("\\\\", "\\", {});
  check_parse_markdown("\\\\\\", "\\\\", {});
  check_parse_markdown("\\\\\\\\\\_\\*\\`", "\\\\_*`", {});
  check_parse_markdown("➡️ ➡️", "➡️ ➡️", {});
  check_parse_markdown("🏟 🏟``", "🏟 🏟", {});
  check_parse_markdown("🏟 🏟_abac \\* asd _", "🏟 🏟abac * asd ", {{td::MessageEntity::Type::Italic, 5, 11}});
  check_parse_markdown("🏟 \\.🏟_🏟\\. 🏟_", "🏟 .🏟🏟. 🏟", {{td::MessageEntity::Type::Italic, 6, 6}});
  check_parse_markdown("\\\\\\a\\b\\c\\d\\e\\f\\1\\2\\3\\4\\➡️\\", "\\abcdef1234\\➡️\\", {});
  check_parse_markdown("➡️ ➡️_➡️ ➡️_", "➡️ ➡️➡️ ➡️",
                       {{td::MessageEntity::Type::Italic, 5, 5}});
  check_parse_markdown("➡️ ➡️_➡️ ➡️_*➡️ ➡️*", "➡️ ➡️➡️ ➡️➡️ ➡️",
                       {{td::MessageEntity::Type::Italic, 5, 5}, {td::MessageEntity::Type::Bold, 10, 5}});
  check_parse_markdown("🏟 🏟_🏟 \\.🏟_", "🏟 🏟🏟 .🏟", {{td::MessageEntity::Type::Italic, 5, 6}});
  check_parse_markdown("🏟 🏟_🏟 *🏟*_", "🏟 🏟🏟 🏟",
                       {{td::MessageEntity::Type::Italic, 5, 5}, {td::MessageEntity::Type::Bold, 8, 2}});
  check_parse_markdown("🏟 🏟_🏟 __🏟___", "🏟 🏟🏟 🏟",
                       {{td::MessageEntity::Type::Italic, 5, 5}, {td::MessageEntity::Type::Underline, 8, 2}});
  check_parse_markdown("🏟 🏟__🏟 _🏟_ __", "🏟 🏟🏟 🏟 ",
                       {{td::MessageEntity::Type::Underline, 5, 6}, {td::MessageEntity::Type::Italic, 8, 2}});
  check_parse_markdown("🏟 🏟__🏟 _🏟_\\___", "🏟 🏟🏟 🏟_",
                       {{td::MessageEntity::Type::Underline, 5, 6}, {td::MessageEntity::Type::Italic, 8, 2}});
  check_parse_markdown("🏟 🏟`🏟 🏟```", "🏟 🏟🏟 🏟", {{td::MessageEntity::Type::Code, 5, 5}});
  check_parse_markdown("🏟 🏟```🏟 🏟```", "🏟 🏟 🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 3, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\n🏟```", "🏟 🏟🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 2, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\r🏟```", "🏟 🏟🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 2, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\n\r🏟```", "🏟 🏟🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 2, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\r\n🏟```", "🏟 🏟🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 2, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\n\n🏟```", "🏟 🏟\n🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 3, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟\r\r🏟```", "🏟 🏟\r🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 3, "🏟"}});
  check_parse_markdown("🏟 🏟```🏟 \\\\\\`🏟```", "🏟 🏟 \\`🏟",
                       {{td::MessageEntity::Type::PreCode, 5, 5, "🏟"}});
  check_parse_markdown("🏟 🏟**", "🏟 🏟", {});
  check_parse_markdown("🏟 🏟``", "🏟 🏟", {});
  check_parse_markdown("🏟 🏟``````", "🏟 🏟", {});
  check_parse_markdown("🏟 🏟____", "🏟 🏟", {});
  check_parse_markdown("`_* *_`__*` `*__", "_* *_ ",
                       {{td::MessageEntity::Type::Code, 0, 5},
                        {td::MessageEntity::Type::Code, 5, 1},
                        {td::MessageEntity::Type::Bold, 5, 1},
                        {td::MessageEntity::Type::Underline, 5, 1}});
  check_parse_markdown("_* * ` `_", "   ",
                       {{td::MessageEntity::Type::Italic, 0, 3},
                        {td::MessageEntity::Type::Bold, 0, 1},
                        {td::MessageEntity::Type::Code, 2, 1}});
  check_parse_markdown("[](telegram.org)", "", {});
  check_parse_markdown("[ ](telegram.org)", " ", {{td::MessageEntity::Type::TextUrl, 0, 1, "http://telegram.org/"}});
  check_parse_markdown("[ ](as)", " ", {});
  check_parse_markdown("[telegram\\.org]", "telegram.org",
                       {{td::MessageEntity::Type::TextUrl, 0, 12, "http://telegram.org/"}});
  check_parse_markdown("[telegram\\.org]a", "telegram.orga",
                       {{td::MessageEntity::Type::TextUrl, 0, 12, "http://telegram.org/"}});
  check_parse_markdown("[telegram\\.org](telegram.dog)", "telegram.org",
                       {{td::MessageEntity::Type::TextUrl, 0, 12, "http://telegram.dog/"}});
  check_parse_markdown("[telegram\\.org](https://telegram.dog?)", "telegram.org",
                       {{td::MessageEntity::Type::TextUrl, 0, 12, "https://telegram.dog/?"}});
  check_parse_markdown("[telegram\\.org](https://telegram.dog?\\\\\\()", "telegram.org",
                       {{td::MessageEntity::Type::TextUrl, 0, 12, "https://telegram.dog/?\\("}});
  check_parse_markdown("[telegram\\.org]()", "telegram.org", {});
  check_parse_markdown("[telegram\\.org](asdasd)", "telegram.org", {});
  check_parse_markdown("[telegram\\.org](tg:user?id=123456)", "telegram.org", {{0, 12, td::UserId(123456)}});
}
