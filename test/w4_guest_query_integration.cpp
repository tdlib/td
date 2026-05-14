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

TEST(W4GuestQueryIntegration, RequiredGuestQueryPipelinePatternsExist) {
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
  auto guest_query_qts_update = read_normalized("td/telegram/GuestQueryQtsUpdate.h");
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto requests = read_normalized("td/telegram/Requests.cpp");
  auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
  auto guest_bot_top_dialog = read_normalized("td/telegram/GuestBotTopDialog.h");
  auto user_manager = read_normalized("td/telegram/UserManager.cpp");
  auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");
  auto top_dialog_manager = read_normalized("td/telegram/TopDialogManager.cpp");
  auto cli = read_normalized("td/telegram/cli.cpp");

  const std::vector<td::Slice> updates_required = {
      "casetelegram_api::updateBotGuestChatQuery::ID:{",
      "autoresult=dispatch_guest_query_qts_update(",
      "caseGuestQueryQtsUpdateResult::InvalidQueryId:",
      "caseGuestQueryQtsUpdateResult::EmptyMessage:",
      "casetelegram_api::updateBotGuestChatQuery::ID:returntrue;",
      td::Slice(
          "casetelegram_api::updateBotGuestChatQuery::ID:returnstatic_cast<consttelegram_api::updateBotGuestChatQuery*"
          ">(update)->qts_;"),
      td::Slice(
          "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
          "promise){autoqts=update->qts_;add_pending_qts_update(std::move(update),qts,std::move(promise));}"),
  };
  for (auto snippet : updates_required) {
    ASSERT_NE(td::string::npos, updates_manager.find(snippet.str()));
  }

  auto helper_id_check_pos =
      guest_query_qts_update.find(R"(if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;})");
  auto helper_reference_loop_pos = guest_query_qts_update.find(R"(for(auto&reference_message:reference_messages){)");
  auto helper_message_conversion_pos =
      guest_query_qts_update.find(R"(autoconverted_message=convert_message(std::move(message));)");
  ASSERT_NE(td::string::npos, helper_id_check_pos);
  ASSERT_NE(td::string::npos, helper_reference_loop_pos);
  ASSERT_NE(td::string::npos, helper_message_conversion_pos);
  ASSERT_TRUE(helper_id_check_pos < helper_reference_loop_pos);
  ASSERT_TRUE(helper_id_check_pos < helper_message_conversion_pos);
  ASSERT_NE(td::string::npos,
            guest_query_qts_update.find(
                R"(send_update(query_id,std::move(converted_message),std::move(converted_reference_messages));)"));

  ASSERT_NE(td::string::npos,
            inline_queries_manager.find("if(guest_query_id<=0){returnpromise.set_error(400,\"Invalidguest_query_"
                                        "idspecified\");}"));
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
  ASSERT_NE(td::string::npos, inline_queries_manager.find("td_->create_handler<SetBotGuestChatResultQuery>(std::move("
                                                          "promise))->send(guest_query_id,std::move(result));"));
  ASSERT_NE(td::string::npos, requests.find("td_->inline_queries_manager_->answer_guest_query(request.guest_query_id_,"
                                            "std::move(request.result_),std::move(promise));"));
  ASSERT_NE(td::string::npos,
            messages_manager.find("message_info.guest_bot_via_dialog_id=DialogId(message->guestchat_via_from_);"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(automessage_info=parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object");)"));
  ASSERT_NE(td::string::npos,
            messages_manager_h.find(
                "td_api::object_ptr<td_api::MessageSender>get_message_guest_sender_object(constMessage*m)const;"));
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
  ASSERT_NE(td::string::npos, messages_manager.find("get_message_guest_sender_object(m),via_business_bot_user_id"));
  ASSERT_NE(td::string::npos,
            messages_manager.find("dependencies.add_message_sender_dependencies(m->guest_bot_via_dialog_id);"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                "if(old_message->guest_bot_via_dialog_id!=new_message->guest_bot_via_dialog_id){LOG(ERROR)<<\"Changegue"
                "stsenderfrom\"<<old_message->guest_bot_via_dialog_id<<\"to\"<<new_message->guest_bot_via_dialog_id;"
                "old_message->guest_bot_via_dialog_id=new_message->guest_bot_via_dialog_id;need_send_update=true;}"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(if(!is_guest_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(supposed_to_be_outgoing!=is_outgoing&&!is_guest_message&&content_type!=MessageContentType::ChatDeleteHistory)"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                R"(if(!is_guest_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"));
  ASSERT_NE(td::string::npos,
            messages_manager.find("m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_"
                                  "unsent()||m->via_bot_user_id.is_valid()||m->guest_bot_via_dialog_id.is_valid()"
                                  "||m->via_business_bot_user_id.is_valid()||m->forward_info!=nullptr"));
  ASSERT_NE(td::string::npos,
            messages_manager.find("if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||"
                                  "m->guest_bot_via_dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()||"
                                  "!m->sender_user_id.is_valid()||td_->user_manager_->is_user_bot(m->sender_user_"
                                  "id)||m->forward_info!=nullptr){"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->via_business_bot_user_id.is_valid()){return;})"));
  ASSERT_NE(td::string::npos, messages_manager.find(R"(if(m->guest_bot_via_dialog_id.is_valid()){if(!is_forward){)"));
  ASSERT_NE(td::string::npos, messages_manager_h.find("last_outgoing_guest_bot_message_date_;"));
  ASSERT_NE(td::string::npos, messages_manager.find("if(m->guest_bot_via_dialog_id.is_valid()){"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find("if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_date)){"));
  ASSERT_NE(
      td::string::npos,
      messages_manager.find("last_outgoing_guest_bot_message_date_[guest_bot_dialog_id]=last_guest_bot_message_date;"));
  ASSERT_NE(td::string::npos, guest_bot_top_dialog.find("last_message_date>=candidate.message_date"));
  ASSERT_NE(td::string::npos, guest_bot_top_dialog.find("candidate.guest_bot_dialog_id.get_type()!=DialogType::User"));
  ASSERT_NE(td::string::npos, user_manager.find("bot_data.is_guestchat_bot=u->is_guestchat_bot;"));
  ASSERT_NE(td::string::npos, top_dialog_category.find("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_guest\");"));
  ASSERT_NE(td::string::npos,
            top_dialog_category.find("casetd_api::topChatCategoryGuestBots::ID:returnTopDialogCategory::BotGuest;"));
  ASSERT_NE(td::string::npos,
            top_dialog_category.find(
                "casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotGuest;"));
  ASSERT_NE(td::string::npos,
            top_dialog_category.find(
                "caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsGuestChat>();"));
  ASSERT_NE(td::string::npos,
            top_dialog_manager.find(
                "query.category==TopDialogCategory::BotInline||query.category==TopDialogCategory::BotGuest||query."
                "category==TopDialogCategory::BotPM"));
  ASSERT_NE(td::string::npos,
            top_dialog_manager.find(
                "if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_bot_info.ok()"
                ".is_guestchat_bot)){LOG(INFO)<<\"Skipnon-guestbot\"<<user_id;continue;}"));
  ASSERT_NE(td::string::npos, top_dialog_manager.find("telegram_api::contacts_getTopPeers(0,true,true,true,true,true,"
                                                      "true,true,true,true,true,0/*offset*/,100/*limit*/,hash)"));
  ASSERT_NE(td::string::npos, top_dialog_manager.find("query.category==TopDialogCategory::BotGuest"));
  ASSERT_NE(td::string::npos,
            cli.find("}elseif(category==\"guest\"){returntd_api::make_object<td_api::topChatCategoryGuestBots>();}"));
}

TEST(W4GuestQueryIntegration, LegacyGuestQueryUnsafePatternsAreAbsent) {
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
  auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");
  auto top_dialog_manager = read_normalized("td/telegram/TopDialogManager.cpp");

  const std::vector<td::Slice> forbidden = {
      "casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotInline;",
      td::Slice(
          "telegram_api::contacts_getTopPeers(0,true,true,true,true,true,true,true,true,true,0/*offset*/,100/*limit*/"
          ",hash)"),
      "if(query.category==TopDialogCategory::BotInline||query.category==TopDialogCategory::BotPM){",
      td::Slice("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_inline\");"),
      td::Slice("caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsInline>();"),
      td::Slice(
          "m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_unsent()||m->via_bot_user_"
          "id.is_valid()||m->via_business_bot_user_id.is_valid()||m->forward_info!=nullptr"),
      td::Slice(
          "if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||m->via_business_bot_user_"
          "id.is_valid()||!m->sender_user_id.is_valid()||td_->user_manager_->is_user_bot(m->sender_user_id)||m->"
          "forward_info!=nullptr){"),
      td::Slice(
          "if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_bot_info.ok().is_"
          "inline)){"),
      td::Slice("query.category==TopDialogCategory::BotGuest&&!r_bot_info.ok().is_guestchat_bot"),
      td::Slice("classSetBotGuestChatResultQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"),
      td::Slice("create_handler<SetInlineBotResultsQuery>(std::move(promise))->send(guest_query_id,"),
      td::Slice(
          R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object"),false,is_business_message,"get_guest_message_object");)"),
      td::Slice(
          R"(if(!is_business_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"),
      td::Slice(
          R"(supposed_to_be_outgoing!=is_outgoing&&!is_business_message&&content_type!=MessageContentType::ChatDeleteHistory)"),
      td::Slice(
          R"(if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"),
      "m->via_bot_user_id.is_valid()||m->via_business_bot_user_id.is_valid())",
      "std::move(self_destruct_type),ttl_expires_in,auto_delete_in,via_bot_user_id,via_business_bot_user_id,",
      "if(update->query_id_==0){LOG(ERROR)<<\"Receiveinvalidguestqueryidentifier\";break;}",
      R"(for(auto&reference_message:update->reference_messages_){)",
      R"(automessage=td_->messages_manager_->get_guest_message_object(std::move(update->message_),false);)",
      td::Slice(
          "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
          "promise){promise.set_value(Unit());}"),
      "on_dialog_used(TopDialogCategory::BotGuest,DialogId(m->sender_user_id),m->date);",
      "if(!is_forward&&m->guest_bot_via_dialog_id!=DialogId()&&",
      td::Slice(
          "CHECK(td_->auth_manager_->is_bot());TRY_RESULT_PROMISE(promise,result,get_input_bot_inline_result(std::move("
          "input_result),nullptr,nullptr));td_->create_handler<SetBotGuestChatResultQuery>(std::move(promise))->send("
          "guest_"
          "query_id,std::move(result));"),
  };

  ASSERT_EQ(td::string::npos, top_dialog_category.find(forbidden[0].str()));
  ASSERT_EQ(td::string::npos, top_dialog_manager.find(forbidden[1].str()));
  ASSERT_EQ(td::string::npos, top_dialog_manager.find(forbidden[2].str()));
  ASSERT_EQ(td::string::npos, top_dialog_category.find(forbidden[3].str()));
  ASSERT_EQ(td::string::npos, top_dialog_category.find(forbidden[4].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[5].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[6].str()));
  ASSERT_EQ(
      td::string::npos,
      messages_manager.find("dependencies.add(m->via_bot_user_id);dependencies.add(m->via_business_bot_user_id);"));
  ASSERT_EQ(td::string::npos, top_dialog_manager.find(forbidden[7].str()));
  ASSERT_EQ(td::string::npos, top_dialog_manager.find(forbidden[8].str()));
  ASSERT_EQ(td::string::npos, inline_queries_manager.find(forbidden[9].str()));
  ASSERT_EQ(td::string::npos, inline_queries_manager.find(forbidden[10].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[11].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[12].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[13].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[14].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[15].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[16].str()));
  ASSERT_EQ(
      td::string::npos,
      messages_manager.find(
          R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->guest_bot_via_dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()){return;})"));
  ASSERT_EQ(td::string::npos, updates_manager.find(forbidden[17].str()));
  ASSERT_EQ(td::string::npos, updates_manager.find(forbidden[18].str()));
  ASSERT_EQ(td::string::npos, updates_manager.find(forbidden[19].str()));
  ASSERT_EQ(td::string::npos, updates_manager.find(forbidden[20].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[21].str()));
  ASSERT_EQ(td::string::npos, messages_manager.find(forbidden[22].str()));
  ASSERT_EQ(td::string::npos, inline_queries_manager.find(forbidden[23].str()));
}
