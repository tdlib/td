// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

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

TEST(BusinessGuestMessageStress, RepeatedSourceReadsKeepGuestGuardInvariantsStable) {
  constexpr int kIterations = 3000;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
    auto normalized = normalize_for_contract(source);

    ASSERT_NE(td::string::npos,
              normalized.find("if(!is_guest_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||"
                              "sender_dialog_id.is_valid())){"));
    ASSERT_NE(td::string::npos, normalized.find("boolsupposed_to_be_outgoing=sender_user_id==my_id&&"
                                                "!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&"
                                                "!(is_bot&&message_info.via_business_bot_user_id.is_valid());"));
    ASSERT_NE(td::string::npos, normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                                                "story_dialog_id!=DialogId(sender_user_id)&&"
                                                "story_dialog_id!=message_info.guest_bot_via_dialog_id){"));
    ASSERT_NE(td::string::npos,
              normalized.find("if(message_info.is_pinned){LOG(ERROR)<<\"Receivepinned\"<<message_id<<\"in\""
                              "<<dialog_id;message_info.is_pinned=false;}"));
    ASSERT_NE(td::string::npos, normalized.find("on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),"
                                                "is_scheduled,false,source),from_update,is_channel_message,source);"));
    ASSERT_NE(td::string::npos,
              normalized.find("std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),"
                              "is_channel_message,false,source);"));
    ASSERT_NE(td::string::npos,
              normalized.find("automessage_object=get_guest_message_object(std::move(message),true);"));
    ASSERT_NE(td::string::npos,
              normalized.find("returntd_api::make_object<td_api::businessMessage>(std::move(message_object),"
                              "get_guest_message_object(std::move(reply_to_message),true));"));
    ASSERT_NE(td::string::npos, normalized.find("guest_bot_candidate.is_forward=is_forward;"));
    ASSERT_NE(td::string::npos,
              normalized.find("autodialog_message=create_message(td_,parse_telegram_api_message(td_,"
                              "std::move(message),false,false,\"get_dialog_event_log_message_object\"),"
                              "true,false,\"get_dialog_event_log_message_object\");"));

    ASSERT_EQ(td::string::npos,
              normalized.find("boolsupposed_to_be_outgoing=sender_user_id==my_id&&"
                              "!(dialog_id==my_dialog_id&&!message_id.is_scheduled())&&"
                              "!(is_business_message&&is_bot&&message_info.via_business_bot_user_id.is_valid());"));
    ASSERT_EQ(td::string::npos, normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                                                "story_dialog_id!=DialogId(sender_user_id)&&!"
                                                "is_business_message){"));
    ASSERT_EQ(td::string::npos, normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                                                "story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!="
                                                "message_info.guest_bot_via_dialog_id&&!is_guest_message){"));
    ASSERT_EQ(td::string::npos, normalized.find("if(story_dialog_id!=my_dialog_id&&story_dialog_id!=dialog_id&&"
                                                "story_dialog_id!=DialogId(sender_user_id)){"));
    ASSERT_EQ(td::string::npos, normalized.find("on_get_message(parse_telegram_api_message(td_,std::move(message_ptr),"
                                                "is_scheduled,true,source),from_update,is_channel_message,source);"));
    ASSERT_EQ(td::string::npos,
              normalized.find("std::tie(dialog_id,new_message)=create_message(td_,std::move(message_info),"
                              "is_channel_message,true,source);"));
    ASSERT_EQ(td::string::npos,
              normalized.find("autodialog_message=create_message(td_,parse_telegram_api_message(td_,"
                              "std::move(message),false,false,\"get_business_message_message_object\"),"
                              "false,false,\"get_business_message_message_object\");"));
    ASSERT_EQ(td::string::npos,
              normalized.find("automessage_object=get_business_message_message_object(std::move(message));"));
    ASSERT_EQ(td::string::npos, normalized.find("get_business_message_message_object(std::move(reply_to_message))"));
    ASSERT_EQ(td::string::npos, normalized.find("td_api::object_ptr<td_api::message>MessagesManager::"
                                                "get_business_message_message_object("));
    ASSERT_EQ(td::string::npos,
              normalized.find("guest_bot_candidate.guest_bot_is_bot=td_->user_manager_->is_user_bot(m->sender_user_"
                              "id);if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_"
                              "date)){"));
    ASSERT_EQ(td::string::npos,
              normalized.find("autodialog_message=create_message(td_,parse_telegram_api_message(td_,"
                              "std::move(message),false,true,\"get_dialog_event_log_message_object\"),"
                              "true,true,\"get_dialog_event_log_message_object\");"));

    checksum += static_cast<td::uint32>(normalized.size() + static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}
