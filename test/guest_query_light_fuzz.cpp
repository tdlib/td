// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

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

TEST(GuestQueryLightFuzz, RandomizedProbeOrderKeepsRequiredGuestPatternsPinned) {
  auto td_api_tl = read_normalized("td/generate/scheme/td_api.tl");
  auto telegram_api_tl = read_normalized("td/generate/scheme/telegram_api.tl");
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
  auto guest_query_qts_update = read_normalized("td/telegram/GuestQueryQtsUpdate.h");
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
  auto guest_bot_top_dialog = read_normalized("td/telegram/GuestBotTopDialog.h");
  auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");
  auto top_dialog_manager = read_normalized("td/telegram/TopDialogManager.cpp");

  ASSERT_NE(
      td::string::npos,
      messages_manager.find(
          R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->via_business_bot_user_id.is_valid()){return;})"));
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                "if(should_short_circuit_standard_top_dialog_routing(m->guest_bot_via_dialog_id,is_forward)){"));
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
  ASSERT_NE(td::string::npos,
            messages_manager.find(
                "if(old_message->guest_bot_via_dialog_id!=new_message->guest_bot_via_dialog_id){LOG(ERROR)<<\"Changegue"
                "stsenderfrom\"<<old_message->guest_bot_via_dialog_id<<\"to\"<<new_message->guest_bot_via_dialog_id;"
                "old_message->guest_bot_via_dialog_id=new_message->guest_bot_via_dialog_id;need_send_update=true;}"));

  const std::array<td::Slice, 35> required = {
      td::Slice("supports_guest_queries:Bool"),
      td::Slice("updateNewGuestQueryid:int64message:messagereference_messages:vector<message>=Update;"),
      td::Slice("answerGuestQueryguest_query_id:int64result:InputInlineQueryResult=InlineMessageId;"),
      td::Slice("bot_guestchat:flags2.19?true"),
      td::Slice("guestchat_via_from:flags2.19?Peer"),
      td::Slice("messages.setBotGuestChatResult#"),
      td::Slice("if(query_id<=0){returnGuestQueryQtsUpdateResult::InvalidQueryId;}"),
      td::Slice("autoresult=dispatch_guest_query_qts_update("),
      td::Slice("if(guest_query_id<=0){returnpromise.set_error(400,\"Invalidguest_query_idspecified\");}"),
      td::Slice("classSetBotGuestChatResultQueryfinal:publicTd::ResultHandler{"),
      td::Slice("Promise<td_api::object_ptr<td_api::inlineMessageId>>promise_;"),
      td::Slice("telegram_api::messages_setBotGuestChatResult(query_id,std::move(result))"),
      td::Slice("fetch_result<telegram_api::messages_setBotGuestChatResult>(packet);"),
      td::Slice("if(inline_message_id.empty()){returnon_error(Status::Error(500,"
                "\"Receiveinvalidinlinemessageidentifieringuestqueryresult\"));}"),
      td::Slice("promise_.set_value(td_api::make_object<td_api::inlineMessageId>(std::move(inline_message_id)));"),
      td::Slice("boolis_channel_message,boolis_guest_message,constchar*source);"),
      td::Slice("message_info.guest_bot_via_dialog_id=DialogId(message->guestchat_via_from_);"),
      td::Slice(
          R"(automessage_info=parse_telegram_api_message(td_,std::move(message),false,is_business_message,"get_guest_message_object");)"),
      td::Slice(R"(autois_channel_message=message_info.dialog_id.get_type()==DialogType::Channel;)"),
      td::Slice(
          R"(autodialog_message=create_message(td_,std::move(message_info),is_channel_message,true,"get_guest_message_object");)"),
      td::Slice(
          R"(if(!is_guest_message&&dialog_id==my_dialog_id&&(sender_user_id!=my_id||sender_dialog_id.is_valid())){)"),
      td::Slice(
          R"(supposed_to_be_outgoing!=is_outgoing&&!is_guest_message&&content_type!=MessageContentType::ChatDeleteHistory)"),
      td::Slice(
          R"(story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id){)"),
      td::Slice(R"(if(!is_guest_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"),
      td::Slice("m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_unsent()||m->via_bot_"
                "user_id.is_valid()||m->guest_bot_via_dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()"
                "||m->forward_info!=nullptr"),
      td::Slice("if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||m->guest_bot_via_"
                "dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()||!m->sender_user_id.is_valid()||td_->"
                "user_manager_->is_user_bot(m->sender_user_id)||m->forward_info!=nullptr){"),
      td::Slice("last_outgoing_guest_bot_message_date_;"),
      td::Slice("if(note_guest_bot_top_dialog_use(guest_bot_candidate,last_guest_bot_message_date)){"),
      td::Slice("last_message_date>=candidate.message_date"),
      td::Slice("candidate.guest_bot_dialog_id.get_type()!=DialogType::User"),
      td::Slice("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_guest\");"),
      td::Slice("caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsGuestChat>();"),
      td::Slice("query.category==TopDialogCategory::BotInline||query.category==TopDialogCategory::BotGuest||query."
                "category==TopDialogCategory::BotPM"),
      td::Slice("if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_bot_info.ok()"
                ".is_guestchat_bot)){LOG(INFO)<<\"Skipnon-guestbot\"<<user_id;continue;}"),
      td::Slice("casetd_api::topChatCategoryGuestBots::ID:returnTopDialogCategory::BotGuest;"),
  };

  for (int i = 0; i < 12000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    auto snippet = required[idx];
    bool found = td_api_tl.contains(snippet.str()) || telegram_api_tl.contains(snippet.str()) ||
                 updates_manager.contains(snippet.str()) || guest_query_qts_update.contains(snippet.str()) ||
                 inline_queries_manager.contains(snippet.str()) || messages_manager_h.contains(snippet.str()) ||
                 messages_manager.contains(snippet.str()) || guest_bot_top_dialog.contains(snippet.str()) ||
                 top_dialog_category.contains(snippet.str()) || top_dialog_manager.contains(snippet.str());
    ASSERT_TRUE(found);
  }
}

TEST(GuestQueryLightFuzz, RandomizedProbeOrderKeepsLegacyUnsafePatternsAbsent) {
  auto td_api_tl = read_normalized("td/generate/scheme/td_api.tl");
  auto updates_manager = read_normalized("td/telegram/UpdatesManager.cpp");
  auto inline_queries_manager = read_normalized("td/telegram/InlineQueriesManager.cpp");
  auto messages_manager_h = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager = read_normalized("td/telegram/MessagesManager.cpp");
  auto user_manager = read_normalized("td/telegram/UserManager.cpp");
  auto top_dialog_category = read_normalized("td/telegram/TopDialogCategory.cpp");
  auto top_dialog_manager = read_normalized("td/telegram/TopDialogManager.cpp");

  ASSERT_EQ(
      td::string::npos,
      messages_manager.find(
          R"(if(td_->auth_manager_->is_bot()||(!m->is_outgoing&&dialog_id!=td_->dialog_manager_->get_my_dialog_id())||dialog_type==DialogType::SecretChat||!m->message_id.is_any_server()||m->guest_bot_via_dialog_id.is_valid()||m->via_business_bot_user_id.is_valid()){return;})"));
  ASSERT_EQ(
      td::string::npos,
      updates_manager.find(
          "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateBotGuestChatQuery>update,Promise<Unit>&&"
          "promise){promise.set_value(Unit());}"));
  ASSERT_EQ(
      td::string::npos,
      messages_manager.find("dependencies.add(m->via_bot_user_id);dependencies.add(m->via_business_bot_user_id);"));

  const std::array<td::Slice, 24> forbidden = {
      td::Slice("supports_username_queries:Bool"),
      td::Slice("casetelegram_api::topPeerCategoryBotsGuestChat::ID:returnTopDialogCategory::BotInline;"),
      td::Slice("if(update->query_id_==0){LOG(ERROR)<<\"Receiveinvalidguestqueryidentifier\";break;}"),
      td::Slice("for(auto&reference_message:update->reference_messages_){"),
      td::Slice("automessage=td_->messages_manager_->get_guest_message_object(std::move(update->message_),false);"),
      td::Slice("caseTopDialogCategory::BotGuest:returnCSlice(\"bot_inline\");"),
      td::Slice("caseTopDialogCategory::BotGuest:returnmake_tl_object<telegram_api::topPeerCategoryBotsInline>();"),
      td::Slice("if(query.category==TopDialogCategory::BotGuest&&(r_bot_info.ok().username.empty()||!r_bot_info.ok()"
                ".is_inline)){"),
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
          R"(story_dialog_id!=DialogId(sender_user_id)&&story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_business_message){)"),
      td::Slice(
          R"(if(!is_business_message){td->messages_manager_->fix_message_topic(dialog_id,message.get(),false);})"),
      td::Slice("m->message_id.is_scheduled()||m->message_id.is_local()||m->message_id.is_yet_unsent()||m->via_bot_"
                "user_id.is_valid()||m->via_business_bot_user_id.is_valid()||m->forward_info!=nullptr"),
      td::Slice("if(m->is_outgoing||!m->message_id.is_server()||m->via_bot_user_id.is_valid()||m->via_business_"
                "bot_user_id.is_valid()||!m->sender_user_id.is_valid()||td_->user_manager_->is_user_bot(m->sender_"
                "user_id)||m->forward_info!=nullptr){"),
      td::Slice("CHECK(td_->auth_manager_->is_bot());TRY_RESULT_PROMISE(promise,result,get_input_bot_inline_result("
                "std::move(input_result),nullptr,nullptr));td_->create_handler<SetBotGuestChatResultQuery>("
                "std::move(promise))->send(guest_query_id,std::move(result));"),
      td::Slice(
          R"(for(auto&reference_message:update->reference_messages_){automessage=td_->messages_manager_->get_guest_message_object(std::move(reference_message),false);if(message!=nullptr){reference_messages.push_back(std::move(message));}}automessage=td_->messages_manager_->get_guest_message_object(std::move(update->message_),false);if(message==nullptr){LOG(ERROR)<<"Receiveemptyguestmessage";break;}if(update->query_id_<=0){LOG(ERROR)<<"Receiveinvalidguestqueryidentifier";break;})"),
      td::Slice("on_dialog_used(TopDialogCategory::BotGuest,DialogId(m->sender_user_id),m->date);"),
      td::Slice("if(!is_forward&&m->guest_bot_via_dialog_id!=DialogId()&&"),
      td::Slice("type=td_api::make_object<td_api::userTypeBot>(u->can_be_edited_bot,u->can_join_groups,u->can_read_all_"
                "group_messages,u->has_main_app,u->has_bot_forum_view,u->has_bot_forum_view&&!u->can_bot_create_topics,"
                "u->can_manage_bots,u->is_inline_bot,u->inline_query_placeholder,u->need_location_bot,u->is_business_"
                "bot,u->can_be_added_to_attach_menu,u->bot_active_users);"),
      td::Slice(
          "std::move(self_destruct_type),ttl_expires_in,auto_delete_in,via_bot_user_id,via_business_bot_user_id,"),
  };

  for (int i = 0; i < 12000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    auto snippet = forbidden[idx];
    bool found = td_api_tl.contains(snippet.str()) || updates_manager.contains(snippet.str()) ||
                 inline_queries_manager.contains(snippet.str()) || messages_manager.contains(snippet.str()) ||
                 user_manager.contains(snippet.str()) || messages_manager_h.contains(snippet.str()) ||
                 top_dialog_category.contains(snippet.str()) || top_dialog_manager.contains(snippet.str());
    ASSERT_FALSE(found);
  }
}
