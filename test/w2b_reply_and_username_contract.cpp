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

TEST(W2BReplyAndUsernameContract, DraftMessageParseClearsSameChatYetUnsentReplyReference) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp");
  auto normalized = normalize_for_contract(source);

  auto helper_pos = normalized.find("autoclear_same_chat_yet_unsent_reply=[this](){");
  auto inspect_pos = normalized.find("automessage_id=message_input_reply_to_.get_same_chat_reply_to_message_id();");
  auto clear_pos = normalized.find(
      "if((message_id.is_valid()||message_id.is_valid_scheduled())&&message_id.is_yet_unsent())"
      "{message_input_reply_to_={};}");
  auto legacy_clear_pos = normalized.find(
      "message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
      "clear_same_chat_yet_unsent_reply();");
  auto modern_clear_pos = normalized.find(
      "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);clear_same_chat_yet_unsent_reply();}");

  ASSERT_TRUE(helper_pos != td::string::npos);
  ASSERT_TRUE(inspect_pos != td::string::npos);
  ASSERT_TRUE(clear_pos != td::string::npos);
  ASSERT_TRUE(legacy_clear_pos != td::string::npos);
  ASSERT_TRUE(modern_clear_pos != td::string::npos);
  ASSERT_TRUE(helper_pos < inspect_pos);
  ASSERT_TRUE(inspect_pos < clear_pos);
  ASSERT_TRUE(helper_pos < legacy_clear_pos);
  ASSERT_TRUE(legacy_clear_pos < modern_clear_pos);
}

TEST(W2BReplyAndUsernameContract, CreateMessageInputReplyToNullsPointerAfterInvalidReplyRejection) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}") !=
              td::string::npos);
}

TEST(W2BReplyAndUsernameContract, CreateMessageInputReplyToRejectPathClearsMessageIdBeforeUnknownServerReplyAllowance) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto clear_reject_pos = normalized.find("if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}");
  auto missing_message_pos = normalized.find("if(m==nullptr){", clear_reject_pos);
  auto unknown_server_allow_pos = normalized.find(
      "if(message_id.is_server()&&d->dialog_id.get_type()!=DialogType::SecretChat&&"
      "d->last_new_message_id.is_valid()&&message_id>d->last_new_message_id&&"
      "(d->notification_info!=nullptr&&"
      "message_id<=d->notification_info->max_push_notification_message_id_)){",
      missing_message_pos);

  ASSERT_TRUE(clear_reject_pos != td::string::npos);
  ASSERT_TRUE(missing_message_pos != td::string::npos);
  ASSERT_TRUE(unknown_server_allow_pos != td::string::npos);
  ASSERT_TRUE(clear_reject_pos < missing_message_pos);
  ASSERT_TRUE(missing_message_pos < unknown_server_allow_pos);
}

TEST(W2BReplyAndUsernameContract, RestoreReplyToMessageKeepsYetUnsentTargetsOnlyForForwards) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto resolve_pos = normalized.find(
      "automessage_id=get_message_id_by_random_id(d,m->reply_to_random_id,"
      "\"restore_message_reply_to_message_id\");");
  auto guard_pos = normalized.find(
      "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
      "(is_message_forward(m)||!message_id.is_yet_unsent())){");
  auto fallback_compute_pos = normalized.find(
      "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m)."
      "get_implicit_reply_to_message_id(td_);");
  auto fallback_set_pos =
      normalized.find("set_message_reply(d,m,MessageInputReplyTo::regular(implicit_reply_to_message_id),false);");

  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(fallback_compute_pos != td::string::npos);
  ASSERT_TRUE(fallback_set_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos < guard_pos);
  ASSERT_TRUE(guard_pos < fallback_compute_pos);
  ASSERT_TRUE(fallback_compute_pos < fallback_set_pos);
}

TEST(W2BReplyAndUsernameContract, RestoreReplyToMessageUpdatesFromResolvedRandomIdOnAllowedPath) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto resolve_pos = normalized.find(
      "automessage_id=get_message_id_by_random_id(d,m->reply_to_random_id,"
      "\"restore_message_reply_to_message_id\");");
  auto guard_pos = normalized.find(
      "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
      "(is_message_forward(m)||!message_id.is_yet_unsent())){");
  auto update_pos = normalized.find(
      "update_message_reply_to_message_id(d,m,message_id,false,\"restore_message_reply_to_message_id\");");
  auto fallback_compute_pos = normalized.find(
      "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m)."
      "get_implicit_reply_to_message_id(td_);");

  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(update_pos != td::string::npos);
  ASSERT_TRUE(fallback_compute_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos < guard_pos);
  ASSERT_TRUE(guard_pos < update_pos);
  ASSERT_TRUE(update_pos < fallback_compute_pos);
}

TEST(W2BReplyAndUsernameContract, CheckDialogUsernameMapsOccupiedAndPurchasableErrorsDeterministically) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto occupied_pos = normalized.find(
      "if(error.message()==\"USERNAME_OCCUPIED\"){returnpromise.set_value("
      "CheckDialogUsernameResult::Occupied);}");
  auto purchasable_pos = normalized.find(
      "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_value("
      "CheckDialogUsernameResult::Purchasable);}");
  auto fallback_pos = normalized.find("returnpromise.set_error(std::move(error));");
  auto success_pos = normalized.find(
      "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Occupied);");

  ASSERT_TRUE(occupied_pos != td::string::npos);
  ASSERT_TRUE(purchasable_pos != td::string::npos);
  ASSERT_TRUE(fallback_pos != td::string::npos);
  ASSERT_TRUE(success_pos != td::string::npos);
  ASSERT_TRUE(occupied_pos < purchasable_pos);
  ASSERT_TRUE(purchasable_pos < fallback_pos);
  ASSERT_TRUE(fallback_pos < success_pos);
}

}  // namespace
