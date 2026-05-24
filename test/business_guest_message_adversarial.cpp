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

TEST(BusinessGuestMessageAdversarial, LegacySelfDialogNormalizationMustNotRewriteBusinessGuestSender) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(R"(if(dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)") ==
      td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacyOutgoingExpectationMustNotIgnoreBusinessGuestViaSender) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("boolsupposed_to_be_outgoing=sender_user_id==my_id&&"
                              "!(dialog_id==my_dialog_id&&!message_id.is_scheduled());") == td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacyOutgoingExpectationMustNotCoupleBusinessViaSenderToBusinessLaneFlag) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());)") ==
      td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacyScheduledPinnedSanitizationMustNotUseDetachedLocalFlag) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("boolis_pinned=message_info.is_pinned;") == td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacyBusinessGuestPathMustNotRunTopicFixupUnconditionally) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(message->forward_info==nullptr&&has_forward_info){message->had_forward_info=true;}"
                              "td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);") ==
              td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacySplitHideEditDateGuardsMustNotReappear) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(hide_edit_date&&is_bot){hide_edit_date=false;}"
                              "if(hide_edit_date&&content_type==MessageContentType::LiveLocation){"
                              "hide_edit_date=false;}") == td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, LegacyReplyToStoryValidationMustNotBypassGuestLaneValidation) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                              "story_dialog_id!=DialogId(sender_user_id)&&"
                              "story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_guest_message){") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                              "story_dialog_id!=DialogId(sender_user_id)&&!is_business_message){") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                              "story_dialog_id!=DialogId(sender_user_id)){") == td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, RegularUpdatePathMustNotAccidentallyEnableBusinessLane) {
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

TEST(BusinessGuestMessageAdversarial, BusinessMessageObjectPathMustNotFallbackToRegularLane) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find(R"(automessage_object=get_business_message_message_object(std::move(message));)") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("get_business_message_message_object(std::move(reply_to_message))") == td::string::npos);
  ASSERT_TRUE(
      normalized.find("td_api::object_ptr<td_api::message>MessagesManager::get_business_message_message_object(") ==
      td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, EventLogPathMustNotReuseBusinessMessageRouting) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_dialog_event_log_message_object"),true,true,"get_dialog_event_log_message_object");)") ==
      td::string::npos);
}

TEST(BusinessGuestMessageAdversarial, GuestTopDialogCandidateMustNotReachHelperWithoutExplicitForwardFlagPropagation) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("guest_bot_candidate.guest_bot_is_bot=td_->user_manager_->is_user_bot(m->sender_user_"
                              "id);if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_"
                              "date)){") == td::string::npos);
}

}  // namespace
