// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

}  // namespace

TEST(W2BGuestMessageLightFuzz, RandomizedProbeOrderKeepsRequiredGuardPatternsPinned) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  const std::array<td::string, 10> required = {
      "if(!is_business_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){",
      R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_bot&&message_info.via_business_bot_user_id.is_valid());)",
      "if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)&&"
      "story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_business_message){",
      R"(if(message_info.is_pinned){LOG(ERROR)<<"Receivepinned"<<message_id<<"in"<<dialog_id;message_info.is_pinned=false;})",
      "if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);}",
      "if(hide_edit_date&&(is_bot||content_type==MessageContentType::LiveLocation)){hide_edit_date=false;}",
      R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,false,source),from_update,is_channel_message,source);)",
      R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,false,source);)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_business_message_message_object"),false,true,"get_business_message_message_object");)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_dialog_event_log_message_object"),true,false,"get_dialog_event_log_message_object");)",
  };

  for (int i = 0; i < 12000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    ASSERT_NE(td::string::npos, normalized.find(required[idx]));
  }
}

TEST(W2BGuestMessageLightFuzz, RandomizedProbeOrderKeepsLegacyUnsafePatternsAbsent) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  const std::array<td::string, 11> forbidden = {
      "if(dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){",
      "boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled());",
      R"(boolsupposed_to_be_outgoing=sender_user_id==my_id&&!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());)",
      "if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)&&!is_"
      "business_message){",
      "if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&story_dialog_id!=DialogId(sender_user_id)){",
      "boolis_pinned=message_info.is_pinned;",
      "if(hide_edit_date&&is_bot){hide_edit_date=false;}"
      "if(hide_edit_date&&content_type==MessageContentType::LiveLocation){hide_edit_date=false;}",
      R"(on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),is_scheduled,true,source),from_update,is_channel_message,source);)",
      R"(std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),is_channel_message,true,source);)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,false,"get_business_message_message_object"),false,false,"get_business_message_message_object");)",
      R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,true,"get_dialog_event_log_message_object"),true,true,"get_dialog_event_log_message_object");)",
  };

  for (int i = 0; i < 12000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    ASSERT_EQ(td::string::npos, normalized.find(forbidden[idx]));
  }
}
