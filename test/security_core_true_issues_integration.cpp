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
    if (auto b = static_cast<unsigned char>(c); b == ' ' || b == '\t' || b == '\n' || b == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

TEST(SecurityCoreTrueIssuesIntegration, v730_cross_file_initialization_contract_holds) {
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.h"));
  const auto buffer_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/buffer.h"));

  ASSERT_TRUE(net_stats_source.find("NetStatsDatalast_sync_stats{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocommon_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfomedia_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocall_net_stats_{};") != td::string::npos);

  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_{};") != td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>updates_duplicate_checker_{};") !=
              td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<100>updates_duplicate_rechecker_{};") !=
              td::string::npos);

  ASSERT_TRUE(buffer_source.find("size_tdata_size_{0};") != td::string::npos);

  ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsDatalast_sync_stats;"));
  ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_;"));
  ASSERT_EQ(td::string::npos, buffer_source.find("size_tdata_size_;"));
}

TEST(SecurityCoreTrueIssuesIntegration,
     v730_reply_markup_and_sequence_dispatcher_cross_file_initialization_contract_holds) {
  const auto reply_markup_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
  const auto sequence_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));

  ASSERT_TRUE(reply_markup_source.find("KeyboardButtonStylestyle{};") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("UserIduser_id{};") != td::string::npos);

  ASSERT_TRUE(sequence_source.find("NetQueryRefnet_query_ref_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("NetQueryPtrquery_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("ActorShared<NetQueryCallback>callback_{};") != td::string::npos);

  ASSERT_EQ(td::string::npos, reply_markup_source.find("KeyboardButtonStylestyle;"));
  ASSERT_EQ(td::string::npos, reply_markup_source.find("UserIduser_id;"));
  ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryRefnet_query_ref_;"));
  ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryPtrquery_;"));
  ASSERT_EQ(td::string::npos, sequence_source.find("ActorShared<NetQueryCallback>callback_;"));
}

TEST(SecurityCoreTrueIssuesIntegration, v730_message_handlers_message_query_cross_file_initialization_contract_holds) {
  const auto message_query_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageQueryManager.cpp"));
  const auto messages_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

  ASSERT_TRUE(message_query_source.find("MessageSearchFilterfilter_{};") != td::string::npos);
  ASSERT_EQ(td::string::npos, message_query_source.find("MessageSearchFilterfilter_;"));

  ASSERT_TRUE(messages_source.find("SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_{};") !=
              td::string::npos);
  ASSERT_TRUE(messages_source.find("MessageTopicmessage_topic_;MessageSearchFilterfilter_{};") != td::string::npos);
  ASSERT_TRUE(messages_source.find("DialogIddialog_id_;uint32generation_{0};") != td::string::npos);
  ASSERT_TRUE(messages_source.find("MessageIdmessage_id_;boolis_media_=false;") != td::string::npos);
  ASSERT_TRUE(messages_source.find("Promise<Unit>promise_;int64random_id_=0;DialogIddialog_id_;") != td::string::npos);

  ASSERT_EQ(td::string::npos,
            messages_source.find("SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_;"));
  ASSERT_EQ(td::string::npos, messages_source.find("MessageTopicmessage_topic_;MessageSearchFilterfilter_;"));
  ASSERT_EQ(td::string::npos, messages_source.find("DialogIddialog_id_;uint32generation_;"));
  ASSERT_EQ(td::string::npos, messages_source.find("MessageIdmessage_id_;boolis_media_;"));
  ASSERT_EQ(td::string::npos, messages_source.find("Promise<Unit>promise_;int64random_id_;DialogIddialog_id_;"));
}

TEST(SecurityCoreTrueIssuesIntegration, v547_v730_session_ctor_init_contract_holds) {
  const auto session_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.cpp"));
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));

  ASSERT_TRUE(session_source.find("if(close_flag_||primary->state_!=ConnectionInfo::State::Ready){return;}") !=
              td::string::npos);
  ASSERT_TRUE(session_source.find("input.shutdown_requested=logging_out_flag_;") != td::string::npos);

  ASSERT_TRUE(auth_data_source.find("AuthData::AuthData():duplicate_checker_{},updates_duplicate_checker_{},"
                                    "updates_duplicate_rechecker_{}{") != td::string::npos);
  ASSERT_TRUE(auth_data_source.find("server_salt_.salt=Random::secure_int64();") != td::string::npos);

  ASSERT_TRUE(net_stats_source.find("explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)),"
                                    "common_net_stats_{},media_net_stats_{},call_net_stats_{}{") != td::string::npos);
}
