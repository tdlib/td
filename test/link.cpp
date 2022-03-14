//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LinkManager.h"

#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/tests.h"

static void check_find_urls(const td::string &url, bool is_valid) {
  auto url_lower = td::to_lower(url);
  {
    auto tg_urls = td::find_tg_urls(url);
    if (is_valid && (td::begins_with(url_lower, "tg://") || td::begins_with(url_lower, "ton://"))) {
      ASSERT_EQ(1u, tg_urls.size());
      ASSERT_STREQ(url, tg_urls[0]);
    } else {
      ASSERT_TRUE(tg_urls.empty() || tg_urls[0] != url);
    }
  }

  {
    if (is_valid && (td::begins_with(url_lower, "http") || td::begins_with(url_lower, "t.me")) &&
        url.find('.') != td::string::npos && url.find(' ') == td::string::npos && url != "http://..") {
      auto urls = td::find_urls(url);
      ASSERT_EQ(1u, urls.size());
      ASSERT_STREQ(url, urls[0].first);
    }
  }
}

static void check_link(const td::string &url, const td::string &expected) {
  auto result = td::LinkManager::check_link(url);
  if (result.is_ok()) {
    ASSERT_STREQ(expected, result.ok());
  } else {
    ASSERT_TRUE(expected.empty());
  }

  check_find_urls(url, result.is_ok());
}

TEST(Link, check_link) {
  check_link("sftp://google.com", "");
  check_link("tg://google_com", "tg://google_com/");
  check_link("tOn://google", "ton://google/");
  check_link("httP://google.com?1#tes", "http://google.com/?1#tes");
  check_link("httPs://google.com/?1#tes", "https://google.com/?1#tes");
  check_link("http://google.com:0", "");
  check_link("http://google.com:0000000001", "http://google.com:1/");
  check_link("http://google.com:-1", "");
  check_link("tg://google?1#tes", "tg://google?1#tes");
  check_link("tg://google/?1#tes", "tg://google?1#tes");
  check_link("TG:_", "tg://_/");
  check_link("sftp://google.com", "");
  check_link("sftp://google.com", "");
  check_link("http:google.com", "");
  check_link("tg://http://google.com", "");
  check_link("tg:http://google.com", "");
  check_link("tg:https://google.com", "");
  check_link("tg:test@google.com", "");
  check_link("tg:google.com:80", "");
  check_link("tg:google-com", "tg://google-com/");
  check_link("tg:google.com", "");
  check_link("tg:google.com:0", "");
  check_link("tg:google.com:a", "");
  check_link("tg:[2001:db8:0:0:0:ff00:42:8329]", "");
  check_link("tg:127.0.0.1", "");
  check_link("http://[2001:db8:0:0:0:ff00:42:8329]", "http://[2001:db8:0:0:0:ff00:42:8329]/");
  check_link("http://localhost", "");
  check_link("http://..", "http://../");
  check_link("..", "http://../");
  check_link("https://.", "");
}

static void parse_internal_link(const td::string &url, td::td_api::object_ptr<td::td_api::InternalLinkType> expected) {
  auto result = td::LinkManager::parse_internal_link(url);
  if (result != nullptr) {
    auto object = result->get_internal_link_type_object();
    if (object->get_id() == td::td_api::internalLinkTypeMessageDraft::ID) {
      static_cast<td::td_api::internalLinkTypeMessageDraft *>(object.get())->text_->entities_.clear();
    }
    ASSERT_STREQ(url + " " + to_string(expected), url + " " + to_string(object));
  } else {
    ASSERT_TRUE(expected == nullptr);
  }

  check_find_urls(url, result != nullptr);
}

TEST(Link, parse_internal_link) {
  auto active_sessions = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeActiveSessions>();
  };
  auto authentication_code = [](const td::string &code) {
    return td::td_api::make_object<td::td_api::internalLinkTypeAuthenticationCode>(code);
  };
  auto background = [](const td::string &background_name) {
    return td::td_api::make_object<td::td_api::internalLinkTypeBackground>(background_name);
  };
  auto bot_start = [](const td::string &bot_username, const td::string &start_parameter) {
    return td::td_api::make_object<td::td_api::internalLinkTypeBotStart>(bot_username, start_parameter);
  };
  auto bot_start_in_group = [](const td::string &bot_username, const td::string &start_parameter) {
    return td::td_api::make_object<td::td_api::internalLinkTypeBotStartInGroup>(bot_username, start_parameter);
  };
  auto change_phone_number = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeChangePhoneNumber>();
  };
  auto chat_invite = [](const td::string &hash) {
    return td::td_api::make_object<td::td_api::internalLinkTypeChatInvite>("tg:join?invite=" + hash);
  };
  auto filter_settings = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeFilterSettings>();
  };
  auto game = [](const td::string &bot_username, const td::string &game_short_name) {
    return td::td_api::make_object<td::td_api::internalLinkTypeGame>(bot_username, game_short_name);
  };
  auto language_pack = [](const td::string &language_pack_name) {
    return td::td_api::make_object<td::td_api::internalLinkTypeLanguagePack>(language_pack_name);
  };
  auto language_settings = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeLanguageSettings>();
  };
  auto message = [](const td::string &url) {
    return td::td_api::make_object<td::td_api::internalLinkTypeMessage>(url);
  };
  auto message_draft = [](td::string text, bool contains_url) {
    auto formatted_text = td::td_api::make_object<td::td_api::formattedText>();
    formatted_text->text_ = std::move(text);
    return td::td_api::make_object<td::td_api::internalLinkTypeMessageDraft>(std::move(formatted_text), contains_url);
  };
  auto passport_data_request = [](td::int32 bot_user_id, const td::string &scope, const td::string &public_key,
                                  const td::string &nonce, const td::string &callback_url) {
    return td::td_api::make_object<td::td_api::internalLinkTypePassportDataRequest>(bot_user_id, scope, public_key,
                                                                                    nonce, callback_url);
  };
  auto phone_number_confirmation = [](const td::string &hash, const td::string &phone_number) {
    return td::td_api::make_object<td::td_api::internalLinkTypePhoneNumberConfirmation>(hash, phone_number);
  };
  auto privacy_and_security_settings = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypePrivacyAndSecuritySettings>();
  };
  auto proxy_mtproto = [](const td::string &server, td::int32 port, const td::string &secret) {
    return td::td_api::make_object<td::td_api::internalLinkTypeProxy>(
        server, port, td::td_api::make_object<td::td_api::proxyTypeMtproto>(secret));
  };
  auto proxy_socks = [](const td::string &server, td::int32 port, const td::string &username,
                        const td::string &password) {
    return td::td_api::make_object<td::td_api::internalLinkTypeProxy>(
        server, port, td::td_api::make_object<td::td_api::proxyTypeSocks5>(username, password));
  };
  auto public_chat = [](const td::string &chat_username) {
    return td::td_api::make_object<td::td_api::internalLinkTypePublicChat>(chat_username);
  };
  auto qr_code_authentication = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeQrCodeAuthentication>();
  };
  auto settings = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeSettings>();
  };
  auto sticker_set = [](const td::string &sticker_set_name) {
    return td::td_api::make_object<td::td_api::internalLinkTypeStickerSet>(sticker_set_name);
  };
  auto theme = [](const td::string &theme_name) {
    return td::td_api::make_object<td::td_api::internalLinkTypeTheme>(theme_name);
  };
  auto theme_settings = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeThemeSettings>();
  };
  auto unknown_deep_link = [](const td::string &link) {
    return td::td_api::make_object<td::td_api::internalLinkTypeUnknownDeepLink>(link);
  };
  auto unsupported_proxy = [] {
    return td::td_api::make_object<td::td_api::internalLinkTypeUnsupportedProxy>();
  };
  auto user_phone_number = [](const td::string &phone_number) {
    return td::td_api::make_object<td::td_api::internalLinkTypeUserPhoneNumber>(phone_number);
  };
  auto video_chat = [](const td::string &chat_username, const td::string &invite_hash, bool is_live_stream) {
    return td::td_api::make_object<td::td_api::internalLinkTypeVideoChat>(chat_username, invite_hash, is_live_stream);
  };

  parse_internal_link("t.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("telegram.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("telegram.dog/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("www.t.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("www%2etelegram.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("www%2Etelegram.dog/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("www%252Etelegram.dog/levlam/1", nullptr);
  parse_internal_link("www.t.me/s/s/s/s/s/joinchat/1", chat_invite("1"));
  parse_internal_link("www.t.me/s/%73/%73/s/%73/joinchat/1", chat_invite("1"));
  parse_internal_link("http://t.me/s/s/s/s/s/s/s/s/s/s/s/s/s/s/s/s/s/joinchat/1", chat_invite("1"));
  parse_internal_link("http://t.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("https://t.me/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("hTtp://www.t.me:443/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("httPs://t.me:80/levlam/1", message("tg:resolve?domain=levlam&post=1"));
  parse_internal_link("https://t.me:200/levlam/1", nullptr);
  parse_internal_link("http:t.me/levlam/1", nullptr);
  parse_internal_link("t.dog/levlam/1", nullptr);
  parse_internal_link("t.m/levlam/1", nullptr);
  parse_internal_link("t.men/levlam/1", nullptr);

  parse_internal_link("tg:resolve?domain=username&post=12345&single",
                      message("tg:resolve?domain=username&post=12345&single"));
  parse_internal_link("tg:resolve?domain=user%31name&post=%312345&single&comment=456&t=789&single&thread=123%20%31",
                      message("tg:resolve?domain=user1name&post=12345&single&thread=123%201&comment=456&t=789"));
  parse_internal_link("TG://resolve?domain=username&post=12345&single&voicechat=aasd",
                      message("tg:resolve?domain=username&post=12345&single"));
  parse_internal_link("TG://test@resolve?domain=username&post=12345&single", nullptr);
  parse_internal_link("tg:resolve:80?domain=username&post=12345&single", nullptr);
  parse_internal_link("tg:http://resolve?domain=username&post=12345&single", nullptr);
  parse_internal_link("tg:https://resolve?domain=username&post=12345&single", nullptr);
  parse_internal_link("tg:resolve?domain=&post=12345&single",
                      unknown_deep_link("tg://resolve?domain=&post=12345&single"));
  parse_internal_link("tg:resolve?domain=telegram&post=&single", public_chat("telegram"));
  parse_internal_link("tg:resolve?domain=123456&post=&single",
                      unknown_deep_link("tg://resolve?domain=123456&post=&single"));

  parse_internal_link("tg:resolve?phone=1", user_phone_number("1"));
  parse_internal_link("tg:resolve?phone=123456", user_phone_number("123456"));
  parse_internal_link("tg:resolve?phone=01234567890123456789012345678912",
                      user_phone_number("01234567890123456789012345678912"));
  parse_internal_link("tg:resolve?phone=012345678901234567890123456789123",
                      unknown_deep_link("tg://resolve?phone=012345678901234567890123456789123"));
  parse_internal_link("tg:resolve?phone=", unknown_deep_link("tg://resolve?phone="));
  parse_internal_link("tg:resolve?phone=+123", unknown_deep_link("tg://resolve?phone=+123"));
  parse_internal_link("tg:resolve?phone=123456 ", unknown_deep_link("tg://resolve?phone=123456 "));

  parse_internal_link("t.me/username/12345?single", message("tg:resolve?domain=username&post=12345&single"));
  parse_internal_link("t.me/username/12345?asdasd", message("tg:resolve?domain=username&post=12345"));
  parse_internal_link("t.me/username/12345", message("tg:resolve?domain=username&post=12345"));
  parse_internal_link("t.me/username/12345/", message("tg:resolve?domain=username&post=12345"));
  parse_internal_link("t.me/username/12345#asdasd", message("tg:resolve?domain=username&post=12345"));
  parse_internal_link("t.me/username/12345//?voicechat=&single",
                      message("tg:resolve?domain=username&post=12345&single"));
  parse_internal_link("t.me/username/12345/asdasd//asd/asd/asd/?single",
                      message("tg:resolve?domain=username&post=12345&single"));
  parse_internal_link("t.me/username/1asdasdas/asdasd//asd/asd/asd/?single",
                      message("tg:resolve?domain=username&post=1&single"));
  parse_internal_link("t.me/username/asd", public_chat("username"));
  parse_internal_link("t.me/username/0", public_chat("username"));
  parse_internal_link("t.me/username/-12345", public_chat("username"));
  parse_internal_link("t.me//12345?single", nullptr);
  parse_internal_link("https://telegram.dog/telegram/?single", public_chat("telegram"));

  parse_internal_link("tg:privatepost?domain=username/12345&single",
                      unknown_deep_link("tg://privatepost?domain=username/12345&single"));
  parse_internal_link("tg:privatepost?channel=username/12345&single",
                      unknown_deep_link("tg://privatepost?channel=username/12345&single"));
  parse_internal_link("tg:privatepost?channel=username&post=12345",
                      message("tg:privatepost?channel=username&post=12345"));

  parse_internal_link("t.me/c/12345?single", nullptr);
  parse_internal_link("t.me/c/1/c?single", nullptr);
  parse_internal_link("t.me/c/c/1?single", nullptr);
  parse_internal_link("t.me/c//1?single", nullptr);
  parse_internal_link("t.me/c/12345/123", message("tg:privatepost?channel=12345&post=123"));
  parse_internal_link("t.me/c/12345/123?single", message("tg:privatepost?channel=12345&post=123&single"));
  parse_internal_link("t.me/c/12345/123/asd/asd////?single", message("tg:privatepost?channel=12345&post=123&single"));
  parse_internal_link("t.me/c/%312345/%3123?comment=456&t=789&single&thread=123%20%31",
                      message("tg:privatepost?channel=12345&post=123&single&thread=123%201&comment=456&t=789"));

  parse_internal_link("tg:bg?color=111111#asdasd", background("111111"));
  parse_internal_link("tg:bg?color=11111%31", background("111111"));
  parse_internal_link("tg:bg?color=11111%20", background("11111%20"));
  parse_internal_link("tg:bg?gradient=111111-222222", background("111111-222222"));
  parse_internal_link("tg:bg?rotation=180%20&gradient=111111-222222%20",
                      background("111111-222222%20?rotation=180%20"));
  parse_internal_link("tg:bg?gradient=111111~222222", background("111111~222222"));
  parse_internal_link("tg:bg?gradient=abacaba", background("abacaba"));
  parse_internal_link("tg:bg?slug=111111~222222#asdasd", background("111111~222222"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12&text=1", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12&mode=1", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=test&mode=12&rotation=4&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3&rotation=4"));
  parse_internal_link("tg:bg?mode=12&&slug=test&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3"));
  parse_internal_link("tg:bg?mode=12&intensity=2&bg_color=3",
                      unknown_deep_link("tg://bg?mode=12&intensity=2&bg_color=3"));

  parse_internal_link("tg:bg?color=111111#asdasd", background("111111"));
  parse_internal_link("tg:bg?color=11111%31", background("111111"));
  parse_internal_link("tg:bg?color=11111%20", background("11111%20"));
  parse_internal_link("tg:bg?gradient=111111-222222", background("111111-222222"));
  parse_internal_link("tg:bg?rotation=180%20&gradient=111111-222222%20",
                      background("111111-222222%20?rotation=180%20"));
  parse_internal_link("tg:bg?gradient=111111~222222", background("111111~222222"));
  parse_internal_link("tg:bg?gradient=abacaba", background("abacaba"));
  parse_internal_link("tg:bg?slug=111111~222222#asdasd", background("111111~222222"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12&text=1", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=111111~222222&mode=12&mode=1", background("111111~222222?mode=12"));
  parse_internal_link("tg:bg?slug=test&mode=12&rotation=4&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3&rotation=4"));
  parse_internal_link("tg:bg?mode=12&&slug=test&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3"));
  parse_internal_link("tg:bg?mode=12&intensity=2&bg_color=3",
                      unknown_deep_link("tg://bg?mode=12&intensity=2&bg_color=3"));

  parse_internal_link("%54.me/bg/111111#asdasd", background("111111"));
  parse_internal_link("t.me/bg/11111%31", background("111111"));
  parse_internal_link("t.me/bg/11111%20", background("11111%20"));
  parse_internal_link("t.me/bg/111111-222222", background("111111-222222"));
  parse_internal_link("t.me/bg/111111-222222%20?rotation=180%20", background("111111-222222%20?rotation=180%20"));
  parse_internal_link("t.me/bg/111111~222222", background("111111~222222"));
  parse_internal_link("t.me/bg/abacaba", background("abacaba"));
  parse_internal_link("t.me/Bg/abacaba", public_chat("Bg"));
  parse_internal_link("t.me/bg/111111~222222#asdasd", background("111111~222222"));
  parse_internal_link("t.me/bg/111111~222222?mode=12", background("111111~222222?mode=12"));
  parse_internal_link("t.me/bg/111111~222222?mode=12&text=1", background("111111~222222?mode=12"));
  parse_internal_link("t.me/bg/111111~222222?mode=12&mode=1", background("111111~222222?mode=12"));
  parse_internal_link("t.me/bg/test?mode=12&rotation=4&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3&rotation=4"));
  parse_internal_link("t.me/%62g/test/?mode=12&&&intensity=2&bg_color=3",
                      background("test?mode=12&intensity=2&bg_color=3"));
  parse_internal_link("t.me/bg//", nullptr);
  parse_internal_link("t.me/bg/%20/", background("%20"));
  parse_internal_link("t.me/bg/", nullptr);

  parse_internal_link("tg:share?url=google.com&text=text#asdasd", message_draft("google.com\ntext", true));
  parse_internal_link("tg:share?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("tg:share?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("tg:msg_url?url=google.com&text=text", message_draft("google.com\ntext", true));
  parse_internal_link("tg:msg_url?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("tg:msg_url?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("tg:msg?url=google.com&text=text", message_draft("google.com\ntext", true));
  parse_internal_link("tg:msg?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("tg:msg?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("tg:msg?url=&text=\n\n\n\n\n\n\n\n", nullptr);
  parse_internal_link("tg:msg?url=%20\n&text=", nullptr);
  parse_internal_link("tg:msg?url=%20\n&text=google.com", message_draft("google.com", false));
  parse_internal_link("tg:msg?url=@&text=", message_draft(" @", false));
  parse_internal_link("tg:msg?url=&text=@", message_draft(" @", false));
  parse_internal_link("tg:msg?url=@&text=@", message_draft(" @\n@", true));
  parse_internal_link("tg:msg?url=%FF&text=1", nullptr);

  parse_internal_link("https://t.me/share?url=google.com&text=text#asdasd", message_draft("google.com\ntext", true));
  parse_internal_link("https://t.me/share?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("https://t.me/share?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=google.com&text=text", message_draft("google.com\ntext", true));
  parse_internal_link("https://t.me/msg?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=google.com&text=text", message_draft("google.com\ntext", true));
  parse_internal_link("https://t.me/msg?url=google.com&text=", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=&text=google.com", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=&text=\n\n\n\n\n\n\n\n", nullptr);
  parse_internal_link("https://t.me/msg?url=%20%0A&text=", nullptr);
  parse_internal_link("https://t.me/msg?url=%20%0A&text=google.com", message_draft("google.com", false));
  parse_internal_link("https://t.me/msg?url=@&text=", message_draft(" @", false));
  parse_internal_link("https://t.me/msg?url=&text=@", message_draft(" @", false));
  parse_internal_link("https://t.me/msg?url=@&text=@", message_draft(" @\n@", true));
  parse_internal_link("https://t.me/msg?url=%FF&text=1", nullptr);

  parse_internal_link("tg:login?codec=12345", unknown_deep_link("tg://login?codec=12345"));
  parse_internal_link("tg:login", unknown_deep_link("tg://login"));
  parse_internal_link("tg:login?code=abacaba", authentication_code("abacaba"));
  parse_internal_link("tg:login?code=123456", authentication_code("123456"));

  parse_internal_link("t.me/login?codec=12345", nullptr);
  parse_internal_link("t.me/login", nullptr);
  parse_internal_link("t.me/login/", nullptr);
  parse_internal_link("t.me/login//12345", nullptr);
  parse_internal_link("t.me/login?/12345", nullptr);
  parse_internal_link("t.me/login/?12345", nullptr);
  parse_internal_link("t.me/login/#12345", nullptr);
  parse_internal_link("t.me/login/abacaba", authentication_code("abacaba"));
  parse_internal_link("t.me/login/aba%20aba", authentication_code("aba aba"));
  parse_internal_link("t.me/login/123456a", authentication_code("123456a"));
  parse_internal_link("t.me/login/12345678901", authentication_code("12345678901"));
  parse_internal_link("t.me/login/123456", authentication_code("123456"));
  parse_internal_link("t.me/login/123456/123123/12/31/a/s//21w/?asdas#test", authentication_code("123456"));

  parse_internal_link("tg:login?token=abacaba", qr_code_authentication());
  parse_internal_link("tg:login?token=", unknown_deep_link("tg://login?token="));

  parse_internal_link("t.me/joinchat?invite=abcdef", nullptr);
  parse_internal_link("t.me/joinchat", nullptr);
  parse_internal_link("t.me/joinchat/", nullptr);
  parse_internal_link("t.me/joinchat//abcdef", nullptr);
  parse_internal_link("t.me/joinchat?/abcdef", nullptr);
  parse_internal_link("t.me/joinchat/?abcdef", nullptr);
  parse_internal_link("t.me/joinchat/#abcdef", nullptr);
  parse_internal_link("t.me/joinchat/abacaba", chat_invite("abacaba"));
  parse_internal_link("t.me/joinchat/aba%20aba", chat_invite("aba%20aba"));
  parse_internal_link("t.me/joinchat/aba%30aba", chat_invite("aba0aba"));
  parse_internal_link("t.me/joinchat/123456a", chat_invite("123456a"));
  parse_internal_link("t.me/joinchat/12345678901", chat_invite("12345678901"));
  parse_internal_link("t.me/joinchat/123456", chat_invite("123456"));
  parse_internal_link("t.me/joinchat/123456/123123/12/31/a/s//21w/?asdas#test", chat_invite("123456"));

  parse_internal_link("t.me/+?invite=abcdef", nullptr);
  parse_internal_link("t.me/+a", chat_invite("a"));
  parse_internal_link("t.me/+", nullptr);
  parse_internal_link("t.me/+/abcdef", nullptr);
  parse_internal_link("t.me/ ?/abcdef", nullptr);
  parse_internal_link("t.me/+?abcdef", nullptr);
  parse_internal_link("t.me/+#abcdef", nullptr);
  parse_internal_link("t.me/ abacaba", chat_invite("abacaba"));
  parse_internal_link("t.me/+aba%20aba", chat_invite("aba%20aba"));
  parse_internal_link("t.me/+aba%30aba", chat_invite("aba0aba"));
  parse_internal_link("t.me/+123456a", chat_invite("123456a"));
  parse_internal_link("t.me/%2012345678901", user_phone_number("12345678901"));
  parse_internal_link("t.me/+123456", user_phone_number("123456"));
  parse_internal_link("t.me/ 123456/123123/12/31/a/s//21w/?asdas#test", user_phone_number("123456"));
  parse_internal_link("t.me/ /123456/123123/12/31/a/s//21w/?asdas#test", nullptr);

  parse_internal_link("tg:join?invite=abcdef", chat_invite("abcdef"));
  parse_internal_link("tg:join?invite=abc%20def", chat_invite("abc%20def"));
  parse_internal_link("tg://join?invite=abc%30def", chat_invite("abc0def"));
  parse_internal_link("tg:join?invite=", unknown_deep_link("tg://join?invite="));

  parse_internal_link("t.me/addstickers?set=abcdef", nullptr);
  parse_internal_link("t.me/addstickers", nullptr);
  parse_internal_link("t.me/addstickers/", nullptr);
  parse_internal_link("t.me/addstickers//abcdef", nullptr);
  parse_internal_link("t.me/addstickers?/abcdef", nullptr);
  parse_internal_link("t.me/addstickers/?abcdef", nullptr);
  parse_internal_link("t.me/addstickers/#abcdef", nullptr);
  parse_internal_link("t.me/addstickers/abacaba", sticker_set("abacaba"));
  parse_internal_link("t.me/addstickers/aba%20aba", sticker_set("aba aba"));
  parse_internal_link("t.me/addstickers/123456a", sticker_set("123456a"));
  parse_internal_link("t.me/addstickers/12345678901", sticker_set("12345678901"));
  parse_internal_link("t.me/addstickers/123456", sticker_set("123456"));
  parse_internal_link("t.me/addstickers/123456/123123/12/31/a/s//21w/?asdas#test", sticker_set("123456"));

  parse_internal_link("tg:addstickers?set=abcdef", sticker_set("abcdef"));
  parse_internal_link("tg:addstickers?set=abc%30ef", sticker_set("abc0ef"));
  parse_internal_link("tg://addstickers?set=", unknown_deep_link("tg://addstickers?set="));

  parse_internal_link("t.me/confirmphone?hash=abc%30ef&phone=", nullptr);
  parse_internal_link("t.me/confirmphone/123456/123123/12/31/a/s//21w/?hash=abc%30ef&phone=123456789",
                      phone_number_confirmation("abc0ef", "123456789"));
  parse_internal_link("t.me/confirmphone?hash=abc%30ef&phone=123456789",
                      phone_number_confirmation("abc0ef", "123456789"));

  parse_internal_link("tg:confirmphone?hash=abc%30ef&phone=",
                      unknown_deep_link("tg://confirmphone?hash=abc%30ef&phone="));
  parse_internal_link("tg:confirmphone?hash=abc%30ef&phone=123456789",
                      phone_number_confirmation("abc0ef", "123456789"));
  parse_internal_link("tg://confirmphone?hash=123&phone=123456789123456789",
                      phone_number_confirmation("123", "123456789123456789"));
  parse_internal_link("tg://confirmphone?hash=&phone=123456789123456789",
                      unknown_deep_link("tg://confirmphone?hash=&phone=123456789123456789"));
  parse_internal_link("tg://confirmphone?hash=123456789123456789&phone=",
                      unknown_deep_link("tg://confirmphone?hash=123456789123456789&phone="));

  parse_internal_link("t.me/setlanguage?lang=abcdef", nullptr);
  parse_internal_link("t.me/setlanguage", nullptr);
  parse_internal_link("t.me/setlanguage/", nullptr);
  parse_internal_link("t.me/setlanguage//abcdef", nullptr);
  parse_internal_link("t.me/setlanguage?/abcdef", nullptr);
  parse_internal_link("t.me/setlanguage/?abcdef", nullptr);
  parse_internal_link("t.me/setlanguage/#abcdef", nullptr);
  parse_internal_link("t.me/setlanguage/abacaba", language_pack("abacaba"));
  parse_internal_link("t.me/setlanguage/aba%20aba", language_pack("aba aba"));
  parse_internal_link("t.me/setlanguage/123456a", language_pack("123456a"));
  parse_internal_link("t.me/setlanguage/12345678901", language_pack("12345678901"));
  parse_internal_link("t.me/setlanguage/123456", language_pack("123456"));
  parse_internal_link("t.me/setlanguage/123456/123123/12/31/a/s//21w/?asdas#test", language_pack("123456"));

  parse_internal_link("tg:setlanguage?lang=abcdef", language_pack("abcdef"));
  parse_internal_link("tg:setlanguage?lang=abc%30ef", language_pack("abc0ef"));
  parse_internal_link("tg://setlanguage?lang=", unknown_deep_link("tg://setlanguage?lang="));

  parse_internal_link("t.me/addtheme?slug=abcdef", nullptr);
  parse_internal_link("t.me/addtheme", nullptr);
  parse_internal_link("t.me/addtheme/", nullptr);
  parse_internal_link("t.me/addtheme//abcdef", nullptr);
  parse_internal_link("t.me/addtheme?/abcdef", nullptr);
  parse_internal_link("t.me/addtheme/?abcdef", nullptr);
  parse_internal_link("t.me/addtheme/#abcdef", nullptr);
  parse_internal_link("t.me/addtheme/abacaba", theme("abacaba"));
  parse_internal_link("t.me/addtheme/aba%20aba", theme("aba aba"));
  parse_internal_link("t.me/addtheme/123456a", theme("123456a"));
  parse_internal_link("t.me/addtheme/12345678901", theme("12345678901"));
  parse_internal_link("t.me/addtheme/123456", theme("123456"));
  parse_internal_link("t.me/addtheme/123456/123123/12/31/a/s//21w/?asdas#test", theme("123456"));

  parse_internal_link("tg:addtheme?slug=abcdef", theme("abcdef"));
  parse_internal_link("tg:addtheme?slug=abc%30ef", theme("abc0ef"));
  parse_internal_link("tg://addtheme?slug=", unknown_deep_link("tg://addtheme?slug="));

  parse_internal_link("t.me/proxy?server=1.2.3.4&port=80&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("1.2.3.4", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("t.me/proxy?server=1.2.3.4&port=80adasdas&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("1.2.3.4", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("t.me/proxy?server=1.2.3.4&port=adasdas&secret=1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("t.me/proxy?server=1.2.3.4&port=65536&secret=1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=", unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=12", unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("google.com", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=dd1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("google.com", 80, "dd1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=de1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=ee1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=ee1234567890abcdef1234567890ABCDEF0",
                      unsupported_proxy());
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=ee1234567890abcdef1234567890ABCDEF%30%30",
                      proxy_mtproto("google.com", 80, "ee1234567890abcdef1234567890ABCDEF00"));
  parse_internal_link(
      "t.me/proxy?server=google.com&port=8%30&secret=ee1234567890abcdef1234567890ABCDEF010101010101010101",
      proxy_mtproto("google.com", 80, "ee1234567890abcdef1234567890ABCDEF010101010101010101"));
  parse_internal_link("t.me/proxy?server=google.com&port=8%30&secret=7tAAAAAAAAAAAAAAAAAAAAAAAAcuZ29vZ2xlLmNvbQ",
                      proxy_mtproto("google.com", 80, "7tAAAAAAAAAAAAAAAAAAAAAAAAcuZ29vZ2xlLmNvbQ"));

  parse_internal_link("tg:proxy?server=1.2.3.4&port=80&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("1.2.3.4", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("tg:proxy?server=1.2.3.4&port=80adasdas&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("1.2.3.4", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("tg:proxy?server=1.2.3.4&port=adasdas&secret=1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("tg:proxy?server=1.2.3.4&port=65536&secret=1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());
  parse_internal_link("tg:proxy?server=google.com&port=8%30&secret=1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("google.com", 80, "1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("tg:proxy?server=google.com&port=8%30&secret=dd1234567890abcdef1234567890ABCDEF",
                      proxy_mtproto("google.com", 80, "dd1234567890abcdef1234567890ABCDEF"));
  parse_internal_link("tg:proxy?server=google.com&port=8%30&secret=de1234567890abcdef1234567890ABCDEF",
                      unsupported_proxy());

  parse_internal_link("t.me/socks?server=1.2.3.4&port=80", proxy_socks("1.2.3.4", 80, "", ""));
  parse_internal_link("t.me/socks?server=1.2.3.4&port=80adasdas", proxy_socks("1.2.3.4", 80, "", ""));
  parse_internal_link("t.me/socks?server=1.2.3.4&port=adasdas", unsupported_proxy());
  parse_internal_link("t.me/socks?server=1.2.3.4&port=65536", unsupported_proxy());
  parse_internal_link("t.me/socks?server=google.com&port=8%30", proxy_socks("google.com", 80, "", ""));
  parse_internal_link("t.me/socks?server=google.com&port=8%30&user=1&pass=", proxy_socks("google.com", 80, "1", ""));
  parse_internal_link("t.me/socks?server=google.com&port=8%30&user=&pass=2", proxy_socks("google.com", 80, "", "2"));
  parse_internal_link("t.me/socks?server=google.com&port=80&user=1&pass=2", proxy_socks("google.com", 80, "1", "2"));

  parse_internal_link("tg:socks?server=1.2.3.4&port=80", proxy_socks("1.2.3.4", 80, "", ""));
  parse_internal_link("tg:socks?server=1.2.3.4&port=80adasdas", proxy_socks("1.2.3.4", 80, "", ""));
  parse_internal_link("tg:socks?server=1.2.3.4&port=adasdas", unsupported_proxy());
  parse_internal_link("tg:socks?server=1.2.3.4&port=65536", unsupported_proxy());
  parse_internal_link("tg:socks?server=google.com&port=8%30", proxy_socks("google.com", 80, "", ""));
  parse_internal_link("tg:socks?server=google.com&port=8%30&user=1&pass=", proxy_socks("google.com", 80, "1", ""));
  parse_internal_link("tg:socks?server=google.com&port=8%30&user=&pass=2", proxy_socks("google.com", 80, "", "2"));
  parse_internal_link("tg:socks?server=google.com&port=80&user=1&pass=2", proxy_socks("google.com", 80, "1", "2"));

  parse_internal_link("tg:resolve?domain=username&voice%63hat=aasdasd", video_chat("username", "aasdasd", false));
  parse_internal_link("tg:resolve?domain=username&video%63hat=aasdasd", video_chat("username", "aasdasd", false));
  parse_internal_link("tg:resolve?domain=username&livestream=aasdasd", video_chat("username", "aasdasd", true));
  parse_internal_link("TG://resolve?domain=username&voicechat=", video_chat("username", "", false));
  parse_internal_link("TG://test@resolve?domain=username&voicechat=", nullptr);
  parse_internal_link("tg:resolve:80?domain=username&voicechat=", nullptr);
  parse_internal_link("tg:http://resolve?domain=username&voicechat=", nullptr);
  parse_internal_link("tg:https://resolve?domain=username&voicechat=", nullptr);
  parse_internal_link("tg:resolve?domain=&voicechat=", unknown_deep_link("tg://resolve?domain=&voicechat="));
  parse_internal_link("tg:resolve?domain=telegram&&&&&&&voicechat=%30", video_chat("telegram", "0", false));

  parse_internal_link("t.me/username/0/a//s/as?voicechat=", video_chat("username", "", false));
  parse_internal_link("t.me/username/0/a//s/as?videochat=2", video_chat("username", "2", false));
  parse_internal_link("t.me/username/0/a//s/as?livestream=3", video_chat("username", "3", true));
  parse_internal_link("t.me/username/aasdas?test=1&voicechat=#12312", video_chat("username", "", false));
  parse_internal_link("t.me/username/0?voicechat=", video_chat("username", "", false));
  parse_internal_link("t.me/username/-1?voicechat=asdasd", video_chat("username", "asdasd", false));
  parse_internal_link("t.me/username?voicechat=", video_chat("username", "", false));
  parse_internal_link("t.me/username#voicechat=asdas", public_chat("username"));
  parse_internal_link("t.me//username?voicechat=", nullptr);
  parse_internal_link("https://telegram.dog/tele%63ram?voi%63e%63hat=t%63st", video_chat("telecram", "tcst", false));

  parse_internal_link("tg:resolve?domain=username&start=aasdasd", bot_start("username", "aasdasd"));
  parse_internal_link("TG://resolve?domain=username&start=", bot_start("username", ""));
  parse_internal_link("TG://test@resolve?domain=username&start=", nullptr);
  parse_internal_link("tg:resolve:80?domain=username&start=", nullptr);
  parse_internal_link("tg:http://resolve?domain=username&start=", nullptr);
  parse_internal_link("tg:https://resolve?domain=username&start=", nullptr);
  parse_internal_link("tg:resolve?domain=&start=", unknown_deep_link("tg://resolve?domain=&start="));
  parse_internal_link("tg:resolve?domain=telegram&&&&&&&start=%30", bot_start("telegram", "0"));

  parse_internal_link("t.me/username/0/a//s/as?start=", bot_start("username", ""));
  parse_internal_link("t.me/username/aasdas?test=1&start=#12312", bot_start("username", ""));
  parse_internal_link("t.me/username/0?start=", bot_start("username", ""));
  parse_internal_link("t.me/username/-1?start=asdasd", bot_start("username", "asdasd"));
  parse_internal_link("t.me/username?start=", bot_start("username", ""));
  parse_internal_link("t.me/username#start=asdas", public_chat("username"));
  parse_internal_link("t.me//username?start=", nullptr);
  parse_internal_link("https://telegram.dog/tele%63ram?start=t%63st", bot_start("telecram", "tcst"));

  parse_internal_link("tg:resolve?domain=username&startgroup=aasdasd", bot_start_in_group("username", "aasdasd"));
  parse_internal_link("TG://resolve?domain=username&startgroup=", bot_start_in_group("username", ""));
  parse_internal_link("TG://test@resolve?domain=username&startgroup=", nullptr);
  parse_internal_link("tg:resolve:80?domain=username&startgroup=", nullptr);
  parse_internal_link("tg:http://resolve?domain=username&startgroup=", nullptr);
  parse_internal_link("tg:https://resolve?domain=username&startgroup=", nullptr);
  parse_internal_link("tg:resolve?domain=&startgroup=", unknown_deep_link("tg://resolve?domain=&startgroup="));
  parse_internal_link("tg:resolve?domain=telegram&&&&&&&startgroup=%30", bot_start_in_group("telegram", "0"));

  parse_internal_link("t.me/username/0/a//s/as?startgroup=", bot_start_in_group("username", ""));
  parse_internal_link("t.me/username/aasdas?test=1&startgroup=#12312", bot_start_in_group("username", ""));
  parse_internal_link("t.me/username/0?startgroup=", bot_start_in_group("username", ""));
  parse_internal_link("t.me/username/-1?startgroup=asdasd", bot_start_in_group("username", "asdasd"));
  parse_internal_link("t.me/username?startgroup=", bot_start_in_group("username", ""));
  parse_internal_link("t.me/username#startgroup=asdas", public_chat("username"));
  parse_internal_link("t.me//username?startgroup=", nullptr);
  parse_internal_link("https://telegram.dog/tele%63ram?startgroup=t%63st", bot_start_in_group("telecram", "tcst"));

  parse_internal_link("tg:resolve?domain=username&game=aasdasd", game("username", "aasdasd"));
  parse_internal_link("TG://resolve?domain=username&game=", public_chat("username"));
  parse_internal_link("TG://test@resolve?domain=username&game=asd", nullptr);
  parse_internal_link("tg:resolve:80?domain=username&game=asd", nullptr);
  parse_internal_link("tg:http://resolve?domain=username&game=asd", nullptr);
  parse_internal_link("tg:https://resolve?domain=username&game=asd", nullptr);
  parse_internal_link("tg:resolve?domain=&game=asd", unknown_deep_link("tg://resolve?domain=&game=asd"));
  parse_internal_link("tg:resolve?domain=telegram&&&&&&&game=%30", game("telegram", "0"));

  parse_internal_link("t.me/username/0/a//s/as?game=asd", game("username", "asd"));
  parse_internal_link("t.me/username/aasdas?test=1&game=asd#12312", game("username", "asd"));
  parse_internal_link("t.me/username/0?game=asd", game("username", "asd"));
  parse_internal_link("t.me/username/-1?game=asdasd", game("username", "asdasd"));
  parse_internal_link("t.me/username?game=asd", game("username", "asd"));
  parse_internal_link("t.me/username?game=", public_chat("username"));
  parse_internal_link("t.me/username#game=asdas", public_chat("username"));
  parse_internal_link("t.me//username?game=asd", nullptr);
  parse_internal_link("https://telegram.dog/tele%63ram?game=t%63st", game("telecram", "tcst"));

  parse_internal_link("tg:resolve?domain=username&Game=asd", public_chat("username"));
  parse_internal_link("TG://test@resolve?domain=username", nullptr);
  parse_internal_link("tg:resolve:80?domain=username", nullptr);
  parse_internal_link("tg:http://resolve?domain=username", nullptr);
  parse_internal_link("tg:https://resolve?domain=username", nullptr);
  parse_internal_link("tg:resolve?domain=", unknown_deep_link("tg://resolve?domain="));
  parse_internal_link("tg:resolve?&&&&&&&domain=telegram", public_chat("telegram"));

  parse_internal_link("t.me/a", public_chat("a"));
  parse_internal_link("t.me/abcdefghijklmnopqrstuvwxyz123456", public_chat("abcdefghijklmnopqrstuvwxyz123456"));
  parse_internal_link("t.me/abcdefghijklmnopqrstuvwxyz1234567", nullptr);
  parse_internal_link("t.me/abcdefghijklmnop-qrstuvwxyz", nullptr);
  parse_internal_link("t.me/abcdefghijklmnop~qrstuvwxyz", nullptr);
  parse_internal_link("t.me/_asdf", nullptr);
  parse_internal_link("t.me/0asdf", nullptr);
  parse_internal_link("t.me/9asdf", nullptr);
  parse_internal_link("t.me/Aasdf", public_chat("Aasdf"));
  parse_internal_link("t.me/asdf_", nullptr);
  parse_internal_link("t.me/asdf0", public_chat("asdf0"));
  parse_internal_link("t.me/asd__fg", nullptr);
  parse_internal_link("t.me/username/0/a//s/as?gam=asd", public_chat("username"));
  parse_internal_link("t.me/username/aasdas?test=1", public_chat("username"));
  parse_internal_link("t.me/username/0", public_chat("username"));
  parse_internal_link("t.me//username", nullptr);
  parse_internal_link("https://telegram.dog/tele%63ram", public_chat("telecram"));

  parse_internal_link(
      "tg://"
      "resolve?domain=telegrampassport&bot_id=543260180&scope=%7B%22v%22%3A1%2C%22d%22%3A%5B%7B%22%22%5D%7D%5D%7D&"
      "public_key=BEGIN%20PUBLIC%20KEY%0A&nonce=b8ee&callback_url=https%3A%2F%2Fcore.telegram.org%2Fpassport%2Fexample%"
      "3Fpassport_ssid%3Db8ee&payload=nonce",
      passport_data_request(543260180, "{\"v\":1,\"d\":[{\"\"]}]}", "BEGIN PUBLIC KEY\n", "b8ee",
                            "https://core.telegram.org/passport/example?passport_ssid=b8ee"));
  parse_internal_link("tg://resolve?domain=telegrampassport&bot_id=12345&public_key=key&scope=asd&payload=nonce",
                      passport_data_request(12345, "asd", "key", "nonce", ""));
  parse_internal_link("tg://passport?bot_id=12345&public_key=key&scope=asd&payload=nonce",
                      passport_data_request(12345, "asd", "key", "nonce", ""));
  parse_internal_link("tg://passport?bot_id=0&public_key=key&scope=asd&payload=nonce",
                      unknown_deep_link("tg://passport?bot_id=0&public_key=key&scope=asd&payload=nonce"));
  parse_internal_link("tg://passport?bot_id=-1&public_key=key&scope=asd&payload=nonce",
                      unknown_deep_link("tg://passport?bot_id=-1&public_key=key&scope=asd&payload=nonce"));
  parse_internal_link("tg://passport?bot_id=12345&public_key=&scope=asd&payload=nonce",
                      unknown_deep_link("tg://passport?bot_id=12345&public_key=&scope=asd&payload=nonce"));
  parse_internal_link("tg://passport?bot_id=12345&public_key=key&scope=&payload=nonce",
                      unknown_deep_link("tg://passport?bot_id=12345&public_key=key&scope=&payload=nonce"));
  parse_internal_link("tg://passport?bot_id=12345&public_key=key&scope=asd&payload=",
                      unknown_deep_link("tg://passport?bot_id=12345&public_key=key&scope=asd&payload="));
  parse_internal_link("t.me/telegrampassport?bot_id=12345&public_key=key&scope=asd&payload=nonce",
                      public_chat("telegrampassport"));

  parse_internal_link("tg://settings", settings());
  parse_internal_link("tg://setting", unknown_deep_link("tg://setting"));
  parse_internal_link("tg://settings?asdsa?D?SADasD?asD", settings());
  parse_internal_link("tg://settings#test", settings());
  parse_internal_link("tg://settings/#test", settings());
  parse_internal_link("tg://settings/aadsa#test", settings());
  parse_internal_link("tg://settings/theme#test", settings());
  parse_internal_link("tg://settings/themes#test", theme_settings());
  parse_internal_link("tg://settings/themesa#test", settings());
  parse_internal_link("tg://settings/themes/?as#rad", theme_settings());
  parse_internal_link("tg://settings/themes/a", settings());
  parse_internal_link("tg://settings/asdsathemesasdas/devices", settings());
  parse_internal_link("tg://settings/devices", active_sessions());
  parse_internal_link("tg://settings/change_number", change_phone_number());
  parse_internal_link("tg://settings/folders", filter_settings());
  parse_internal_link("tg://settings/filters", settings());
  parse_internal_link("tg://settings/language", language_settings());
  parse_internal_link("tg://settings/privacy", privacy_and_security_settings());
}
