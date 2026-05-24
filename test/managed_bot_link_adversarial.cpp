// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

#include <array>

namespace {

bool is_request_managed_bot_link(td::Slice url) {
  auto parsed = td::managed_bot_link_test::parse_link(url);
  if (parsed == nullptr) {
    return false;
  }
  auto object = td::managed_bot_link_test::get_internal_link_type_object(std::move(parsed));
  return object != nullptr && object->get_id() == td::td_api::internalLinkTypeRequestManagedBot::ID;
}

}  // namespace

TEST(ManagedBotLinkAdversarial, InvalidManagerUsernameWithoutSuggestedUsernameMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("t.me/newbot/0manager?name=asd"));
  ASSERT_FALSE(is_request_managed_bot_link("tg:newbot?manager=0manager&name=asd"));
}

TEST(ManagedBotLinkAdversarial, InvalidSuggestedUsernameWhenPresentMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("t.me/newbot/manager/0testbot?name=asd"));
  ASSERT_FALSE(is_request_managed_bot_link("tg:newbot?manager=managerot&username=0testbot&name=asd"));
}

TEST(ManagedBotLinkAdversarial, MalformedSuggestedUsernameMustNotBeSilentlyRewrittenDuringParsing) {
  const std::array<td::string, 4> invalid_usernames = {{"a_", "a__", "a-b", "a.bot"}};

  for (const auto &invalid_username : invalid_usernames) {
    ASSERT_FALSE(is_request_managed_bot_link(td::string("t.me/newbot/manager/") + invalid_username + "?name=asd"));
    ASSERT_FALSE(is_request_managed_bot_link(td::string("tg:newbot?manager=managerot&username=") + invalid_username +
                                             "&name=asd"));
  }
}

TEST(ManagedBotLinkAdversarial, DuplicateManagerQueryParameterMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("tg:newbot?manager=managerot&manager=evilbot&username=a&name=asd"));
}

TEST(ManagedBotLinkAdversarial, DuplicateSuggestedUsernameQueryParameterMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("tg:newbot?manager=managerot&username=a&username=b&name=asd"));
}

TEST(ManagedBotLinkAdversarial, DuplicateSuggestedNameQueryParameterMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("tg:newbot?manager=managerot&username=a&name=asd&name=evil"));
}

TEST(ManagedBotLinkAdversarial, TMeSuggestedUsernameMustFailClosedWhenNormalizationWouldOverflowLength) {
  const td::string max_length_raw_username(32, 'a');

  ASSERT_FALSE(is_request_managed_bot_link(td::string("t.me/newbot/manager/") + max_length_raw_username + "?name=asd"));
}

TEST(ManagedBotLinkAdversarial, TgSuggestedUsernameMustFailClosedWhenNormalizationWouldOverflowLength) {
  const td::string max_length_raw_username(32, 'a');

  ASSERT_FALSE(is_request_managed_bot_link(td::string("tg:newbot?manager=managerot&username=") +
                                           max_length_raw_username + "&name=asd"));
}

TEST(ManagedBotLinkAdversarial, ExtraPathSegmentMustNotProduceManagedBotLink) {
  ASSERT_FALSE(is_request_managed_bot_link("t.me/newbot/manager/a/extra?name=asd"));
}

TEST(ManagedBotLinkAdversarial, RepeatedTrailingSlashesMustCanonicalizeToManagerOnlyManagedBotLink) {
  // The shared URL parser trims trailing empty path segments, so repeated trailing slashes must not
  // create a distinct managed-bot path shape or bypass username normalization.
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("t.me/newbot/manager//?name=asd");

  ASSERT_EQ("manager", parsed->manager_bot_username_);
  ASSERT_EQ("bot", parsed->suggested_bot_username_);
  ASSERT_EQ("asd", parsed->suggested_bot_name_);
}

TEST(ManagedBotLinkAdversarial, InvalidUtf8SuggestedNameInQueryMustNotProduceManagedBotLink) {
  td::string invalid_utf8_link = "tg:newbot?manager=managerot&username=a&name=";
  invalid_utf8_link.push_back(static_cast<char>(0xC3));
  invalid_utf8_link.push_back('(');

  ASSERT_FALSE(is_request_managed_bot_link(invalid_utf8_link));
}

TEST(ManagedBotLinkAdversarial, InvalidUtf8SuggestedNameInPathMustNotProduceManagedBotLink) {
  td::string invalid_utf8_link = "t.me/newbot/manager/a?name=";
  invalid_utf8_link.push_back(static_cast<char>(0xC3));
  invalid_utf8_link.push_back('(');

  ASSERT_FALSE(is_request_managed_bot_link(invalid_utf8_link));
}

TEST(ManagedBotLinkAdversarial, ShortSuggestedUsernameNormalizationStillEndsWithBot) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("tg:newbot?manager=managerot&username=a");

  ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(parsed->suggested_bot_username_));
  ASSERT_EQ("abot", parsed->suggested_bot_username_);
}

TEST(ManagedBotLinkAdversarial, GeneratedInternalLinkMustNotLeakUnsuffixedSuggestedUsername) {
  auto built = td::managed_bot_link_test::build_request_managed_bot_link("manager", "a", "asd", true);

  ASSERT_TRUE(built.is_ok());
  ASSERT_EQ("tg://newbot?manager=manager&username=abot&name=asd", built.ok());
}

TEST(ManagedBotLinkAdversarial, GeneratedExternalLinkMustNotLeakEmptySuggestedUsername) {
  auto built = td::managed_bot_link_test::build_request_managed_bot_link("manager", "", "asd", false);

  ASSERT_TRUE(built.is_ok());
  ASSERT_EQ("https://t.me/newbot/manager/bot?name=asd", built.ok());
}

TEST(ManagedBotLinkAdversarial, GeneratedLinkMustRejectSuggestedUsernameThatOverflowsAfterNormalization) {
  const td::string max_length_raw_username(32, 'a');

  auto internal =
      td::managed_bot_link_test::build_request_managed_bot_link("manager", max_length_raw_username, "asd", true);
  auto external =
      td::managed_bot_link_test::build_request_managed_bot_link("manager", max_length_raw_username, "asd", false);

  ASSERT_TRUE(internal.is_error());
  ASSERT_TRUE(external.is_error());
}

TEST(ManagedBotLinkAdversarial,
     GeneratedLinkMustRejectMalformedSuggestedUsernameEvenIfNormalizationWouldProduceValidUsername) {
  const std::array<td::string, 4> invalid_usernames = {{"a_", "a__", "a-b", "a.bot"}};

  for (const auto &invalid_username : invalid_usernames) {
    auto internal = td::managed_bot_link_test::build_request_managed_bot_link("manager", invalid_username, "asd", true);
    auto external =
        td::managed_bot_link_test::build_request_managed_bot_link("manager", invalid_username, "asd", false);

    ASSERT_TRUE(internal.is_error());
    ASSERT_TRUE(external.is_error());
  }
}