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

TEST(BusinessGuestMessageContract, SelfDialogSenderNormalizationMustSkipBusinessGuestLane) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(if(!is_guest_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)") !=
      td::string::npos);
}

TEST(BusinessGuestMessageContract, OutgoingExpectationMustAccountForBotBusinessViaSender) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_bot&&message_info.via_business_bot_user_id.is_valid());)") !=
      td::string::npos);
}

TEST(BusinessGuestMessageContract, OutgoingExpectationMustFailClosedOnBusinessViaSenderMarker) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());)") ==
      td::string::npos);
}

TEST(BusinessGuestMessageContract, ScheduledMessagesMustDropPinnedFlagBeforeMessageConstruction) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto scheduled_pos = normalized.find("if(message_id.is_scheduled()){");
  auto clear_pinned_pos = normalized.find(
      R"(if(message_info.is_pinned){LOG(ERROR)<<"Receivepinned"<<message_id<<"in"<<dialog_id;message_info.is_pinned=false;})");
  auto assign_pinned_pos = normalized.find("message->is_pinned=message_info.is_pinned;");

  ASSERT_TRUE(scheduled_pos != td::string::npos);
  ASSERT_TRUE(clear_pinned_pos != td::string::npos);
  ASSERT_TRUE(assign_pinned_pos != td::string::npos);
  ASSERT_TRUE(scheduled_pos < clear_pinned_pos);
  ASSERT_TRUE(clear_pinned_pos < assign_pinned_pos);
}

TEST(BusinessGuestMessageContract, BusinessGuestLaneMustSkipMessageTopicFixup) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(!is_guest_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),"
                              "false);}") != td::string::npos);
}

TEST(BusinessGuestMessageContract, HideEditDateGuardMustBeSingleFailClosedPredicate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(if(hide_edit_date&&(is_bot||content_type==MessageContentType::LiveLocation)){hide_edit_date=false;})") !=
      td::string::npos);
}

TEST(BusinessGuestMessageContract, ReplyToStoryValidationMustStayFailClosedInAllLanes) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id){)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_guest_message){)") ==
      td::string::npos);
}

TEST(BusinessGuestMessageContract, RegularUpdatePathMustKeepBusinessLaneDisabledDuringParsingAndConstruction) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,false,source),from_update,is_channel_message,source);)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,false,source);)") !=
      td::string::npos);
}

TEST(BusinessGuestMessageContract, BusinessMessageObjectPathMustPropagateBusinessLaneEndToEnd) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find(R"(automessage_object=get_guest_message_object(std::move(message),true);)") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(returntd_api::make_object<td_api::businessMessage>(std::move(message_object),get_guest_message_object(std::move(reply_to_message),true));)") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("get_business_message_message_object(std::move(message))") == td::string::npos);
  ASSERT_TRUE(normalized.find("get_business_message_message_object(std::move(reply_to_message))") == td::string::npos);
}

TEST(BusinessGuestMessageContract, EventLogPathMustStayOutOfBusinessLane) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_dialog_event_log_message_object"),true,false,"get_dialog_event_log_message_object");)") !=
      td::string::npos);
}

TEST(BusinessGuestMessageContract, GuestTopDialogCandidateMustPropagateForwardFlagIntoHelperContract) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("guest_bot_candidate.is_forward=is_forward;") != td::string::npos);
}

TEST(BusinessGuestMessageContract, ScheduledMessagesMustSanitizeMutableSubobjectsBeforeConstruction) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto scheduled_pos = normalized.find("if(message_id.is_scheduled()){");
  auto reply_clear_pos = normalized.find(
      R"(if(message_info.reply_info!=nullptr){LOG(ERROR)<<"Receive"<<message_id<<"in"<<dialog_id<<"with"<<to_string(message_info.reply_info);message_info.reply_info=nullptr;})");
  auto reactions_clear_pos = normalized.find(
      R"(if(message_info.reactions!=nullptr){LOG(ERROR)<<"Receive"<<message_id<<"in"<<dialog_id<<"with"<<to_string(message_info.reactions);message_info.reactions=nullptr;})");
  auto fact_check_clear_pos = normalized.find(
      R"(if(message_info.fact_check!=nullptr){LOG(ERROR)<<"Receive"<<message_id<<"in"<<dialog_id<<"with"<<to_string(message_info.fact_check);message_info.fact_check=nullptr;})");
  auto suggested_post_clear_pos = normalized.find(
      R"(if(message_info.suggested_post!=nullptr){LOG(ERROR)<<"Receive"<<message_id<<"in"<<dialog_id<<"with"<<to_string(message_info.suggested_post);message_info.suggested_post=nullptr;})");
  auto reply_construct_pos =
      normalized.find("MessageReplyInforeply_info(td,std::move(message_info.reply_info),is_bot);");

  ASSERT_TRUE(scheduled_pos != td::string::npos);
  ASSERT_TRUE(reply_clear_pos != td::string::npos);
  ASSERT_TRUE(reactions_clear_pos != td::string::npos);
  ASSERT_TRUE(fact_check_clear_pos != td::string::npos);
  ASSERT_TRUE(suggested_post_clear_pos != td::string::npos);
  ASSERT_TRUE(reply_construct_pos != td::string::npos);

  ASSERT_TRUE(scheduled_pos < reply_clear_pos);
  ASSERT_TRUE(scheduled_pos < reactions_clear_pos);
  ASSERT_TRUE(scheduled_pos < fact_check_clear_pos);
  ASSERT_TRUE(scheduled_pos < suggested_post_clear_pos);
  ASSERT_TRUE(reply_clear_pos < reply_construct_pos);
}

}  // namespace
