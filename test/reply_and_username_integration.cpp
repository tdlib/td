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
    switch (static_cast<unsigned char>(c)) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        continue;
      default:
        break;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(ReplyAndUsernameIntegration, RequiredReplyAndUsernameGuardPatternsExistAcrossAllTouchedFiles) {
  struct ExpectedSnippet {
    td::Slice path;
    td::Slice snippet;
  };

  const ExpectedSnippet required[] = {
      {"td/telegram/DraftMessage.hpp", "autoclear_same_chat_yet_unsent_reply=[this](){"},
      {"td/telegram/DraftMessage.hpp", "automessage_id=message_input_reply_to_.get_same_chat_reply_to_message_id();"},
      {"td/telegram/DraftMessage.hpp",
       "if((message_id.is_valid()||message_id.is_valid_scheduled())&&"
       "(message_id.is_yet_unsent()||message_id.is_local()))"
       "{message_input_reply_to_={};}"},
      {"td/telegram/DraftMessage.hpp",
       "message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
       "clear_same_chat_yet_unsent_reply();"},
      {"td/telegram/DraftMessage.hpp",
       "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);"
       "clear_same_chat_yet_unsent_reply();}"},
      {"td/telegram/MessagesManager.cpp", "if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}"},
      {"td/telegram/MessagesManager.cpp",
       "if(m==nullptr){if(message_id.is_server()&&d->dialog_id.get_type()!=DialogType::SecretChat&&"
       "d->last_new_message_id.is_valid()&&message_id>d->last_new_message_id&&"
       "(d->notification_info!=nullptr&&"
       "message_id<=d->notification_info->max_push_notification_message_id_)){"},
      {"td/telegram/MessagesManager.cpp",
       "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
       "(is_message_forward(m)||!message_id.is_yet_unsent())){"},
      {"td/telegram/MessagesManager.cpp",
       "update_message_reply_to_message_id(d,m,message_id,false,\"restore_message_reply_to_message_id\");"},
      {"td/telegram/MessagesManager.cpp",
       "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m).get_implicit_reply_to_message_id(td_);"},
      {"td/telegram/MessagesManager.cpp",
       "set_message_reply(d,m,MessageInputReplyTo::regular(implicit_reply_to_message_id),false);"},
      {"td/telegram/DialogManager.cpp",
       "if(error.message()==\"USERNAME_OCCUPIED\"){returnpromise.set_value(CheckDialogUsernameResult::Occupied);}"},
      {"td/telegram/DialogManager.cpp",
       "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_value("
       "CheckDialogUsernameResult::Purchasable);}"},
      {"td/telegram/DialogManager.cpp", "returnpromise.set_error(std::move(error));"},
      {"td/telegram/DialogManager.cpp",
       "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Occupied);"},
  };

  for (const auto &entry : required) {
    auto source = td::mtproto::test::read_repo_text_file(entry.path);
    auto normalized = normalize_for_contract(source);
    ASSERT_TRUE(normalized.find(entry.snippet.str()) != td::string::npos);
  }
}

TEST(ReplyAndUsernameIntegration, LegacyReplyAndUsernameUnsafePatternsRemainAbsentAcrossAllTouchedFiles) {
  struct ForbiddenSnippet {
    td::Slice path;
    td::Slice snippet;
  };

  const ForbiddenSnippet forbidden[] = {
      {"td/telegram/DraftMessage.hpp",
       "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);}if(has_local_content){"},
      {"td/telegram/DraftMessage.hpp",
       "if(message_id.is_valid()&&message_id.is_yet_unsent()){message_input_reply_to_={};}"},
      {"td/telegram/DraftMessage.hpp",
       "if((message_id.is_valid()||message_id.is_valid_scheduled())&&message_id.is_yet_unsent())"
       "{message_input_reply_to_={};}"},
      {"td/telegram/MessagesManager.cpp", "if(!can_reply_to_message(d,message_id,m)){message_id={};}"},
      {"td/telegram/MessagesManager.cpp", "if(!can_reply_to_message(d,message_id,m)){m=nullptr;}"},
      {"td/telegram/MessagesManager.cpp",
       "if(!can_reply_to_message(d,message_id,m)){message_id={};}if(m==nullptr){if(message_id.is_server()"},
      {"td/telegram/MessagesManager.cpp",
       "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()){"},
      {"td/telegram/MessagesManager.cpp",
       "update_message_reply_to_message_id(d,m,replied_message_id,false,\"restore_message_reply_to_message_id\");"},
      {"td/telegram/MessagesManager.cpp", "set_message_reply(d,m,MessageInputReplyTo::regular(message_id),false);"},
      {"td/telegram/MessagesManager.cpp",
       "set_message_reply(d,m,MessageInputReplyTo::regular(replied_message_id),false);"},
      {"td/telegram/DialogManager.cpp", "begins_with(G()->get_option_string(\"my_phone_number\"),\"1\")"},
      {"td/telegram/DialogManager.cpp",
       "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_error(std::move(error));}"},
      {"td/telegram/DialogManager.cpp",
       "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Purchasable);"},
      {"td/telegram/DialogManager.cpp",
       "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Invalid);"},
  };

  for (const auto &entry : forbidden) {
    auto source = td::mtproto::test::read_repo_text_file(entry.path);
    auto normalized = normalize_for_contract(source);
    ASSERT_TRUE(normalized.find(entry.snippet.str()) == td::string::npos);
  }
}

}  // namespace
