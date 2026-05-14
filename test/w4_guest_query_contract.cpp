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

TEST(W4GuestQueryContract, TdApiSchemaMustExposeGuestQuerySurface) {
  auto normalized = read_normalized("td/generate/scheme/td_api.tl");

  ASSERT_NE(td::string::npos, normalized.find("inlineMessageIdid:string=InlineMessageId;"));
  ASSERT_NE(td::string::npos, normalized.find("supports_guest_queries:Bool"));
  ASSERT_NE(td::string::npos, normalized.find("guest_bot_caller_id:MessageSender"));
  ASSERT_NE(td::string::npos, normalized.find("topChatCategoryGuestBots=TopChatCategory;"));
  ASSERT_NE(td::string::npos,
            normalized.find("updateNewGuestQueryid:int64message:messagereference_messages:vector<message>=Update;"));
  ASSERT_NE(td::string::npos,
            normalized.find("answerGuestQueryguest_query_id:int64result:InputInlineQueryResult=InlineMessageId;"));
}

TEST(W4GuestQueryContract, TelegramApiSchemaMustExposeGuestTransportSurface) {
  auto normalized = read_normalized("td/generate/scheme/telegram_api.tl");

  ASSERT_NE(td::string::npos, normalized.find("bot_guestchat:flags2.19?true"));
  ASSERT_NE(td::string::npos, normalized.find("guestchat_via_from:flags2.19?Peer"));
  ASSERT_NE(td::string::npos, normalized.find("updateBotGuestChatQuery#"));
  ASSERT_NE(td::string::npos, normalized.find("topPeerCategoryBotsGuestChat#"));
  ASSERT_NE(td::string::npos, normalized.find("bots_guestchat:flags.17?true"));
  ASSERT_NE(td::string::npos, normalized.find("messages.setBotGuestChatResult#"));
}

TEST(W4GuestQueryContract, GuestQueryEntryPointsMustBeDeclared) {
  auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
  auto updates_manager_h = read_normalized("td/telegram/UpdatesManager.h");
  auto inline_queries_manager_h = read_normalized("td/telegram/InlineQueriesManager.h");
  auto requests_h = read_normalized("td/telegram/Requests.h");
  auto top_dialog_category_h = read_normalized("td/telegram/TopDialogCategory.h");
  auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");

  ASSERT_NE(td::string::npos,
            messages_manager_h.find("td_api::object_ptr<td_api::message>get_guest_message_object(telegram_api::object_"
                                    "ptr<telegram_api::Message>&&message,boolis_business_message);"));
  ASSERT_NE(td::string::npos,
            updates_manager_h.find(
                "voidon_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&promise);"));
  ASSERT_NE(td::string::npos, updates_manager.find("casetelegram_api::updateBotGuestChatQuery::ID:returntrue;"));
  ASSERT_NE(
      td::string::npos,
      updates_manager.find(
          "casetelegram_api::updateBotGuestChatQuery::ID:returnstatic_cast<consttelegram_api::updateBotGuestChatQuery*"
          ">(update)->qts_;"));
  ASSERT_NE(
      td::string::npos,
      updates_manager.find(
          "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
          "promise){autoqts=update->qts_;add_pending_qts_update(std::move(update),qts,std::move(promise));}"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager_h.find(
                "voidanswer_guest_query(int64guest_query_id,td_api::object_ptr<td_api::InputInlineQueryResult>&&input_"
                "result,Promise<td_api::object_ptr<td_api::inlineMessageId>>&&promise)const;"));
  ASSERT_NE(td::string::npos, requests_h.find("voidon_request(uint64id,td_api::answerGuestQuery&request);"));
  ASSERT_NE(td::string::npos, top_dialog_category_h.find("BotGuest,"));
  ASSERT_NE(td::string::npos, top_dialog_category.find("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_guest\");"));
  ASSERT_NE(td::string::npos,
            top_dialog_category.find(
                "caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsGuestChat>();"));
  ASSERT_NE(td::string::npos,
            messages_manager_h.find("staticstd::pair<DialogId,unique_ptr<Message>>create_message(Td*td,MessageInfo"
                                    "&&message_info,boolis_channel_message,boolis_guest_message,constchar*source);"));
  ASSERT_NE(td::string::npos,
            messages_manager_h.find(
                "td_api::object_ptr<td_api::MessageSender>get_message_guest_sender_object(constMessage*m)const;"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(automessage_info=parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object");)"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(R"(autois_channel_message=message_info.dialog_id.get_type()==DialogType::Channel;)"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(autodialog_message=create_message(td_,std::move(message_info),is_channel_message,true,"get_guest_message_object");)"));
  ASSERT_NE(td::string::npos,
            messages_manager.find("message->guest_bot_via_dialog_id=message_info.guest_bot_via_dialog_id;"));
  ASSERT_NE(td::string::npos, messages_manager.find("if(m->guest_bot_via_dialog_id==DialogId()){returnnullptr;}"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                "returnget_message_sender_object_const(td_,m->guest_bot_via_dialog_id,\"get_message_guest_sender_"
                "object\");"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(if(!is_guest_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(supposed_to_be_outgoing!=is_outgoing&&!is_guest_message&&content_type!=MessageContentType::ChatDeleteHistory)"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_guest_message){)"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                R"(if(!is_guest_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->via_business_bot_user_id.is_valid()){return;})"));
  ASSERT_NE(td::string::npos, messages_manager.find(R"(if(m->guest_bot_via_dialog_id.is_valid()){if(!is_forward){)"));
}

TEST(W4GuestQueryContract, GuestQueryBotOnlyEntryPointsMustFailClosed) {
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto requests = read_normalized("td/telegram/Requests.cpp");

  ASSERT_NE(
      td::string::npos,
      requests.find(
          R"(voidRequests::on_request(uint64id,td_api::answerGuestQuery&request){CHECK_IS_BOT();CREATE_REQUEST_PROMISE();td_->inline_queries_manager_->answer_guest_query(request.guest_query_id_,std::move(request.result_),std::move(promise));})"));
  ASSERT_NE(
      td::string::npos,
      inline_queries_manager.find(
          R"(voidInlineQueriesManager::answer_guest_query(int64guest_query_id,td_api::object_ptr<td_api::InputInlineQueryResult>&&input_result,Promise<td_api::object_ptr<td_api::inlineMessageId>>&&promise)const{CHECK(td_->auth_manager_->is_bot());)"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("classSetBotGuestChatResultQueryfinal:publicTd::ResultHandler{"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("Promise<td_api::object_ptr<td_api::inlineMessageId>>promise_;"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("telegram_api::messages_setBotGuestChatResult(query_id,std::move(result))"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("fetch_result<telegram_api::messages_setBotGuestChatResult>(packet);"));
  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("promise_.set_value(td_api::make_object<td_api::inlineMessageId>("
                                        "InlineQueriesManager::get_inline_message_id(std::move(ptr))));"));
}

TEST(W4GuestQueryContract, GuestQueryIdentifiersMustBeStrictlyPositiveAcrossEntryPoints) {
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto guest_query_qts_update = read_normalized("td/telegram/GuestQueryQtsUpdate.h");
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");

  ASSERT_NE(td::string::npos,
            inline_queries_manager.find(
                R"(if(guest_query_id<=0){returnpromise.set_error(400,"Invalidguest_query_idspecified");})"));
  ASSERT_NE(td::string::npos,
            guest_query_qts_update.find(R"(if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;})"));
  ASSERT_NE(td::string::npos, updates_manager.find("autoresult=dispatch_guest_query_qts_update("));
  ASSERT_NE(td::string::npos, updates_manager.find("caseGuestQueryQtsUpdateResult::InvalidQueryId:"));
  ASSERT_NE(td::string::npos, updates_manager.find("LOG(ERROR)<<\"Receiveinvalidguestqueryidentifier\";"));
}

TEST(W4GuestQueryContract, GuestQueryIdentifiersMustBeRejectedBeforeGuestMessageConversion) {
  auto guest_query_qts_update = read_normalized("td/telegram/GuestQueryQtsUpdate.h");

  auto id_check_pos =
      guest_query_qts_update.find(R"(if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;})");
  auto reference_loop_pos = guest_query_qts_update.find(R"(for(auto&reference_message:reference_messages){)");
  auto message_conversion_pos =
      guest_query_qts_update.find(R"(autoconverted_message=convert_message(std::move(message));)");

  ASSERT_NE(td::string::npos, id_check_pos);
  ASSERT_NE(td::string::npos, reference_loop_pos);
  ASSERT_NE(td::string::npos, message_conversion_pos);
  ASSERT_TRUE(id_check_pos < reference_loop_pos);
  ASSERT_TRUE(id_check_pos < message_conversion_pos);
}
