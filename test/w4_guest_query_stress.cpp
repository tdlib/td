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

TEST(W4GuestQueryStress, RepeatedSourceReadsKeepGuestBundleInvariantsStable) {
  constexpr int kIterations = 3000;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto td_api_tl = read_normalized("td/generate/scheme/td_api.tl");
    auto telegram_api_tl = read_normalized("td/generate/scheme/telegram_api.tl");
    auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
    auto guest_query_qts_update = read_normalized("td/telegram/GuestQueryQtsUpdate.h");
    auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
    auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
    auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
    auto guest_bot_top_dialog = read_normalized("td/telegram/GuestBotTopDialog.h");
    auto user_manager = read_normalized("td/telegram/UserManager.cpp");
    auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");
    auto top_dialog_manager = read_normalized("td/telegram/TopDialogManager.cpp");

    ASSERT_NE(td::string::npos, td_api_tl.find("supports_guest_queries:Bool"));
    ASSERT_NE(td::string::npos,
              td_api_tl.find("answerGuestQueryguest_query_id:int64result:InputInlineQueryResult=InlineMessageId;"));
    ASSERT_NE(td::string::npos, telegram_api_tl.find("bot_guestchat:flags2.19?true"));
    ASSERT_NE(td::string::npos, telegram_api_tl.find("messages.setBotGuestChatResult#"));
    ASSERT_NE(td::string::npos,
              guest_query_qts_update.find("if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;}"));
    ASSERT_NE(td::string::npos, updates_manager.find("autoresult=dispatch_guest_query_qts_update("));
    ASSERT_NE(td::string::npos, updates_manager.find("casetelegram_api::updateBotGuestChatQuery::ID:returntrue;"));
    ASSERT_NE(td::string::npos, updates_manager.find("casetelegram_api::updateBotGuestChatQuery::ID:returnstatic_cast<"
                                                     "consttelegram_api::updateBotGuestChatQuery*"
                                                     ">(update)->qts_;"));
    ASSERT_NE(
        td::string::npos,
        updates_manager.find(
            "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
            "promise){autoqts=update->qts_;add_pending_qts_update(std::move(update),qts,std::move(promise));}"));
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
    ASSERT_EQ(
        td::string::npos,
        updates_manager.find(
            "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
            "promise){promise.set_value(Unit());}"));
    ASSERT_NE(td::string::npos,
              inline_queries_manager.find("promise_.set_value(td_api::make_object<td_api::inlineMessageId>("
                                          "InlineQueriesManager::get_inline_message_id(std::move(ptr))));"));
    ASSERT_NE(td::string::npos,
              messages_manager_h.find("boolis_channel_message,boolis_guest_message,constchar*source);"));
    ASSERT_NE(td::string::npos,
              messages_manager.find("message_info.guest_bot_via_dialog_id=DialogId(message->guestchat_via_from_);"));
    ASSERT_NE(
        td::string::npos,
        messages_manager.find(
            R"(automessage_info=parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object");)"));
    ASSERT_NE(
        td::string::npos,
        messages_manager.find(R"(autois_channel_message=message_info.dialog_id.get_type()==DialogType::Channel;)"));
    ASSERT_NE(
        td::string::npos,
        messages_manager.find(
            R"(autodialog_message=create_message(td_,std::move(message_info),is_channel_message,true,"get_guest_message_object");)"));
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
    ASSERT_NE(td::string::npos,
              messages_manager_h.find(
                  "td_api::object_ptr<td_api::MessageSender>get_message_guest_sender_object(constMessage*m)const;"));
    ASSERT_NE(td::string::npos, messages_manager.find("if(m->guest_bot_via_dialog_id==DialogId()){returnnullptr;}"));
    ASSERT_NE(td::string::npos,
              messages_manager.find(
                  "returnget_message_sender_object_const(td_,m->guest_bot_via_dialog_id,\"get_message_guest_sender_"
                  "object\");"));
    ASSERT_NE(td::string::npos, messages_manager.find("get_message_guest_sender_object(m),via_business_bot_user_id"));
    ASSERT_NE(td::string::npos,
              messages_manager.find("dependencies.add_message_sender_dependencies(m->guest_bot_via_dialog_id);"));
    ASSERT_NE(
        td::string::npos,
        messages_manager.find(
            "if(old_message->guest_bot_via_dialog_id!=new_message->guest_bot_via_dialog_id){LOG(ERROR)<<\"Changegue"
            "stsenderfrom\"<<old_message->guest_bot_via_dialog_id<<\"to\"<<new_message->guest_bot_via_dialog_id;"
            "old_message->guest_bot_via_dialog_id=new_message->guest_bot_via_dialog_id;need_send_update=true;}"));
    ASSERT_NE(td::string::npos, messages_manager_h.find("last_outgoing_guest_bot_message_date_;"));
    ASSERT_NE(
        td::string::npos,
        messages_manager.find("if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_date)){"));
    ASSERT_NE(td::string::npos, guest_bot_top_dialog.find("last_message_date>=candidate.message_date"));
    ASSERT_NE(td::string::npos, user_manager.find("bot_data.is_guestchat_bot=u->is_guestchat_bot;"));
    ASSERT_NE(td::string::npos,
              top_dialog_category.find("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_guest\");"));
    ASSERT_NE(td::string::npos,
              top_dialog_category.find(
                  "casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotGuest;"));
    ASSERT_NE(
        td::string::npos,
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
    ASSERT_NE(td::string::npos, top_dialog_manager.find("query.category==TopDialogCategory::BotGuest"));

    ASSERT_EQ(td::string::npos, td_api_tl.find("supports_username_queries:Bool"));
    ASSERT_EQ(td::string::npos,
              top_dialog_category.find(
                  "casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotInline;"));
    ASSERT_EQ(td::string::npos,
              top_dialog_category.find("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_inline\");"));
    ASSERT_EQ(td::string::npos,
              top_dialog_category.find(
                  "caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsInline>();"));
    ASSERT_EQ(td::string::npos,
              top_dialog_manager.find(
                  "if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_bot_info.ok()"
                  ".is_inline)){"));
    ASSERT_EQ(td::string::npos, top_dialog_manager.find(
                                    "query.category==TopDialogCategory::BotGuest&&!r_bot_info.ok().is_guestchat_bot"));
    ASSERT_EQ(td::string::npos,
              updates_manager.find("if(update->query_id_==0){LOG(ERROR)<<\"Receiveinvalidguestqueryidentifier\";"
                                   "break;}"));
    ASSERT_EQ(td::string::npos, updates_manager.find("for(auto&reference_message:update->reference_messages_){"));
    ASSERT_EQ(td::string::npos,
              updates_manager.find("automessage=td_->messages_manager_->get_guest_message_object(std::move(update->"
                                   "message_),false);"));
    ASSERT_EQ(td::string::npos,
              inline_queries_manager.find(
                  "classSetBotGuestChatResultQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"));
    ASSERT_EQ(td::string::npos,
              inline_queries_manager.find(
                  "create_handler<SetInlineBotResultsQuery>(std::move(promise))->send(guest_query_id,"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find(
            R"(autodialog_message=create_message(td_,parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object"),false,is_business_message,"get_guest_message_object");)"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find(
            R"(if(!is_business_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find(
            R"(supposed_to_be_outgoing!=is_outgoing&&!is_business_message&&content_type!=MessageContentType::ChatDeleteHistory)"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find(
            R"(if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"));
    ASSERT_EQ(td::string::npos,
              messages_manager.find("m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_"
                                    "unsent()||m->via_bot_user_id.is_valid()||m->via_business_bot_user_id.is_valid()"
                                    "||m->forward_info!=nullptr"));
    ASSERT_EQ(td::string::npos,
              messages_manager.find("if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||"
                                    "m->via_business_bot_user_id.is_valid()||!m->sender_user_id.is_valid()||td_->"
                                    "user_manager_->is_user_bot(m->sender_user_id)||m->forward_info!=nullptr){"));
    ASSERT_EQ(td::string::npos,
              inline_queries_manager.find(
                  "CHECK(td_->auth_manager_->is_bot());TRY_RESULT_PROMISE(promise,result,get_input_bot_inline_result("
                  "std::move(input_result),nullptr,nullptr));td_->create_handler<SetBotGuestChatResultQuery>("
                  "std::move(promise))->send(guest_query_id,std::move(result));"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find("on_dialog_used(TopDialogCategory::BotGuest,DialogId(m->sender_user_id),m->date);"));
    ASSERT_EQ(td::string::npos,
              top_dialog_manager.find(
                  "if(query.category==TopDialogCategory::BotInline||query.category==TopDialogCategory::BotPM){"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find(
            R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->guest_bot_via_dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()){return;})"));
    ASSERT_EQ(
        td::string::npos,
        messages_manager.find("dependencies.add(m->via_bot_user_id);dependencies.add(m->via_business_bot_user_id);"));

    checksum += static_cast<td::uint32>(td_api_tl.size() + telegram_api_tl.size() + updates_manager.size() +
                                        guest_query_qts_update.size() + inline_queries_manager.size() +
                                        messages_manager_h.size() + messages_manager.size() +
                                        guest_bot_top_dialog.size() + user_manager.size() + top_dialog_category.size() +
                                        top_dialog_manager.size() + static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}
