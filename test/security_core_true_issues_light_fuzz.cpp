// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_no_space(td::Slice source) {
  td::string out;
  out.reserve(source.size());
  for (auto c : source) {
    unsigned char b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\n' || b == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

TEST(SecurityCoreTrueIssuesLightFuzz, forbidden_legacy_fragments_never_reappear) {
  const auto ip_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/IPAddress.cpp"));
  const auto parser_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c"));
  const auto query_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/QueryCombiner.cpp"));
  const auto session_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
  const auto cli_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp"));
  const auto reply_markup_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
  const auto tl_simple_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdtl/td/tl/tl_simple.h"));

  const td::string patterns[] = {
      "std::memcpy(&ipv6_addr_,reinterpret_cast<constsockaddr_in6*>(addr),sizeof(ipv6_addr_));",
      "std::memcpy(&ipv4_addr_,reinterpret_cast<constsockaddr_in*>(addr),sizeof(ipv4_addr_));",
      // V512 (CWE-119) fix: the intermediate staging-buffer pattern must not reappear.
      "sockaddr_storagenormalized_addr{};",
      "std::memcpy(&normalized_addr,addr,len);",
      "TL_ERROR(\"Natvarcannotpreceedwith%%\\n\");return0;",
      "CHECK(!query.send_query);",
      "force_close(static_cast<mtproto::SessionConnection::Callback*>(this));CHECK(info->state_==ConnectionInfo::State:"
      ":Empty);",
      "get_args(args,file_id,offset,limit,priority);if(priority<=0){priority=1;}int32max_file_id=file_id.file_id;",
      "int64story_sound_id=0;",
      "while(nextch()!=10);",
      "Typetype;",
      "}type;",
  };

  for (int i = 0; i < 15000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    const auto &pattern = patterns[idx];
    ASSERT_EQ(td::string::npos, ip_source.find(pattern));
    ASSERT_EQ(td::string::npos, parser_source.find(pattern));
    ASSERT_EQ(td::string::npos, query_source.find(pattern));
    ASSERT_EQ(td::string::npos, session_source.find(pattern));
    ASSERT_EQ(td::string::npos, cli_source.find(pattern));
    ASSERT_EQ(td::string::npos, reply_markup_source.find(pattern));
    ASSERT_EQ(td::string::npos, tl_simple_source.find(pattern));
  }

  ASSERT_TRUE(parser_source.find("while(curch&&nextch()!=10){}") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("Typetype=Type::Text;") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("Typetype=Type::Url;") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("Typetype=Type::InlineKeyboard;") != td::string::npos);
  ASSERT_TRUE(tl_simple_source.find("}type=Int32;") != td::string::npos);
}

// V730/V501 regression fuzz: ensure that none of the uninitialized-member or
// double-init forbidden patterns reappear in any of the affected source files.
TEST(SecurityCoreTrueIssuesLightFuzz, v730_v501_forbidden_patterns_stay_absent) {
  const auto forum_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ForumTopicManager.cpp"));
  const auto group_call_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/GroupCallManager.cpp"));
  const auto inline_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp"));
  const auto chat_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp"));
  const auto cli_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp"));

  struct SourcePattern {
    const td::string *source;
    const char *pattern;
  };

  const td::string *sources[] = {&forum_source, &group_call_source, &inline_source, &chat_source, &cli_source};
  const char *patterns[] = {
      "int64random_id_;",
      "int32message_id_;",
      "int64paid_message_star_count_;",
      "boolis_live_story_;",
      "uint64query_hash_;",
      "boolis_active_;",
      "int32unrestrict_boost_count_;",
      "boolis_all_history_available_;",
      "boolcan_have_sponsored_messages_;",
      "boolhas_automatic_translation_;",
      "boolhas_hidden_participants_;",
      "boolhas_aggressive_anti_spam_enabled_;",
      "PublicDialogTypetype_;",
      "file_log.init(file_name.str()).is_ok()&&file_log.init(file_name.str(),1000<<20).is_ok()",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(chat_source.find("boolis_active_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("int32unrestrict_boost_count_=0;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("boolis_all_history_available_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("boolcan_have_sponsored_messages_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("boolhas_automatic_translation_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("boolhas_hidden_participants_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("boolhas_aggressive_anti_spam_enabled_=false;") != td::string::npos);
  ASSERT_TRUE(chat_source.find("PublicDialogTypetype_=PublicDialogType::ForPersonalDialog;") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v730_core_runtime_forbidden_patterns_stay_absent) {
  const auto sequence_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));
  const auto reaction_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageReaction.cpp"));
  const auto business_connection_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp"));
  const auto notification_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/NotificationSettingsManager.cpp"));
  const auto translation_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/TranslationManager.cpp"));

  const td::string *sources[] = {&sequence_source, &reaction_source, &business_connection_source, &notification_source,
                                 &translation_source};
  const char *patterns[] = {
      "Statestate_;",
      "int64star_count_;",
      "classGetBusinessStarTransferPaymentFormQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
      "BusinessConnectionIdbusiness_connection_id_;int64star_count_;",
      "classGetScopeNotifySettingsQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
      "NotificationSettingsScopescope_;",
      "classUpdateScopeNotifySettingsQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
      "NotificationSettingsScopescope_;",
      "boolskip_bot_commands_;",
      "int32max_media_timestamp_;",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(sequence_source.find("Statestate_=State::Start;") != td::string::npos);
  ASSERT_TRUE(reaction_source.find("int64star_count_=0;") != td::string::npos);
  ASSERT_TRUE(
      business_connection_source.find("classGetBusinessStarTransferPaymentFormQueryfinal:publicTd::ResultHandler{"
                                      "Promise<Unit>promise_;BusinessConnectionIdbusiness_connection_id_;"
                                      "int64star_count_=0;") != td::string::npos);
  ASSERT_TRUE(notification_source.find("classGetScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;"
                                       "NotificationSettingsScopescope_=NotificationSettingsScope::Private;") !=
              td::string::npos);
  ASSERT_TRUE(notification_source.find("classUpdateScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;"
                                       "NotificationSettingsScopescope_=NotificationSettingsScope::Private;") !=
              td::string::npos);
  ASSERT_TRUE(translation_source.find("boolskip_bot_commands_=false;") != td::string::npos);
  ASSERT_TRUE(translation_source.find("int32max_media_timestamp_=-1;") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v730_request_handlers_forbidden_patterns_stay_absent) {
  const auto background_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BackgroundManager.cpp"));
  const auto business_connection_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp"));
  const auto inline_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp"));
  const auto ts_file_log_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/TsFileLog.cpp"));

  const td::string *sources[] = {&background_source, &business_connection_source, &inline_source, &ts_file_log_source};
  const char *patterns[] = {
      "boolfor_dark_theme_;",
      "Promise<td_api::object_ptr<td_api::upgradedGift>>promise_;DialogIdowner_dialog_id_;stringname_;int64star_count_"
      ";",
      "classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
      "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
      "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_;",
      "std::atomic<bool>is_inited{false};size_tid;",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(background_source.find("boolfor_dark_theme_=false;") != td::string::npos);
  ASSERT_TRUE(business_connection_source.find("int64star_count_=0;") != td::string::npos);
  ASSERT_TRUE(inline_source.find("classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
                                 "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
                                 "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_=0;") != td::string::npos);
  ASSERT_TRUE(ts_file_log_source.find("std::atomic<bool>is_inited{false};size_tid{0};") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v730_forbidden_patterns_stay_absent) {
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.h"));
  const auto buffer_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/buffer.h"));

  const td::string *sources[] = {&net_stats_source, &auth_data_source, &buffer_source};
  const char *patterns[] = {
      "NetStatsDatalast_sync_stats;",
      "NetStatsInfocommon_net_stats_;",
      "NetStatsInfomedia_net_stats_;",
      "NetStatsInfocall_net_stats_;",
      "std::array<TypeStats,5/*NetStatsManager::net_type_size()*/>stats_by_type;",
      "MessageIdDuplicateChecker<1000>duplicate_checker_;",
      "MessageIdDuplicateChecker<1000>updates_duplicate_checker_;",
      "MessageIdDuplicateChecker<100>updates_duplicate_rechecker_;",
      "size_tdata_size_;",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(net_stats_source.find("NetStatsDatalast_sync_stats{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocommon_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfomedia_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocall_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("std::array<TypeStats,5/*NetStatsManager::net_type_size()*/>stats_by_type{};") !=
              td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_{};") != td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>updates_duplicate_checker_{};") !=
              td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<100>updates_duplicate_rechecker_{};") !=
              td::string::npos);
  ASSERT_TRUE(buffer_source.find("size_tdata_size_{0};") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v730_reply_markup_and_sequence_dispatcher_forbidden_patterns_stay_absent) {
  const auto reply_markup_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
  const auto sequence_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));

  const td::string *sources[] = {&reply_markup_source, &sequence_source};
  const char *patterns[] = {
      "KeyboardButtonStylestyle;",
      "UserIduser_id;",
      "NetQueryRefnet_query_ref_;",
      "NetQueryPtrquery_;",
      "ActorShared<NetQueryCallback>callback_;",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(reply_markup_source.find("KeyboardButtonStylestyle{};") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("UserIduser_id{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("NetQueryRefnet_query_ref_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("NetQueryPtrquery_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("ActorShared<NetQueryCallback>callback_{};") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v730_message_handlers_message_handler_forbidden_patterns_stay_absent) {
  const auto message_query_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageQueryManager.cpp"));
  const auto messages_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

  const td::string *sources[] = {&message_query_source, &messages_source};
  const char *patterns[] = {
      "classSearchMessagesGlobalQueryfinal:publicTd::ResultHandler{"
      "Promise<td_api::object_ptr<td_api::foundMessages>>promise_;stringquery_;int32offset_date_{0};"
      "DialogIdoffset_dialog_id_;MessageIdoffset_message_id_;int32limit_{0};MessageSearchFilterfilter_;",
      "classGetMessagePositionQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;DialogIddialog_id_;"
      "MessageIdmessage_id_;MessageSearchFilterfilter_;MessageTopicmessage_topic_;",
      "classGetSearchResultPositionsQueryfinal:publicTd::ResultHandler{"
      "Promise<td_api::object_ptr<td_api::messagePositions>>promise_;DialogIddialog_id_;"
      "SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_;",
      "classGetSearchCountersQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;DialogIddialog_id_;"
      "MessageTopicmessage_topic_;MessageSearchFilterfilter_;",
      "classGetAllScheduledMessagesQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;DialogIddialog_id_;"
      "uint32generation_;",
      "classEditMessageQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;DialogIddialog_id_;"
      "MessageIdmessage_id_;boolis_media_;",
      "classSendScreenshotNotificationQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;int64random_id_;"
      "DialogIddialog_id_;",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(message_query_source.find("MessageSearchFilterfilter_{};") != td::string::npos);
  ASSERT_TRUE(messages_source.find("SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_{};") !=
              td::string::npos);
  ASSERT_TRUE(messages_source.find("MessageTopicmessage_topic_;MessageSearchFilterfilter_{};") != td::string::npos);
  ASSERT_TRUE(messages_source.find("DialogIddialog_id_;uint32generation_{0};") != td::string::npos);
  ASSERT_TRUE(messages_source.find("MessageIdmessage_id_;boolis_media_=false;") != td::string::npos);
  ASSERT_TRUE(messages_source.find("Promise<Unit>promise_;int64random_id_=0;DialogIddialog_id_;") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesLightFuzz, v547_v730_session_ctor_forbidden_patterns_stay_absent) {
  const auto session_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.cpp"));
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));

  const td::string *sources[] = {&session_source, &auth_data_source, &net_stats_source};
  const char *patterns[] = {
      "input.shutdown_requested=close_flag_||logging_out_flag_;",
      "AuthData::AuthData(){",
      "explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)){",
  };

  constexpr int kIter = 10000;
  for (int i = 0; i < kIter; i++) {
    auto src_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(sources) / sizeof(sources[0])) - 1));
    auto pat_idx =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(sizeof(patterns) / sizeof(patterns[0])) - 1));
    ASSERT_EQ(td::string::npos, sources[src_idx]->find(patterns[pat_idx]));
  }

  ASSERT_TRUE(session_source.find("input.shutdown_requested=logging_out_flag_;") != td::string::npos);
  ASSERT_TRUE(auth_data_source.find("AuthData::AuthData():duplicate_checker_{},updates_duplicate_checker_{},"
                                    "updates_duplicate_rechecker_{}{") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)),"
                                    "common_net_stats_{},media_net_stats_{},call_net_stats_{}{") != td::string::npos);
}
