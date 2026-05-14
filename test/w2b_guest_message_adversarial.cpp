// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

bool is_contract_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (is_contract_whitespace(c)) {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(W2BGuestMessageAdversarial, LegacySelfDialogNormalizationMustNotRewriteBusinessGuestSender) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(R"(if(dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)") ==
      td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacyOutgoingExpectationMustNotIgnoreBusinessGuestViaSender) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("boolsupposed_to_be_outgoing=sender_user_id==my_id&&"
                              "!(dialog_id==my_dialog_id&&!message_id.is_scheduled());") == td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacyOutgoingExpectationMustNotCoupleBusinessViaSenderToBusinessLaneFlag) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());)") ==
      td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacyScheduledPinnedSanitizationMustNotUseDetachedLocalFlag) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("boolis_pinned=message_info.is_pinned;") == td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacyBusinessGuestPathMustNotRunTopicFixupUnconditionally) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(message->forward_info==nullptr&&has_forward_info){message->had_forward_info=true;}"
                              "td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);") ==
              td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacySplitHideEditDateGuardsMustNotReappear) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(hide_edit_date&&is_bot){hide_edit_date=false;}"
                              "if(hide_edit_date&&content_type==MessageContentType::LiveLocation){"
                              "hide_edit_date=false;}") == td::string::npos);
}

TEST(W2BGuestMessageAdversarial, LegacyReplyToStoryValidationMustNotDropBusinessLaneGuard) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                              "story_dialog_id!=DialogId(sender_user_id)&&!is_business_message){") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                              "story_dialog_id!=DialogId(sender_user_id)){") == td::string::npos);
}

TEST(W2BGuestMessageAdversarial, RegularUpdatePathMustNotAccidentallyEnableBusinessLane) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,true,source),from_update,is_channel_message,source);)") ==
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,true,source);)") ==
      td::string::npos);
}

TEST(W2BGuestMessageAdversarial, BusinessMessageObjectPathMustNotFallbackToRegularLane) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_business_message_message_object"),false,false,"get_business_message_message_object");)") ==
      td::string::npos);
}

TEST(W2BGuestMessageAdversarial, EventLogPathMustNotReuseBusinessMessageRouting) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_dialog_event_log_message_object"),true,true,"get_dialog_event_log_message_object");)") ==
      td::string::npos);
}

}  // namespace
