// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <vector>

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

TEST(W2BGuestMessageIntegration, RequiredWave2BGuestGuardPatternsExistInMessagesManager) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  const std::vector<td::Slice> required = {
      "if(!is_business_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){",
      R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_bot&&message_info.via_business_bot_user_id.is_valid());)",
      R"(if(message_info.is_pinned){LOG(ERROR)<<"Receivepinned"<<message_id<<"in"<<dialog_id;message_info.is_pinned=false;})",
      R"(message->is_pinned=message_info.is_pinned;)",
      "if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);}",
      "if(hide_edit_date&&(is_bot||content_type==MessageContentType::LiveLocation)){hide_edit_date=false;}",
      R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,false,source),from_update,is_channel_message,source);)",
      R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,false,source);)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_business_message_message_object"),false,true,"get_business_message_message_object");)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_dialog_event_log_message_object"),true,false,"get_dialog_event_log_message_object");)",

      R"(if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)&&!is_business_message){)",
  };

  for (auto snippet : required) {
    ASSERT_TRUE(normalized.find(snippet.str()) != td::string::npos);
  }
}

TEST(W2BGuestMessageIntegration, LegacyWave2BGuestUnsafePatternsAreAbsentInMessagesManager) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  const std::vector<td::Slice> forbidden = {
      "if(dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){",
      "boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled());",
      R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());)",
      R"(boolis_pinned=message_info.is_pinned;)",
      R"(if(hide_edit_date&&is_bot){hide_edit_date=false;}if(hide_edit_date&&content_type==MessageContentType::LiveLocation){hide_edit_date=false;})",
      R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,true,source),from_update,is_channel_message,source);)",
      R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,true,source);)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_business_message_message_object"),false,false,"get_business_message_message_object");)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_dialog_event_log_message_object"),true,true,"get_dialog_event_log_message_object");)",
      R"(if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)){)",
  };

  for (auto snippet : forbidden) {
    ASSERT_TRUE(normalized.find(snippet.str()) == td::string::npos);
  }
}

}  // namespace
