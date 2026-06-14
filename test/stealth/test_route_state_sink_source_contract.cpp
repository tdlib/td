// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace route_state_sink_source_contract {

td::string extract_source_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

TEST(RouteStateSinkSourceContract, RouteCorrectionGuardsReturnBeforeSaltMutation) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");
  auto region = extract_source_region(source,
                                      "Status SessionConnection::on_packet(const MsgInfo &info, const "
                                      "mtproto_api::bad_server_salt &bad_server_salt) {",
                                      "Status SessionConnection::on_packet(const MsgInfo &info, const "
                                      "mtproto_api::msgs_ack &msgs_ack) {");

  auto tear_pos = region.find("if (correction_decision == RouteCorrectionSequencer::Decision::TearDown) {");
  auto rate_pos = region.find("if (correction_decision == RouteCorrectionSequencer::Decision::RateLimit) {");
  auto reject_pos = region.find("if (correction_decision == RouteCorrectionSequencer::Decision::Reject) {");
  auto set_salt_pos = region.find("auth_data_->set_server_salt(");
  auto callback_pos = region.find("callback_->on_server_salt_updated();");
  auto resend_pos = region.find("on_message_failed(bad_info.message_id, Status::Error(\"Bad server salt\"));");

  ASSERT_TRUE(tear_pos != td::string::npos);
  ASSERT_TRUE(rate_pos != td::string::npos);
  ASSERT_TRUE(reject_pos != td::string::npos);
  ASSERT_TRUE(set_salt_pos != td::string::npos);
  ASSERT_TRUE(callback_pos != td::string::npos);
  ASSERT_TRUE(resend_pos != td::string::npos);

  ASSERT_TRUE(tear_pos < set_salt_pos);
  ASSERT_TRUE(rate_pos < set_salt_pos);
  ASSERT_TRUE(reject_pos < set_salt_pos);
  ASSERT_TRUE(set_salt_pos < callback_pos);
  ASSERT_TRUE(callback_pos < resend_pos);

  auto tear_block = region.substr(tear_pos, set_salt_pos - tear_pos);
  auto rate_block = region.substr(rate_pos, set_salt_pos - rate_pos);
  auto reject_block = region.substr(reject_pos, set_salt_pos - reject_pos);

  ASSERT_TRUE(tear_block.find("note_route_correction_chain_reset") != td::string::npos);
  ASSERT_TRUE(tear_block.find("return Status::Error(\"Route correction chain limit exceeded\");") != td::string::npos);
  ASSERT_TRUE(rate_block.find("note_route_correction_rate_gate") != td::string::npos);
  ASSERT_TRUE(rate_block.find("return Status::OK();") != td::string::npos);
  ASSERT_TRUE(reject_block.find("note_route_correction_unref") != td::string::npos);
  ASSERT_TRUE(reject_block.find("return Status::OK();") != td::string::npos);
}

TEST(RouteStateSinkSourceContract, FutureSaltRateGateReturnsBeforeFutureSaltMutation) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");
  auto region = extract_source_region(source,
                                      "Status SessionConnection::on_packet(const MsgInfo &info, const "
                                      "mtproto_api::future_salts &salts) {",
                                      "Status SessionConnection::on_msgs_state_info(const vector<int64> &msg_ids, "
                                      "Slice info) {");

  auto rate_gate_pos = region.find("if (swp_result.rate_limited) {");
  auto set_future_pos = region.find("auth_data_->set_future_salts(new_salts, now);");
  auto callback_pos = region.find("callback_->on_server_salt_updated();");

  ASSERT_TRUE(rate_gate_pos != td::string::npos);
  ASSERT_TRUE(set_future_pos != td::string::npos);
  ASSERT_TRUE(callback_pos != td::string::npos);
  ASSERT_TRUE(rate_gate_pos < set_future_pos);
  ASSERT_TRUE(set_future_pos < callback_pos);

  auto rate_gate_block = region.substr(rate_gate_pos, set_future_pos - rate_gate_pos);
  ASSERT_TRUE(rate_gate_block.find("note_route_salt_rate_gate") != td::string::npos);
  ASSERT_TRUE(rate_gate_block.find("return Status::OK();") != td::string::npos);
}

TEST(RouteStateSinkSourceContract, SessionInitReplayReturnsBeforeCallbackButRateGatedSessionStillNotifies) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");
  auto region = extract_source_region(source,
                                      "Status SessionConnection::on_packet(const MsgInfo &info, const "
                                      "mtproto_api::new_session_created &new_session_created) {",
                                      "Status SessionConnection::on_packet(const MsgInfo &info,\n"
                                      "                                    const mtproto_api::bad_msg_notification "
                                      "&bad_msg_notification) {");

  auto replay_pos = region.find("if (init_decision == SessionInitSequencer::Decision::ReplayWithoutSaltUpdate) {");
  auto remap_pos = region.find("if (it != service_queries_.end()) {");
  auto rate_gate_pos = region.find("if (init_decision == SessionInitSequencer::Decision::AcceptWithoutSaltUpdate) {");
  auto callback_pos =
      region.find("callback_->on_new_session_created(new_session_created.unique_id_, first_message_id);");

  ASSERT_TRUE(replay_pos != td::string::npos);
  ASSERT_TRUE(remap_pos != td::string::npos);
  ASSERT_TRUE(rate_gate_pos != td::string::npos);
  ASSERT_TRUE(callback_pos != td::string::npos);
  ASSERT_TRUE(replay_pos < rate_gate_pos);
  ASSERT_TRUE(rate_gate_pos < remap_pos);
  ASSERT_TRUE(remap_pos < callback_pos);

  auto replay_block = region.substr(replay_pos, remap_pos - replay_pos);
  ASSERT_TRUE(replay_block.find("note_session_init_replay") != td::string::npos);
  ASSERT_TRUE(replay_block.find("return Status::OK();") != td::string::npos);

  auto rate_gate_block = region.substr(rate_gate_pos, callback_pos - rate_gate_pos);
  ASSERT_TRUE(rate_gate_block.find("note_session_init_rate_gate") != td::string::npos);
}

}  // namespace route_state_sink_source_contract
