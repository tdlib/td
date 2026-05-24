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

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

}  // namespace

TEST(GuestQueryAdversarial, LegacyGuestCapabilityNameMustNotReappear) {
  auto normalized = read_normalized("td/generate/scheme/td_api.tl");

  ASSERT_EQ(td::string::npos, normalized.find("supports_username_queries:Bool"));
}

TEST(GuestQueryAdversarial, GuestTopPeerCategoryMustNotAliasInlineBots) {
  auto normalized = read_normalized("td/telegram/TopDialogCategory.cpp");

  ASSERT_EQ(td::string::npos,
            normalized.find("casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotInline;"));
}

TEST(GuestQueryAdversarial, GuestUpdatePathMustNotSkipIdentifierValidation) {
  auto normalized = read_normalized("td/telegram/GuestQueryQtsUpdate.h");

  auto id_check_pos = normalized.find(R"(if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;})");
  auto send_update_pos =
      normalized.find(R"(send_update(query_id,std::move(converted_message),std::move(converted_reference_messages));)");

  ASSERT_NE(td::string::npos, id_check_pos);
  ASSERT_NE(td::string::npos, send_update_pos);
  ASSERT_TRUE(id_check_pos < send_update_pos);
}

TEST(GuestQueryAdversarial, GuestUpdatePathMustNotAcceptNegativeIdentifiers) {
  auto normalized = read_normalized("td/telegram/UpdatesManager.cpp");

  ASSERT_EQ(td::string::npos,
            normalized.find("if(update->query_id_==0){LOG(ERROR)<<\"Receiveinvalidguestqueryidentifier\";break;}"));
}

TEST(GuestQueryAdversarial, GuestUpdatePathMustNotConvertMessagesBeforeIdentifierValidation) {
  auto normalized = read_normalized("td/telegram/UpdatesManager.cpp");

  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(for(auto&reference_message:update->reference_messages_){automessage=td_->messages_manager_->get_guest_message_object(std::move(reference_message),false);if(message!=nullptr){reference_messages.push_back(std::move(message));}}automessage=td_->messages_manager_->get_guest_message_object(std::move(update->message_),false);if(message==nullptr){LOG(ERROR)<<"Receiveemptyguestmessage";break;}if(update->query_id_<=0){LOG(ERROR)<<"Receiveinvalidguestqueryidentifier";break;})"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(for(auto&reference_message:update->reference_messages_){)"));
  ASSERT_EQ(td::string::npos,
            normalized.find(
                R"(automessage=td_->messages_manager_->get_guest_message_object(std::move(update->message_),false);)"));
}

TEST(GuestQueryAdversarial, GuestMessageConversionMustFailClosedOnNullInputWithoutAssertion) {
  auto normalized = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_EQ(td::string::npos, normalized.find("if(message==nullptr){CHECK(is_business_message);returnnullptr;}"));
  ASSERT_NE(td::string::npos, normalized.find("if(message==nullptr){returnnullptr;}"));
}

TEST(GuestQueryAdversarial, GuestUpdateQtsHandlerMustNotDegradeToNoopPromisePath) {
  auto normalized = read_normalized("td/telegram/UpdatesManager.cpp");

  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
          "promise){promise.set_value(Unit());}"));
}

TEST(GuestQueryAdversarial, GuestAnswerPathMustNotForwardUncheckedIdentifiers) {
  auto normalized = read_normalized("td/telegram/InlineQueriesManager.cpp");

  ASSERT_EQ(td::string::npos,
            normalized.find(
                "CHECK(td_->auth_manager_->is_bot());TRY_RESULT_PROMISE(promise,result,get_input_bot_inline_result("
                "std::move(input_result),nullptr,nullptr));td_->create_handler<SetBotGuestChatResultQuery>("
                "std::move(promise))->send(guest_query_id,std::move(result));"));
  ASSERT_EQ(td::string::npos,
            normalized.find("classSetBotGuestChatResultQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"));
  ASSERT_EQ(td::string::npos,
            normalized.find("create_handler<SetInlineBotResultsQuery>(std::move(promise))->send(guest_query_id,"));
}

TEST(GuestQueryAdversarial, GuestTopDialogFilteringMustNotIgnoreGuestBotCapability) {
  auto normalized = read_normalized("td/telegram/TopDialogManager.cpp");

  ASSERT_EQ(
      td::string::npos,
      normalized.find("if(query.category==TopDialogCategory::BotInline||query.category==TopDialogCategory::BotPM){"));
  ASSERT_EQ(td::string::npos,
            normalized.find("if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_"
                            "bot_info.ok().is_inline)){"));
  ASSERT_EQ(td::string::npos,
            normalized.find("query.category==TopDialogCategory::BotGuest&&!r_bot_info.ok().is_guestchat_bot"));
}

TEST(GuestQueryAdversarial, GuestBotTopDialogPathMustNotApplyUngatedGuestRating) {
  auto normalized = read_normalized("td/telegram/MessagesManager.cpp");

  auto guest_gate_pos =
      normalized.find("if(should_short_circuit_standard_top_dialog_routing(m->guest_bot_via_dialog_id,is_forward)){");
  auto inline_bot_pos = normalized.find("if(m->via_bot_user_id.is_valid()&&!is_forward){");

  ASSERT_NE(td::string::npos, guest_gate_pos);
  ASSERT_NE(td::string::npos, inline_bot_pos);
  ASSERT_TRUE(guest_gate_pos < inline_bot_pos);
  ASSERT_NE(td::string::npos,
            normalized.find("if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_date)){"));
  ASSERT_NE(td::string::npos,
            normalized.find("last_outgoing_guest_bot_message_date_[guest_bot_dialog_id]=last_guest_bot_message_date;"));
  ASSERT_EQ(td::string::npos,
            normalized.find(
                "if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_"
                "id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->guest_bot_via_"
                "dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()){return;}"));
  ASSERT_EQ(td::string::npos,
            normalized.find("on_dialog_used(TopDialogCategory::BotGuest,DialogId(m->sender_user_id),m->date);"));
}

TEST(GuestQueryAdversarial, UserTypeBotConstructionMustNotOmitGuestCapabilityField) {
  auto normalized = read_normalized("td/telegram/UserManager.cpp");

  ASSERT_EQ(
      td::string::npos,
      normalized.find("type=td_api::make_object<td_api::userTypeBot>(u->can_be_edited_bot,u->can_join_groups,u->can_"
                      "read_all_group_messages,u->has_main_app,u->has_bot_forum_view,u->has_bot_forum_view&&!u->can_"
                      "bot_create_topics,u->can_manage_bots,u->is_inline_bot,u->inline_query_placeholder,u->need_"
                      "location_bot,u->is_business_bot,u->can_be_added_to_attach_menu,u->bot_active_users);"));
}

TEST(GuestQueryAdversarial, RegularMessageObjectMustNotDropGuestCallerIdentifier) {
  auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
  auto normalized = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_EQ(td::string::npos,
            messages_manager_h.find("staticstd::pair<DialogId,unique_ptr<Message>>create_message(Td*td,MessageInfo"
                                    "&&message_info,boolis_channel_message,boolis_business_message,constchar*"
                                    "source);"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object"),false,is_business_message,"get_guest_message_object");)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(if(!is_business_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(supposed_to_be_outgoing!=is_outgoing&&!is_business_message&&content_type!=MessageContentType::ChatDeleteHistory)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_business_message){)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"));
  ASSERT_EQ(td::string::npos,
            normalized.find("dependencies.add(m->via_bot_user_id);dependencies.add(m->via_business_bot_user_id);"));
  ASSERT_EQ(td::string::npos, normalized.find("std::move(self_destruct_type),ttl_expires_in,auto_delete_in,via_bot_"
                                              "user_id,via_business_bot_user_id,m->sender_boost_count,"));
}

TEST(GuestQueryAdversarial, GuestCallerLiveLocationPathsMustNotUseLegacyUngatedPredicates) {
  auto normalized = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_EQ(td::string::npos,
            normalized.find("m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_unsent()||"
                            "m->via_bot_user_id.is_valid()||m->via_business_bot_user_id.is_valid()||m->forward_"
                            "info!=nullptr"));
  ASSERT_EQ(td::string::npos,
            normalized.find("if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||m->via_"
                            "business_bot_user_id.is_valid()||!m->sender_user_id.is_valid()||td_->user_manager_->"
                            "is_user_bot(m->sender_user_id)||m->forward_info!=nullptr){"));
}
