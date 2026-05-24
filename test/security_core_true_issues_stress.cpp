// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
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

TEST(SecurityCoreTrueIssuesStress, repeated_source_reads_preserve_hardened_invariants) {
  constexpr int kIterations = 3500;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto ip_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/IPAddress.cpp"));
    auto parser_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c"));
    auto query_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/QueryCombiner.cpp"));
    auto session_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
    auto cli_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp"));
    auto reply_markup_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
    auto tl_simple_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdtl/td/tl/tl_simple.h"));

    ASSERT_EQ(
        td::string::npos,
        ip_source.find("std::memcpy(&ipv6_addr_,reinterpret_cast<constsockaddr_in6*>(addr),sizeof(ipv6_addr_));"));
    // V512 (CWE-119) fix guards: intermediate staging buffer must not reappear.
    ASSERT_EQ(td::string::npos, ip_source.find("sockaddr_storagenormalized_addr{};"));
    ASSERT_EQ(td::string::npos, ip_source.find("std::memcpy(&normalized_addr,addr,len);"));
    // The fixed direct-copy via const void* must remain present.
    ASSERT_TRUE(ip_source.find("std::memcpy(&ipv6_addr_,static_cast<constvoid*>(addr),sizeof(ipv6_addr_));") !=
                td::string::npos);
    ASSERT_TRUE(ip_source.find("std::memcpy(&ipv4_addr_,static_cast<constvoid*>(addr),sizeof(ipv4_addr_));") !=
                td::string::npos);
    ASSERT_EQ(td::string::npos, parser_source.find("TL_ERROR(\"Natvarcannotpreceedwith%%\\n\");return0;"));
    ASSERT_EQ(td::string::npos, query_source.find("CHECK(!query.send_query);"));
    ASSERT_EQ(td::string::npos,
              session_source.find("force_close(static_cast<mtproto::SessionConnection::Callback*>(this));"
                                  "CHECK(info->state_==ConnectionInfo::State::Empty);"));
    ASSERT_EQ(td::string::npos,
              cli_source.find("get_args(args,file_id,offset,limit,priority);if(priority<=0){priority=1;}"
                              "int32max_file_id=file_id.file_id;"));
    ASSERT_EQ(td::string::npos, parser_source.find("while(nextch()!=10);"));
    ASSERT_TRUE(parser_source.find("while(curch&&nextch()!=10){}") != td::string::npos);
    ASSERT_EQ(td::string::npos, reply_markup_source.find("Typetype;"));
    ASSERT_TRUE(reply_markup_source.find("Typetype=Type::Text;") != td::string::npos);
    ASSERT_TRUE(reply_markup_source.find("Typetype=Type::Url;") != td::string::npos);
    ASSERT_TRUE(reply_markup_source.find("Typetype=Type::InlineKeyboard;") != td::string::npos);
    ASSERT_EQ(td::string::npos, tl_simple_source.find("}type;"));
    ASSERT_TRUE(tl_simple_source.find("}type=Int32;") != td::string::npos);

    checksum += static_cast<td::uint32>(ip_source.size() ^ parser_source.size() ^ query_source.size() ^
                                        session_source.size() ^ cli_source.size() ^ reply_markup_source.size() ^
                                        tl_simple_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

// V730/V501 stress: sustained repeated reads of all affected source files must
// never detect a regression in uninitialized-member or double-init patterns.
TEST(SecurityCoreTrueIssuesStress, v730_v501_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1500;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto forum_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ForumTopicManager.cpp"));
    const auto group_call_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/GroupCallManager.cpp"));
    const auto inline_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp"));
    const auto chat_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp"));
    const auto cli_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp"));

    // V730: bare (uninitialized) member declarations must be absent.
    ASSERT_EQ(td::string::npos, forum_source.find("int64random_id_;"));
    ASSERT_EQ(td::string::npos, group_call_source.find("int32message_id_;"));
    ASSERT_EQ(td::string::npos, group_call_source.find("int64paid_message_star_count_;"));
    ASSERT_EQ(td::string::npos, group_call_source.find("boolis_live_story_;"));
    ASSERT_EQ(td::string::npos, inline_source.find("uint64query_hash_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolis_active_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("int32unrestrict_boost_count_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolis_all_history_available_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolcan_have_sponsored_messages_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolhas_automatic_translation_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolhas_hidden_participants_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("boolhas_aggressive_anti_spam_enabled_;"));
    ASSERT_EQ(td::string::npos, chat_source.find("PublicDialogTypetype_;"));

    // V730: zero-initialized forms must be present.
    ASSERT_TRUE(forum_source.find("int64random_id_=0;") != td::string::npos);
    ASSERT_TRUE(group_call_source.find("int32message_id_=0;") != td::string::npos);
    ASSERT_TRUE(group_call_source.find("int64paid_message_star_count_=0;") != td::string::npos);
    ASSERT_TRUE(group_call_source.find("boolis_live_story_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolis_active_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("int32unrestrict_boost_count_=0;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolis_all_history_available_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolcan_have_sponsored_messages_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolhas_automatic_translation_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolhas_hidden_participants_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("boolhas_aggressive_anti_spam_enabled_=false;") != td::string::npos);
    ASSERT_TRUE(chat_source.find("PublicDialogTypetype_=PublicDialogType::ForPersonalDialog;") != td::string::npos);

    // V501: double file_log.init() pattern must not reappear.
    ASSERT_EQ(
        td::string::npos,
        cli_source.find("file_log.init(file_name.str()).is_ok()&&file_log.init(file_name.str(),1000<<20).is_ok()"));
    ASSERT_TRUE(cli_source.find("file_log.init(file_name.str(),1000<<20).is_ok()") != td::string::npos);

    checksum += static_cast<td::uint32>(forum_source.size() ^ group_call_source.size() ^ inline_source.size() ^
                                        chat_source.size() ^ cli_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v730_core_runtime_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1500;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
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

    ASSERT_EQ(td::string::npos, sequence_source.find("Statestate_;"));
    ASSERT_EQ(td::string::npos, reaction_source.find("int64star_count_;"));
    ASSERT_EQ(td::string::npos, business_connection_source.find(
                                    "classGetBusinessStarTransferPaymentFormQueryfinal:publicTd::ResultHandler{"
                                    "Promise<Unit>promise_;BusinessConnectionIdbusiness_connection_id_;"
                                    "int64star_count_;"));
    ASSERT_EQ(td::string::npos,
              notification_source.find("classGetScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;NotificationSettingsScopescope_;"));
    ASSERT_EQ(td::string::npos,
              notification_source.find("classUpdateScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;NotificationSettingsScopescope_;"));
    ASSERT_EQ(td::string::npos, translation_source.find("boolskip_bot_commands_;"));
    ASSERT_EQ(td::string::npos, translation_source.find("int32max_media_timestamp_;"));

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

    checksum +=
        static_cast<td::uint32>(sequence_source.size() ^ reaction_source.size() ^ business_connection_source.size() ^
                                notification_source.size() ^ translation_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v730_request_handlers_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto background_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BackgroundManager.cpp"));
    const auto business_connection_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp"));
    const auto inline_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp"));
    const auto ts_file_log_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/TsFileLog.cpp"));

    ASSERT_EQ(td::string::npos, background_source.find("boolfor_dark_theme_;"));
    ASSERT_EQ(td::string::npos,
              business_connection_source.find("Promise<td_api::object_ptr<td_api::upgradedGift>>promise_;"
                                              "DialogIdowner_dialog_id_;stringname_;int64star_count_;"));
    ASSERT_EQ(td::string::npos, inline_source.find("classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
                                                   "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
                                                   "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_;"));
    ASSERT_EQ(td::string::npos, ts_file_log_source.find("std::atomic<bool>is_inited{false};size_tid;"));

    ASSERT_TRUE(background_source.find("boolfor_dark_theme_=false;") != td::string::npos);
    ASSERT_TRUE(business_connection_source.find("int64star_count_=0;") != td::string::npos);
    ASSERT_TRUE(inline_source.find("classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
                                   "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
                                   "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_=0;") != td::string::npos);
    ASSERT_TRUE(ts_file_log_source.find("std::atomic<bool>is_inited{false};size_tid{0};") != td::string::npos);

    checksum += static_cast<td::uint32>(background_source.size() ^ business_connection_source.size() ^
                                        inline_source.size() ^ ts_file_log_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v730_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto net_stats_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));
    const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.h"));
    const auto buffer_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/buffer.h"));

    ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsDatalast_sync_stats;"));
    ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfocommon_net_stats_;"));
    ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfomedia_net_stats_;"));
    ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfocall_net_stats_;"));
    ASSERT_EQ(td::string::npos,
              net_stats_source.find("std::array<TypeStats,5/*NetStatsManager::net_type_size()*/>stats_by_type;"));

    ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_;"));
    ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<1000>updates_duplicate_checker_;"));
    ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<100>updates_duplicate_rechecker_;"));

    ASSERT_EQ(td::string::npos, buffer_source.find("size_tdata_size_;"));

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

    checksum += static_cast<td::uint32>(net_stats_source.size() ^ auth_data_source.size() ^ buffer_source.size() ^
                                        static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v730_reply_markup_and_sequence_dispatcher_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto reply_markup_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
    const auto sequence_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));

    ASSERT_EQ(td::string::npos, reply_markup_source.find("KeyboardButtonStylestyle;"));
    ASSERT_EQ(td::string::npos, reply_markup_source.find("UserIduser_id;"));
    ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryRefnet_query_ref_;"));
    ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryPtrquery_;"));
    ASSERT_EQ(td::string::npos, sequence_source.find("ActorShared<NetQueryCallback>callback_;"));

    ASSERT_TRUE(reply_markup_source.find("KeyboardButtonStylestyle{};") != td::string::npos);
    ASSERT_TRUE(reply_markup_source.find("UserIduser_id{};") != td::string::npos);
    ASSERT_TRUE(sequence_source.find("NetQueryRefnet_query_ref_{};") != td::string::npos);
    ASSERT_TRUE(sequence_source.find("NetQueryPtrquery_{};") != td::string::npos);
    ASSERT_TRUE(sequence_source.find("ActorShared<NetQueryCallback>callback_{};") != td::string::npos);

    checksum += static_cast<td::uint32>(reply_markup_source.size() ^ sequence_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v730_message_handlers_message_handler_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto message_query_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageQueryManager.cpp"));
    const auto messages_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

    ASSERT_EQ(td::string::npos,
              message_query_source.find("classSearchMessagesGlobalQueryfinal:publicTd::ResultHandler{"
                                        "Promise<td_api::object_ptr<td_api::foundMessages>>promise_;"
                                        "stringquery_;int32offset_date_{0};DialogIdoffset_dialog_id_;"
                                        "MessageIdoffset_message_id_;int32limit_{0};MessageSearchFilterfilter_;"));
    ASSERT_EQ(td::string::npos,
              message_query_source.find("classGetMessagePositionQueryfinal:publicTd::ResultHandler{"
                                        "Promise<int32>promise_;DialogIddialog_id_;MessageIdmessage_id_;"
                                        "MessageSearchFilterfilter_;MessageTopicmessage_topic_;"));

    ASSERT_EQ(td::string::npos,
              messages_source.find("classGetSearchResultPositionsQueryfinal:publicTd::ResultHandler{"
                                   "Promise<td_api::object_ptr<td_api::messagePositions>>promise_;DialogIddialog_id_;"
                                   "SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_;"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("classGetSearchCountersQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                   "DialogIddialog_id_;MessageTopicmessage_topic_;MessageSearchFilterfilter_;"));
    ASSERT_EQ(
        td::string::npos,
        messages_source.find("classGetAllScheduledMessagesQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                             "DialogIddialog_id_;uint32generation_;"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("classEditMessageQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                   "DialogIddialog_id_;MessageIdmessage_id_;boolis_media_;"));
    ASSERT_EQ(
        td::string::npos,
        messages_source.find("classSendScreenshotNotificationQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                             "int64random_id_;DialogIddialog_id_;"));

    ASSERT_TRUE(message_query_source.find("MessageSearchFilterfilter_{};") != td::string::npos);
    ASSERT_TRUE(messages_source.find("SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_{};") !=
                td::string::npos);
    ASSERT_TRUE(messages_source.find("MessageTopicmessage_topic_;MessageSearchFilterfilter_{};") != td::string::npos);
    ASSERT_TRUE(messages_source.find("DialogIddialog_id_;uint32generation_{0};") != td::string::npos);
    ASSERT_TRUE(messages_source.find("MessageIdmessage_id_;boolis_media_=false;") != td::string::npos);
    ASSERT_TRUE(messages_source.find("Promise<Unit>promise_;int64random_id_=0;DialogIddialog_id_;") !=
                td::string::npos);

    checksum += static_cast<td::uint32>(message_query_source.size() ^ messages_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

TEST(SecurityCoreTrueIssuesStress, v547_v730_session_ctor_invariants_hold_over_repeated_reads) {
  constexpr int kIterations = 1200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto session_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
    const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.cpp"));
    const auto net_stats_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));

    ASSERT_EQ(td::string::npos, session_source.find("input.shutdown_requested=close_flag_||logging_out_flag_;"));
    ASSERT_TRUE(session_source.find("input.shutdown_requested=logging_out_flag_;") != td::string::npos);

    ASSERT_EQ(td::string::npos, auth_data_source.find("AuthData::AuthData(){"));
    ASSERT_TRUE(auth_data_source.find("AuthData::AuthData():duplicate_checker_{},updates_duplicate_checker_{},"
                                      "updates_duplicate_rechecker_{}{") != td::string::npos);

    ASSERT_EQ(td::string::npos,
              net_stats_source.find("explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)){"));
    ASSERT_TRUE(net_stats_source.find("explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)),"
                                      "common_net_stats_{},media_net_stats_{},call_net_stats_{}{") != td::string::npos);

    checksum += static_cast<td::uint32>(session_source.size() ^ auth_data_source.size() ^ net_stats_source.size() ^ i);
  }

  ASSERT_TRUE(checksum != 0);
}
