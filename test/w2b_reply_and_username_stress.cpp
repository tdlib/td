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

}  // namespace

TEST(W2BReplyAndUsernameStress, RepeatedSourceReadsKeepRequiredGuardsStable) {
  constexpr int kIterations = 2200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; ++i) {
    auto draft_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp"));
    auto messages_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));
    auto dialog_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp"));

    ASSERT_NE(td::string::npos, draft_source.find("autoclear_same_chat_yet_unsent_reply=[this](){"));
    ASSERT_NE(td::string::npos,
              draft_source.find("automessage_id=message_input_reply_to_.get_same_chat_reply_to_message_id();"));
    ASSERT_NE(
        td::string::npos,
        draft_source.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&message_id.is_yet_unsent())"
                          "{message_input_reply_to_={};}"));
    ASSERT_NE(td::string::npos,
              draft_source.find("message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
                                "clear_same_chat_yet_unsent_reply();"));
    ASSERT_NE(td::string::npos,
              draft_source.find("if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);"
                                "clear_same_chat_yet_unsent_reply();}"));

    ASSERT_NE(td::string::npos,
              messages_source.find("if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}"));
    ASSERT_NE(td::string::npos,
              messages_source.find("if(m==nullptr){if(message_id.is_server()&&d->dialog_id.get_type()!="
                                   "DialogType::SecretChat&&d->last_new_message_id.is_valid()&&"
                                   "message_id>d->last_new_message_id&&(d->notification_info!=nullptr&&"
                                   "message_id<=d->notification_info->max_push_notification_message_id_)){"));
    ASSERT_NE(
        td::string::npos,
        messages_source.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
                             "(is_message_forward(m)||!message_id.is_yet_unsent())){"));
    ASSERT_NE(td::string::npos, messages_source.find("update_message_reply_to_message_id(d,m,message_id,false,"
                                                     "\"restore_message_reply_to_message_id\");"));
    ASSERT_NE(td::string::npos,
              messages_source.find("autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m)."
                                   "get_implicit_reply_to_message_id(td_);"));
    ASSERT_NE(td::string::npos,
              messages_source.find("set_message_reply(d,m,MessageInputReplyTo::regular(implicit_reply_to_message_id),"
                                   "false);"));

    ASSERT_NE(td::string::npos, dialog_source.find("if(error.message()==\"USERNAME_OCCUPIED\"){returnpromise.set_value("
                                                   "CheckDialogUsernameResult::Occupied);}"));
    ASSERT_NE(td::string::npos,
              dialog_source.find("if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_value("
                                 "CheckDialogUsernameResult::Purchasable);}"));
    ASSERT_NE(td::string::npos, dialog_source.find("returnpromise.set_error(std::move(error));"));
    ASSERT_NE(td::string::npos,
              dialog_source.find(
                  "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Occupied);"));

    ASSERT_EQ(td::string::npos,
              draft_source.find(
                  "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);}if(has_local_content){"));
    ASSERT_EQ(td::string::npos,
              draft_source.find("if(message_id.is_valid()&&message_id.is_yet_unsent()){message_input_reply_to_={};}"));
    ASSERT_EQ(td::string::npos, messages_source.find("if(!can_reply_to_message(d,message_id,m)){message_id={};}"));
    ASSERT_EQ(td::string::npos, messages_source.find("if(!can_reply_to_message(d,message_id,m)){m=nullptr;}"));
    ASSERT_EQ(td::string::npos, messages_source.find("if(!can_reply_to_message(d,message_id,m)){message_id={};}"
                                                     "if(m==nullptr){if(message_id.is_server()"));
    ASSERT_EQ(
        td::string::npos,
        messages_source.find("if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()){"));
    ASSERT_EQ(td::string::npos, messages_source.find("update_message_reply_to_message_id(d,m,replied_message_id,false,"
                                                     "\"restore_message_reply_to_message_id\");"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("set_message_reply(d,m,MessageInputReplyTo::regular(message_id),false);"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("set_message_reply(d,m,MessageInputReplyTo::regular(replied_message_id),false);"));
    ASSERT_EQ(td::string::npos, dialog_source.find("begins_with(G()->get_option_string(\"my_phone_number\"),\"1\")"));
    ASSERT_EQ(td::string::npos,
              dialog_source.find(
                  "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_error(std::move(error));}"));
    ASSERT_EQ(
        td::string::npos,
        dialog_source.find(
            "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Purchasable);"));
    ASSERT_EQ(td::string::npos,
              dialog_source.find(
                  "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Invalid);"));

    checksum += static_cast<td::uint32>(draft_source.size() + messages_source.size() + dialog_source.size() +
                                        static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(W2BReplyAndUsernameStress, RepeatedSourceReadsKeepOrderingInvariantsStable) {
  constexpr int kIterations = 2200;

  for (int i = 0; i < kIterations; ++i) {
    auto draft_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp"));
    auto messages_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));
    auto dialog_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp"));

    auto helper_pos = draft_source.find("autoclear_same_chat_yet_unsent_reply=[this](){");
    auto inspect_pos = draft_source.find("automessage_id=message_input_reply_to_.get_same_chat_reply_to_message_id();");
    auto clear_pos = draft_source.find(
        "if((message_id.is_valid()||message_id.is_valid_scheduled())&&message_id.is_yet_unsent())"
        "{message_input_reply_to_={};}");
    auto legacy_clear_pos = draft_source.find(
        "message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
        "clear_same_chat_yet_unsent_reply();");
    auto modern_clear_pos = draft_source.find(
        "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);clear_same_chat_yet_unsent_reply();"
        "}");

    ASSERT_TRUE(helper_pos != td::string::npos);
    ASSERT_TRUE(inspect_pos != td::string::npos);
    ASSERT_TRUE(clear_pos != td::string::npos);
    ASSERT_TRUE(legacy_clear_pos != td::string::npos);
    ASSERT_TRUE(modern_clear_pos != td::string::npos);
    ASSERT_TRUE(helper_pos < inspect_pos);
    ASSERT_TRUE(inspect_pos < clear_pos);
    ASSERT_TRUE(helper_pos < legacy_clear_pos);
    ASSERT_TRUE(legacy_clear_pos < modern_clear_pos);

    auto reject_pos = messages_source.find("if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}");
    auto missing_message_pos = messages_source.find("if(m==nullptr){", reject_pos);
    auto unknown_server_allow_pos = messages_source.find(
        "if(message_id.is_server()&&d->dialog_id.get_type()!=DialogType::SecretChat&&"
        "d->last_new_message_id.is_valid()&&message_id>d->last_new_message_id&&"
        "(d->notification_info!=nullptr&&message_id<=d->notification_info->max_push_notification_message_id_)){",
        missing_message_pos);
    auto resolve_pos = messages_source.find(
        "automessage_id=get_message_id_by_random_id(d,m->reply_to_random_id,"
        "\"restore_message_reply_to_message_id\");");
    auto restore_pos = messages_source.find(
        "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
        "(is_message_forward(m)||!message_id.is_yet_unsent())){");
    auto update_pos = messages_source.find(
        "update_message_reply_to_message_id(d,m,message_id,false,\"restore_message_reply_to_message_id\");",
        restore_pos);
    auto stale_update_pos = messages_source.find(
        "update_message_reply_to_message_id(d,m,replied_message_id,false,\"restore_message_reply_to_message_id\");",
        restore_pos);
    auto fallback_compute_pos = messages_source.find(
        "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m)."
        "get_implicit_reply_to_message_id(td_);");
    auto fallback_set_pos = messages_source.find(
        "set_message_reply(d,m,MessageInputReplyTo::regular(implicit_reply_to_message_id),"
        "false);");
    auto occupied_pos = dialog_source.find(
        "if(error.message()==\"USERNAME_OCCUPIED\"){returnpromise.set_value("
        "CheckDialogUsernameResult::Occupied);}");
    auto purchasable_pos = dialog_source.find(
        "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_value("
        "CheckDialogUsernameResult::Purchasable);}");
    auto fallback_error_pos = dialog_source.find("returnpromise.set_error(std::move(error));");
    auto success_pos = dialog_source.find(
        "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Occupied);");

    ASSERT_TRUE(reject_pos != td::string::npos);
    ASSERT_TRUE(missing_message_pos != td::string::npos);
    ASSERT_TRUE(unknown_server_allow_pos != td::string::npos);
    ASSERT_TRUE(resolve_pos != td::string::npos);
    ASSERT_TRUE(restore_pos != td::string::npos);
    ASSERT_TRUE(update_pos != td::string::npos);
    ASSERT_TRUE(stale_update_pos == td::string::npos);
    ASSERT_TRUE(fallback_compute_pos != td::string::npos);
    ASSERT_TRUE(fallback_set_pos != td::string::npos);
    ASSERT_TRUE(occupied_pos != td::string::npos);
    ASSERT_TRUE(purchasable_pos != td::string::npos);
    ASSERT_TRUE(fallback_error_pos != td::string::npos);
    ASSERT_TRUE(success_pos != td::string::npos);
    ASSERT_TRUE(reject_pos < missing_message_pos);
    ASSERT_TRUE(missing_message_pos < unknown_server_allow_pos);
    ASSERT_TRUE(reject_pos < restore_pos);
    ASSERT_TRUE(resolve_pos < restore_pos);
    ASSERT_TRUE(restore_pos < update_pos);
    ASSERT_TRUE(update_pos < fallback_compute_pos);
    ASSERT_TRUE(restore_pos < fallback_compute_pos);
    ASSERT_TRUE(fallback_compute_pos < fallback_set_pos);
    ASSERT_TRUE(occupied_pos < purchasable_pos);
    ASSERT_TRUE(purchasable_pos < fallback_error_pos);
    ASSERT_TRUE(fallback_error_pos < success_pos);
  }
}
