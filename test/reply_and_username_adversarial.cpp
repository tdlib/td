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

TEST(ReplyAndUsernameAdversarial, DraftMessageParseMustNotKeepLegacyYetUnsentReplyPathUntouched) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
                              "clear_same_chat_yet_unsent_reply();") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);"
                              "clear_same_chat_yet_unsent_reply();}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);}"
                              "if(has_local_content){") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(message_id.is_valid()&&message_id.is_yet_unsent()){message_input_reply_to_={};}") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&"
                              "message_id.is_yet_unsent()){message_input_reply_to_={};}") == td::string::npos);
  ASSERT_TRUE(normalized.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&"
                              "(message_id.is_yet_unsent()||message_id.is_local()))"
                              "{message_input_reply_to_={};}") != td::string::npos);
}

TEST(ReplyAndUsernameAdversarial, InvalidReplyRejectionMustNotRetainStaleMessagePointer) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(!can_reply_to_message(d,message_id,m)){message_id={};}") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(!can_reply_to_message(d,message_id,m)){m=nullptr;}") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(!can_reply_to_message(d,message_id,m)){message_id={};}"
                              "if(m==nullptr){if(message_id.is_server()") == td::string::npos);
}

TEST(ReplyAndUsernameAdversarial, RestoreReplyMustNotBindYetUnsentNonForwardTargets) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()){") ==
      td::string::npos);
  ASSERT_TRUE(normalized.find("set_message_reply(d,m,MessageInputReplyTo::regular(message_id),false);") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("set_message_reply(d,m,MessageInputReplyTo::regular(replied_message_id),false);") ==
              td::string::npos);
}

TEST(ReplyAndUsernameAdversarial, RestoreReplyMustNotUpdateUsingStaleRepliedMessageId) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("update_message_reply_to_message_id(d,m,replied_message_id,false,\"restore_message_reply_"
                              "to_message_id\");") == td::string::npos);
}

TEST(ReplyAndUsernameAdversarial, UsernamePurchaseHandlingMustNotDependOnPhoneCountryHeuristics) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("begins_with(G()->get_option_string(\"my_phone_number\"),\"1\")") == td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_error(std::move(error));}") ==
              td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Purchasable);") ==
      td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Invalid);") ==
              td::string::npos);
}

}  // namespace
