//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageEntity.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"

REGISTER_TESTS(message_entities);

using namespace td;

static void check_mention(string str, std::vector<string> expected) {
  auto result_slice = find_mentions(str);
  std::vector<string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result))
               << tag("expected", format::as_array(expected));
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
  check_mention(
      "@ya @gif @wiki @vid @bing @pic @bold @imdb @coub @like @vote @giff @cap ya cap @y @yar @bingg @bin",
      {"@ya", "@gif", "@wiki", "@vid", "@bing", "@pic", "@bold", "@imdb", "@coub", "@like", "@vote", "@bingg"});
};

static void check_bot_command(string str, std::vector<string> expected) {
  auto result_slice = find_bot_commands(str);
  std::vector<string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result))
               << tag("expected", format::as_array(expected));
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

static void check_hashtag(string str, std::vector<string> expected) {
  auto result_slice = find_hashtags(str);
  std::vector<string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result))
               << tag("expected", format::as_array(expected));
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
  check_hashtag(" #" + string(300, '1'), {});
  check_hashtag(" #" + string(256, '1'), {});
  check_hashtag(" #" + string(256, '1') + "a ", {});
  check_hashtag(" #" + string(255, '1') + "a", {"#" + string(255, '1') + "a"});
  check_hashtag(" #" + string(255, '1') + "Я", {"#" + string(255, '1') + "Я"});
  check_hashtag(" #" + string(255, '1') + "a" + string(255, 'b') + "# ", {});
  check_hashtag("#a#b #c #d", {"#c", "#d"});
  check_hashtag("#test", {"#test"});
  check_hashtag(u8"\U0001F604\U0001F604\U0001F604\U0001F604 \U0001F604\U0001F604\U0001F604#" + string(200, '1') +
                    "ООО" + string(200, '2'),
                {"#" + string(200, '1') + "ООО" + string(53, '2')});
  check_hashtag(u8"#a\u2122", {"#a"});
}

static void check_cashtag(string str, std::vector<string> expected) {
  auto result_slice = find_cashtags(str);
  std::vector<string> result;
  for (auto &it : result_slice) {
    result.push_back(it.str());
  }
  if (result != expected) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result))
               << tag("expected", format::as_array(expected));
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

static void check_is_email_address(string str, bool expected) {
  bool result = is_email_address(str);
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

  vector<string> bad_userdatas = {"",
                                  "a.a.a.a.a.a.a.a.a.a.a.a",
                                  "+.+.+.+.+.+",
                                  "*.a.a",
                                  "a.*.a",
                                  "a.a.*",
                                  "a.a.",
                                  "a.a.abcdefghijklmnopqrstuvwxyz0123456789",
                                  "a.abcdefghijklmnopqrstuvwxyz0.a",
                                  "abcdefghijklmnopqrstuvwxyz0.a.a"};
  vector<string> good_userdatas = {"a.a.a.a.a.a.a.a.a.a.a",
                                   "a+a+a+a+a+a+a+a+a+a+a",
                                   "+.+.+.+.+._",
                                   "aozAQZ0-5-9_+-aozAQZ0-5-9_.aozAQZ0-5-9_.-._.+-",
                                   "a.a.a",
                                   "a.a.abcdefghijklmnopqrstuvwxyz012345678",
                                   "a.abcdefghijklmnopqrstuvwxyz.a",
                                   "a..a",
                                   "abcdefghijklmnopqrstuvwxyz.a.a",
                                   ".a.a"};

  vector<string> bad_domains = {"",
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
  vector<string> good_domains = {"a.a.a.a.a.a.ab",
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

static void check_url(string str, std::vector<string> expected_urls,
                      std::vector<string> expected_email_addresses = {}) {
  auto result_slice = find_urls(str);
  std::vector<string> result_urls;
  std::vector<string> result_email_addresses;
  for (auto &it : result_slice) {
    if (!it.second) {
      result_urls.push_back(it.first.str());
    } else {
      result_email_addresses.push_back(it.first.str());
    }
  }
  if (result_urls != expected_urls) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result_urls))
               << tag("expected", format::as_array(expected_urls));
  }
  if (result_email_addresses != expected_email_addresses) {
    LOG(FATAL) << tag("text", str) << tag("got", format::as_array(result_email_addresses))
               << tag("expected", format::as_array(expected_email_addresses));
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
