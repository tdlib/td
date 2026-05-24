// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

namespace td::message_content_null_guard_test {

inline bool is_contract_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (!is_contract_whitespace(c)) {
      normalized.push_back(c);
    }
  }
  return normalized;
}

inline td::string read_message_content_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
}

inline td::string normalized_message_content_cpp() {
  return normalize_for_contract(read_message_content_cpp());
}

inline td::string extract_normalized_segment(const td::string &source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin_pos = source.find(begin_marker.str());
  if (begin_pos == td::string::npos) {
    return td::string();
  }
  auto end_pos = source.find(end_marker.str(), begin_pos);
  if (end_pos == td::string::npos) {
    end_pos = source.size();
  }
  return source.substr(begin_pos, end_pos - begin_pos);
}

inline td::string extract_normalized_segment(const td::string &source, const char *begin_marker,
                                             const char *end_marker) {
  auto begin_pos = source.find(begin_marker);
  if (begin_pos == td::string::npos) {
    return td::string();
  }
  auto end_pos = source.find(end_marker, begin_pos);
  if (end_pos == td::string::npos) {
    end_pos = source.size();
  }
  return source.substr(begin_pos, end_pos - begin_pos);
}

struct GuardExpectation {
  const char *name;
  const char *begin_marker;
  const char *end_marker;
  const char *guard_marker;
};

struct GuardOrderExpectation {
  const char *name;
  const char *begin_marker;
  const char *end_marker;
  const char *guard_marker;
  const char *first_deref_marker;
};

inline constexpr std::array<GuardExpectation, 30> kGuardExpectations = {{
    {"secret_input_media", "SecretInputMediaget_message_content_secret_input_media(",
     "statictelegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media_impl(",
     "CHECK(content!=nullptr);"},
    {"paid_media_input_media",
     "telegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media("
     "constMessageContent*content,vector<telegram_api::object_ptr<telegram_api::InputMedia>>"
     "&&input_media){",
     "telegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media(constMessageContent*content,"
     "int32media_pos,Td*td,",
     "CHECK(content!=nullptr);"},
    {"can_send", "Statuscan_send_message_content(DialogIddialog_id,constMessageContent*content,boolis_forward,",
     "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){",
     "CHECK(content!=nullptr);"},
    {"can_forward", "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){",
     "boolupdate_opened_message_content(MessageContent*content){", "CHECK(content!=nullptr);"},
    {"update_opened", "boolupdate_opened_message_content(MessageContent*content){",
     "staticint32get_message_content_text_index_mask(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"text_index", "staticint32get_message_content_text_index_mask(constMessageContent*content){",
     "staticint32get_message_content_media_index_mask(constMessageContent*content,constTd*td,boolis_outgoing){",
     "CHECK(content!=nullptr);"},
    {"media_index",
     "staticint32get_message_content_media_index_mask(constMessageContent*content,constTd*td,"
     "boolis_outgoing){",
     "int32get_message_content_index_mask(constMessageContent*content,constTd*td,boolis_outgoing){",
     "CHECK(content!=nullptr);"},
    {"individual_paid_media",
     "vector<unique_ptr<MessageContent>>get_individual_message_contents(constMessageContent"
     "*content){",
     "StickerTypeget_message_content_sticker_type(constTd*td,constMessageContent*content){",
     "CHECK(content!=nullptr);"},
    {"sticker_type", "StickerTypeget_message_content_sticker_type(constTd*td,constMessageContent*content){",
     "MessageIdget_message_content_pinned_message_id(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"pinned_message_id", "MessageIdget_message_content_pinned_message_id(constMessageContent*content){",
     "BackgroundInfoget_message_content_my_background_info(constMessageContent*content,boolis_outgoing){",
     "CHECK(content!=nullptr);"},
    {"background_info",
     "BackgroundInfoget_message_content_my_background_info(constMessageContent*content,boolis_"
     "outgoing){",
     "ChatThemeget_message_content_chat_theme(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"chat_theme", "ChatThemeget_message_content_chat_theme(constMessageContent*content){",
     "MessageFullIdget_message_content_replied_message_full_id(DialogIddialog_id,constMessageContent*content){",
     "CHECK(content!=nullptr);"},
    {"replied_message_full_id",
     "MessageFullIdget_message_content_replied_message_full_id(DialogIddialog_id,const"
     "MessageContent*content){",
     "std::pair<InputGroupCallId,bool>get_message_content_group_call_info(constMessageContent*content){",
     "CHECK(content!=nullptr);"},
    {"group_call_info",
     "std::pair<InputGroupCallId,bool>get_message_content_group_call_info(constMessageContent*"
     "content){",
     "staticvector<UserId>get_formatted_text_user_ids(constFormattedText*formatted_text){", "CHECK(content!=nullptr);"},
    {"added_user_ids", "vector<UserId>get_message_content_added_user_ids(constMessageContent*content){",
     "UserIdget_message_content_deleted_user_id(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"deleted_user_id", "UserIdget_message_content_deleted_user_id(constMessageContent*content){",
     "telegram_api::object_ptr<telegram_api::inputPhoneCall>get_message_content_input_phone_call(",
     "CHECK(content!=nullptr);"},
    {"input_phone_call",
     "telegram_api::object_ptr<telegram_api::inputPhoneCall>get_message_content_input_phone_call"
     "(constMessageContent*content){",
     "int32get_message_content_live_location_period(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"live_location_period", "int32get_message_content_live_location_period(constMessageContent*content){",
     "PollIdget_message_content_poll_id(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"todo_others_append", "boolget_message_content_to_do_list_others_can_append(constMessageContent*content){",
     "boolget_message_content_to_do_list_can_append_items(constTd*td,constMessageContent*content,int32item_count){",
     "CHECK(content!=nullptr);"},
    {"todo_can_append",
     "boolget_message_content_to_do_list_can_append_items(constTd*td,constMessageContent*content,"
     "int32item_count){",
     "boolget_message_content_to_do_list_others_can_complete(constMessageContent*content){",
     "CHECK(content!=nullptr);"},
    {"todo_others_complete", "boolget_message_content_to_do_list_others_can_complete(constMessageContent*content){",
     "constVenue*get_message_content_venue(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"venue", "constVenue*get_message_content_venue(constMessageContent*content){",
     "boolhas_message_content_web_page(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"has_web_page", "boolhas_message_content_web_page(constMessageContent*content){",
     "voidremove_message_content_web_page(MessageContent*content){", "CHECK(content!=nullptr);"},
    {"remove_web_page", "voidremove_message_content_web_page(MessageContent*content){",
     "boolcan_message_content_have_media_timestamp(constMessageContent*content){", "CHECK(content!=nullptr);"},
    {"merge_message_contents_old",
     "voidmerge_message_contents(Td*td,constMessageContent*old_content,MessageContent*"
     "new_content,",
     "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){",
     "CHECK(old_content!=nullptr);"},
    {"merge_message_contents_new",
     "voidmerge_message_contents(Td*td,constMessageContent*old_content,MessageContent*"
     "new_content,",
     "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){",
     "CHECK(new_content!=nullptr);"},
    {"merge_file_id", "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){",
     "voidcompare_message_contents(Td*td,constMessageContent*old_content,constMessageContent*new_content,",
     "CHECK(message_content!=nullptr);"},
    {"update_remote", "voidupdate_message_content_file_id_remote(MessageContent*content,FileIdfile_id){",
     "voidupdate_message_content_file_id_remotes(MessageContent*content,constvector<FileId>&file_ids){",
     "CHECK(content!=nullptr);"},
    {"update_remotes",
     "voidupdate_message_content_file_id_remotes(MessageContent*content,constvector<FileId>&"
     "file_ids){",
     "FileIdget_message_content_thumbnail_file_id(constMessageContent*content,constTd*td){",
     "CHECK(content!=nullptr);"},
    {"register_message_content",
     "voidregister_message_content(Td*td,constMessageContent*content,MessageFullId"
     "message_full_id,int32message_date,",
     "voidunregister_message_content(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,",
     "CHECK(content!=nullptr);"},
}};

inline constexpr std::array<GuardOrderExpectation, 10> kGuardOrderExpectations = {{
    {"secret_input_media", "SecretInputMediaget_message_content_secret_input_media(",
     "statictelegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media_impl(",
     "CHECK(content!=nullptr);", "switch(content->get_type())"},
    {"can_send", "Statuscan_send_message_content(DialogIddialog_id,constMessageContent*content,boolis_forward,",
     "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){", "CHECK(content!=nullptr);",
     "autocontent_type=content->get_type();"},
    {"can_forward", "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){",
     "boolupdate_opened_message_content(MessageContent*content){", "CHECK(content!=nullptr);",
     "autocontent_type=content->get_type();"},
    {"update_opened", "boolupdate_opened_message_content(MessageContent*content){",
     "staticint32get_message_content_text_index_mask(constMessageContent*content){", "CHECK(content!=nullptr);",
     "switch(content->get_type())"},
    {"text_index", "staticint32get_message_content_text_index_mask(constMessageContent*content){",
     "staticint32get_message_content_media_index_mask(constMessageContent*content,constTd*td,boolis_outgoing){",
     "CHECK(content!=nullptr);", "get_message_content_text(content);"},
    {"media_index",
     "staticint32get_message_content_media_index_mask(constMessageContent*content,constTd*td,"
     "boolis_outgoing){",
     "int32get_message_content_index_mask(constMessageContent*content,constTd*td,boolis_outgoing){",
     "CHECK(content!=nullptr);", "switch(content->get_type())"},
    {"merge_message_contents",
     "voidmerge_message_contents(Td*td,constMessageContent*old_content,MessageContent*"
     "new_content,",
     "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){",
     "CHECK(new_content!=nullptr);", "MessageContentTypecontent_type=new_content->get_type();"},
    {"merge_file_id", "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){",
     "voidcompare_message_contents(Td*td,constMessageContent*old_content,constMessageContent*new_content,",
     "CHECK(message_content!=nullptr);", "MessageContentTypecontent_type=message_content->get_type();"},
    {"update_remote", "voidupdate_message_content_file_id_remote(MessageContent*content,FileIdfile_id){",
     "voidupdate_message_content_file_id_remotes(MessageContent*content,constvector<FileId>&file_ids){",
     "CHECK(content!=nullptr);", "switch(content->get_type())"},
    {"register_message_content",
     "voidregister_message_content(Td*td,constMessageContent*content,MessageFullId"
     "message_full_id,int32message_date,",
     "voidunregister_message_content(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,",
     "CHECK(content!=nullptr);", "autocontent_type=content->get_type();"},
}};

}  // namespace td::message_content_null_guard_test
