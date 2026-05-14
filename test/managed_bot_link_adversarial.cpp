// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

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

TEST(ManagedBotLinkAdversarial, ShortSuggestedUsernameNormalizationStillEndsWithBot) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("tg:newbot?manager=managerot&username=a");

  ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(parsed->suggested_bot_username_));
  ASSERT_EQ("abot", parsed->suggested_bot_username_);
}