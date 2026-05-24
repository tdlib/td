// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/LinkManager.h"

#include "td/utils/tests.h"

namespace {

TEST(MessageLinkInfoAdversarial, RejectsNonUtf8PollOptionPayloadFromBase64url) {
  // "__8" decodes into non-UTF8 bytes 0xFF 0xFF.
  auto r_info = td::LinkManager::get_message_link_info("tg://resolve?domain=username&post=12345&option=__8");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Invalid poll option identifier", r_info.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsDuplicateTaskAndOptionInTMeLinks) {
  auto r_task = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?task=17&task=18");
  ASSERT_TRUE(r_task.is_error());
  ASSERT_EQ("Duplicate checklist task identifier", r_task.error().message());

  auto r_option = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?option=Zm9v&option=YmFy");
  ASSERT_TRUE(r_option.is_error());
  ASSERT_EQ("Duplicate poll option identifier", r_option.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsExplicitlyEmptyTaskAndOptionInTMeLinks) {
  auto r_task = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?task=");
  ASSERT_TRUE(r_task.is_error());
  ASSERT_EQ("Wrong checklist task identifier", r_task.error().message());

  auto r_option = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?option=");
  ASSERT_TRUE(r_option.is_error());
  ASSERT_EQ("Invalid poll option identifier", r_option.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsExplicitlyEmptyTaskAndOptionInPrivatePostLinks) {
  auto r_task = td::LinkManager::get_message_link_info("tg://privatepost?channel=123456789&post=12345&task=");
  ASSERT_TRUE(r_task.is_error());
  ASSERT_EQ("Wrong checklist task identifier", r_task.error().message());

  auto r_option = td::LinkManager::get_message_link_info("tg://privatepost?channel=123456789&post=12345&option=");
  ASSERT_TRUE(r_option.is_error());
  ASSERT_EQ("Invalid poll option identifier", r_option.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsDuplicateChannelParameterInPrivatePostLinks) {
  auto r_info =
      td::LinkManager::get_message_link_info("tg://privatepost?channel=123456789&channel=987654321&post=12345");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate chat identifier", r_info.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsDuplicateCommentParameterInTMeLinks) {
  auto r_info = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?comment=17&comment=18");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate comment message identifier", r_info.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsDuplicateThreadParameterInTMeLinks) {
  auto r_info = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?thread=17&thread=18");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate message topic identifier", r_info.error().message());
}

TEST(MessageLinkInfoAdversarial, RejectsDuplicateTimestampParameterInTMeLinks) {
  auto r_info = td::LinkManager::get_message_link_info("https://t.me/c/123456789/12345?t=10&t=20");

  ASSERT_TRUE(r_info.is_error());
  ASSERT_EQ("Duplicate media timestamp", r_info.error().message());
}

}  // namespace