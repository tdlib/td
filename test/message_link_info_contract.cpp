// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/LinkManager.h"

#include "td/utils/tests.h"

namespace {

TEST(MessageLinkInfoContract, AcceptsCanonicalTaskAndOptionForResolveLinks) {
  auto r_info =
      td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&single&task=17&option=Zm9v");

  ASSERT_TRUE(r_info.is_ok());
  const auto &info = r_info.ok();
  ASSERT_EQ("username", info.username);
  ASSERT_EQ(17, info.todo_item_id);
  ASSERT_EQ("foo", info.poll_option_id);
  ASSERT_TRUE(info.is_single);
}

TEST(MessageLinkInfoContract, RejectsDuplicateTaskParameterInResolveLinks) {
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&task=17&task=18");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate checklist task identifier", r_info.error().message());
}

TEST(MessageLinkInfoContract, RejectsDuplicateDomainParameterInResolveLinks) {
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&domain=otheruser&post=12345");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate chat identifier", r_info.error().message());
}

TEST(MessageLinkInfoContract, RejectsDuplicatePostParameterInResolveLinks) {
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&post=12346");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate message identifier", r_info.error().message());
}

TEST(MessageLinkInfoContract, RejectsExplicitlyEmptyTaskParameterInResolveLinks) {
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&task=");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Wrong checklist task identifier", r_info.error().message());
}

TEST(MessageLinkInfoContract, RejectsDuplicateOptionParameterInResolveLinks) {
  auto r_info =
      td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&option=Zm9v&option=YmFy");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate poll option identifier", r_info.error().message());
}

TEST(MessageLinkInfoContract, RejectsExplicitlyEmptyOptionParameterInResolveLinks) {
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&option=");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Invalid poll option identifier", r_info.error().message());
}

}  // namespace