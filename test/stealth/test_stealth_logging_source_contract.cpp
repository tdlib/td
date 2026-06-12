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
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_source_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin);
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

TEST(StealthLoggingSourceContract, RawIpRequestLogsProxyRouteAndEffectiveTransportContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(source, "void ConnectionCreator::request_raw_connection_by_ip(",
                                      "Result<mtproto::TransportType> ConnectionCreator::get_transport_type(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("\"Resolvedraw-IProute\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"proxy_mode\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"socket_ip\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"target_ip\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"tunneled_mtproto_ip\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"transport\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"tls_emulation\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("get_raw_secret()") == td::string::npos);
}

TEST(StealthLoggingSourceContract, RawIpRouteValidationFailuresLogStructuredContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(
      source, "Result<ConnectionCreator::RawIpConnectionRoute> ConnectionCreator::resolve_raw_ip_connection_route(",
      "Result<mtproto::TransportType> ConnectionCreator::resolve_raw_ip_transport_type(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Raw-IProutevalidationfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"proxy_mode\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"target_ip_valid\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"proxy_ip_valid\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"reason\"") != td::string::npos);
}

TEST(StealthLoggingSourceContract, TlsInitLogsRouteEchDecisionAndResponseFailureStage) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/TlsInit.cpp");
  auto send_hello_region =
      extract_source_region(source, "void TlsInit::send_hello()", "Status TlsInit::wait_hello_response()");
  auto wait_region =
      extract_source_region(source, "Status TlsInit::wait_hello_response()", "Status TlsInit::loop_impl()");

  auto normalized_send = normalize_for_contract(send_hello_region);
  auto normalized_wait = normalize_for_contract(wait_region);

  ASSERT_TRUE(normalized_send.find("\"TlsInithelloprepared\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("\"TlsInithellogenerationfailed\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"route_known\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"route_ru\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"ech_mode\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"ech_enabled\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"profile\"") != td::string::npos);
  ASSERT_TRUE(normalized_send.find("hello.size()<kTlsHelloResponseRandomOffset+kTlsHelloResponseRandomSize") !=
              td::string::npos);
  ASSERT_TRUE(normalized_send.find("on_error(make_proxy_setup_error(ProxySetupErrorCode::TlsHelloMalformedResponse") !=
              td::string::npos);
  ASSERT_TRUE(normalized_send.find("tag(\"hello_bytes\"") != td::string::npos);

  ASSERT_TRUE(normalized_wait.find("\"TlsInithelloresponserejected\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"recorded_ech_failure\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"buffered_bytes\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"parsed_bytes\"") != td::string::npos);
  ASSERT_TRUE(normalized_wait.find("tag(\"parse_complete\"") != td::string::npos);
}

TEST(StealthLoggingSourceContract, RawConnectionHandshakeLogsAreStructuredAndNoHexDumpRemains) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/RawConnection.cpp");
  ASSERT_TRUE(source.find("as_hex_dump") == td::string::npos);

  auto normalized_source = normalize_for_contract(source);
  ASSERT_TRUE(normalized_source.find("voidlog_handshake_packet_metadata(") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("\"Sendhandshakepacket\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"handshake_packet_bytes\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"hint\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"transport\"") != td::string::npos);

  auto send_no_crypto_region =
      extract_source_region(source, "void send_no_crypto(const Storer &storer, stealth::TrafficHint hint) final {",
                            "PollableFdInfo &get_poll_info() final {");
  auto normalized = normalize_for_contract(send_no_crypto_region);

  ASSERT_TRUE(normalized.find("log_handshake_packet_metadata(transport_->get_type(),hint,packet.size())") !=
              td::string::npos);
}

TEST(StealthLoggingSourceContract, RawConnectionFlushFailureLogsStructuredMtprotoClassificationContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/RawConnection.cpp");
  auto normalized_source = normalize_for_contract(source);

  ASSERT_TRUE(normalized_source.find("voidlog_raw_connection_flush_failure(") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("\"Rawconnectionflushfailed\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"transport\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"dc_id\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"tls_emulation\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"mtproto_error\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"classification\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"action_hint\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"mtproto_error_notified\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"had_previous_error\"") != td::string::npos);
}

TEST(StealthLoggingSourceContract, RawConnectionQuickAckAnomaliesUseStructuredContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/RawConnection.cpp");
  auto normalized_source = normalize_for_contract(source);

  ASSERT_TRUE(normalized_source.find("Quickacktokencollision") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("Receiveinvalidquick_ack") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("Receiveunknownquick_ack") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"quick_ack\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"transport\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("tag(\"dc_id\"") != td::string::npos);
  ASSERT_TRUE(normalized_source.find("LOG(ERROR)<<\"Quickack\"<<packet_info.message_ack<<\"collision\"") ==
              td::string::npos);
}

TEST(StealthLoggingSourceContract, ConnectionCreatorFailureLogContainsStructuredStatusAndRemediationHint) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(source, "void ConnectionCreator::client_add_connection(",
                                      "void ConnectionCreator::client_wakeup(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Classifiedconnectionfailure") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"action_hint\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("summarize_connection_failure_for_log") != td::string::npos);
}

TEST(StealthLoggingSourceContract, NetQueryErrorLogsUseStructuredPublicStatusContext) {
  auto net_query_source = td::mtproto::test::read_repo_text_file("td/telegram/net/NetQuery.cpp");
  auto net_query_region =
      extract_source_region(net_query_source, "void NetQuery::set_error_impl(Status status, string source) {",
                            "StringBuilder &operator<<(StringBuilder &string_builder, const NetQuery &net_query) {");
  auto normalized_net_query = normalize_for_contract(net_query_region);

  ASSERT_TRUE(normalized_net_query.find("Receiveerror") != td::string::npos);
  ASSERT_TRUE(normalized_net_query.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_net_query.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_net_query.find("\"\"<<status") == td::string::npos);

  auto verifier_source = td::mtproto::test::read_repo_text_file("td/telegram/net/NetQueryVerifier.cpp");
  auto verifier_region = extract_source_region(
      verifier_source, "void NetQueryVerifier::verify(NetQueryPtr query, string nonce) {",
      "void NetQueryVerifier::check_recaptcha(NetQueryPtr query, string action, string recaptcha_key_id) {");
  auto normalized_verifier = normalize_for_contract(verifier_region);

  ASSERT_TRUE(normalized_verifier.find("Receiveinvalidverificationnonce") != td::string::npos);
  ASSERT_TRUE(normalized_verifier.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_verifier.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_verifier.find("Receive<<status") == td::string::npos);

  auto delayer_source = td::mtproto::test::read_repo_text_file("td/telegram/net/NetQueryDelayer.cpp");
  auto delayer_region = extract_source_region(delayer_source, "void NetQueryDelayer::delay(NetQueryPtr query) {",
                                              "void NetQueryDelayer::wakeup() {");
  auto normalized_delayer = normalize_for_contract(delayer_region);

  ASSERT_TRUE(normalized_delayer.find("tag(\"status_code\",error.code())") != td::string::npos);
  ASSERT_TRUE(normalized_delayer.find("tag(\"status_message\",error.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_delayer.find("becauseof<<error") == td::string::npos);

  auto transparent_proxy_source = td::mtproto::test::read_repo_text_file("tdnet/td/net/TransparentProxy.cpp");
  auto transparent_proxy_region =
      extract_source_region(transparent_proxy_source, "void TransparentProxy::on_error(Status status) {",
                            "void TransparentProxy::tear_down() {");
  auto normalized_transparent_proxy = normalize_for_contract(transparent_proxy_region);

  ASSERT_TRUE(normalized_transparent_proxy.find("Receiveproxysetuperror") != td::string::npos);
  ASSERT_TRUE(normalized_transparent_proxy.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_transparent_proxy.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_transparent_proxy.find("Receive<<status") == td::string::npos);

  auto http_connection_source = td::mtproto::test::read_repo_text_file("tdnet/td/net/HttpConnectionBase.cpp");
  auto http_connection_region =
      extract_source_region(http_connection_source, "void HttpConnectionBase::write_error(Status error) {",
                            "void HttpConnectionBase::timeout_expired() {");
  auto normalized_http_connection = normalize_for_contract(http_connection_region);

  ASSERT_TRUE(normalized_http_connection.find("CloseHTTPconnection") != td::string::npos);
  ASSERT_TRUE(normalized_http_connection.find("tag(\"status_code\",error.code())") != td::string::npos);
  ASSERT_TRUE(normalized_http_connection.find("tag(\"status_message\",error.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_http_connection.find("CloseHTTPconnection:<<error") == td::string::npos);
}

TEST(StealthLoggingSourceContract, ConnectionCreatorPingMainDcFailureLogsAreStructuredAndContextual) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(source, "void ConnectionCreator::ping_proxy(",
                                      "void ConnectionCreator::ping_proxy_resolved(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("PingmainDCtransportresolutionfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("PingmainDCsocketopenfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"dc_id\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"target_ip\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG(ERROR)<<r_transport_type.error()") == td::string::npos);
}

TEST(StealthLoggingSourceContract, SessionConnectionProtocolRejectionLogsAreStructured) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");
  auto bad_msg_region = extract_source_region(
      source,
      "Status SessionConnection::on_packet(const MsgInfo &info,\n"
      "                                    const mtproto_api::bad_msg_notification &bad_msg_notification)",
      "Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::bad_server_salt");
  auto raw_packet_region = extract_source_region(source, "Status SessionConnection::on_raw_packet(",
                                                 "Status SessionConnection::on_quick_ack(uint64 quick_ack_token)");
  auto slice_packet_region = extract_source_region(source, "Status SessionConnection::on_slice_packet(",
                                                   "Status SessionConnection::parse_packet(TlParser &parser)");

  auto normalized_bad_msg = normalize_for_contract(bad_msg_region);
  auto normalized_raw_packet = normalize_for_contract(raw_packet_region);
  auto normalized_slice_packet = normalize_for_contract(slice_packet_region);

  ASSERT_TRUE(normalized_bad_msg.find("BadMsgNotificationprotocolviolation") != td::string::npos);
  ASSERT_TRUE(normalized_bad_msg.find("tag(\"error_code\"") != td::string::npos);
  ASSERT_TRUE(normalized_bad_msg.find("tag(\"bad_msg_id\"") != td::string::npos);
  ASSERT_TRUE(normalized_bad_msg.find("tag(\"bad_msg_seqno\"") != td::string::npos);
  ASSERT_TRUE(normalized_bad_msg.find("tag(\"action\"") != td::string::npos);

  ASSERT_TRUE(normalized_raw_packet.find("Receivetoooldpacket") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("tag(\"session_id\"") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("Packetisignored") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("tag(\"message_id\",packet_info.message_id)") != td::string::npos);
  ASSERT_TRUE(normalized_raw_packet.find("Packetisignored:<<status") == td::string::npos);

  ASSERT_TRUE(normalized_slice_packet.find("Receiveveryold") != td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("tag(\"recheck_status_code\",recheck_status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("tag(\"recheck_status_message\",recheck_status.public_message())") !=
              td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("Skip") != td::string::npos);
  ASSERT_TRUE(normalized_slice_packet.find("<<get_update_description()<<\":\"<<status") == td::string::npos);
}

TEST(StealthLoggingSourceContract, SessionConnectionFlushCloseLogUsesStructuredPublicStatus) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");
  auto region =
      extract_source_region(source, "double SessionConnection::flush(SessionConnection::Callback *callback) {",
                            "void SessionConnection::force_close(SessionConnection::Callback *callback) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Closesessionbecauseoffailure") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized.find("Closesessionbecauseof<<status") == td::string::npos);
}

TEST(StealthLoggingSourceContract, SessionDisconnectPropagationUsesPublicStatusMessage) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp");
  auto region = extract_source_region(source, "void Session::on_closed(", "void Session::on_new_session_created(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Sessionfailed:") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized.find("status.public_message()") != td::string::npos);
  ASSERT_TRUE(normalized.find("status.message()") == td::string::npos);
}

TEST(StealthLoggingSourceContract, SessionFailedLogUsesStructuredPublicStatusContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp");
  auto region = extract_source_region(source, "void Session::on_session_failed(Status status) {",
                                      "void Session::on_container_sent(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Sessionfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized.find("<<status") == td::string::npos);
}

TEST(StealthLoggingSourceContract, SessionFailureEmittersUseStructuredPublicStatusContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp");
  auto bind_region = extract_source_region(source, "void Session::on_bind_result(NetQueryPtr query) {",
                                           "void Session::on_check_key_result(NetQueryPtr query) {");
  auto check_region = extract_source_region(source, "void Session::on_check_key_result(NetQueryPtr query) {",
                                            "void Session::on_result(NetQueryPtr query) {");
  auto send_fail_region =
      extract_source_region(source, "void Session::on_message_failed(mtproto::MessageId message_id, Status status) {",
                            "Status Session::on_destroy_auth_key() {");

  auto normalized_bind = normalize_for_contract(bind_region);
  auto normalized_check = normalize_for_contract(check_region);
  auto normalized_send_fail = normalize_for_contract(send_fail_region);

  ASSERT_TRUE(normalized_bind.find("BindKeyfailed") != td::string::npos);
  ASSERT_TRUE(normalized_bind.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_bind.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_bind.find("BindKeyfailed:") == td::string::npos);

  ASSERT_TRUE(normalized_check.find("Checkmainkeyfailed") != td::string::npos);
  ASSERT_TRUE(normalized_check.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_check.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_check.find("Checkmainkeyfailed:") == td::string::npos);

  ASSERT_TRUE(normalized_send_fail.find("Failedtosend") != td::string::npos);
  ASSERT_TRUE(normalized_send_fail.find("tag(\"message_id\",message_id)") != td::string::npos);
  ASSERT_TRUE(normalized_send_fail.find("tag(\"status_code\",status.code())") != td::string::npos);
  ASSERT_TRUE(normalized_send_fail.find("tag(\"status_message\",status.public_message())") != td::string::npos);
  ASSERT_TRUE(normalized_send_fail.find("<<message_id<<\":\"<<status") == td::string::npos);
}

TEST(StealthLoggingSourceContract, ConnectionCreatorClientLoopSocketDiagnosticsAreStructured) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(source, "void ConnectionCreator::client_loop(ClientInfo &client) {",
                                      "void ConnectionCreator::client_create_raw_connection(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Clientloopsocketopenfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("Clientlooplocalendpointintrospectionfailed") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"dc_id\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"connection_context\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG(WARNING)<<extra.debug_str<<\":\"<<r_socket_fd.error()") == td::string::npos);
  ASSERT_TRUE(normalized.find("LOG(ERROR)<<debug_ip_status") == td::string::npos);
}

TEST(StealthLoggingSourceContract, ConnectionCreatorProxySocketOpenErrorsUseSanitizedRetryStatusTags) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto region = extract_source_region(
      source, "Result<ConnectionCreator::ProxySocketOpenResult> ConnectionCreator::open_proxy_socket(",
      "Status ConnectionCreator::verify_connection_peer(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("primary_error.public_message()") != td::string::npos);
  ASSERT_TRUE(normalized.find("fallback_error.public_message()") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\",primary_error_for_retry.code())") != td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          "tag(\"status_message\",sanitize_connection_failure_status_message_for_log(primary_error_for_retry))") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\",primary_error_for_retry.public_message())") == td::string::npos);
  ASSERT_TRUE(normalized.find("primary_error.message()") == td::string::npos);
  ASSERT_TRUE(normalized.find("fallback_error.message()") == td::string::npos);
  ASSERT_TRUE(normalized.find("<<r_socket.error()") == td::string::npos);
}

TEST(StealthLoggingSourceContract, TlsReaderHeaderRejectionLogsStructuredContext) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/TlsReaderByteFlow.cpp");
  auto region = extract_source_region(source, "bool TlsReaderByteFlow::loop() {", "}  // namespace mtproto");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("TLSemulationreaderrejectedunexpectedrecordheader") != td::string::npos);
  ASSERT_TRUE(normalized.find("[content_type=") != td::string::npos);
  ASSERT_TRUE(normalized.find("[version_major=") != td::string::npos);
  ASSERT_TRUE(normalized.find("[version_minor=") != td::string::npos);
  ASSERT_TRUE(normalized.find("[declared_payload_size=") != td::string::npos);
}

TEST(StealthLoggingSourceContract, StreamTransportActivationLogsAreStructuredAndDoNotDumpRawErrors) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/IStreamTransport.cpp");
  auto region = extract_source_region(source, "unique_ptr<IStreamTransport> create_transport(TransportType type) {",
                                      "StreamTransportFactoryForTests set_transport_factory_for_tests(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("Stealthshapingunavailable;refusingemulate_tlstransport(fail-closed)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("sanitize_stealth_activation_status_message(error,secret_copy,") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"reason\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"dc_id\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"tls_emulation\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_code\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"status_message\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("<<error") == td::string::npos);
}

}  // namespace
