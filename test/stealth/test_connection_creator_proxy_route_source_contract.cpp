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

TEST(ConnectionCreatorProxyRouteSourceContract, RawIpRequestUsesEffectiveTransportTypeEverywhere) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto raw_ip_request = extract_source_region(source, "void ConnectionCreator::request_raw_connection_by_ip(",
                                              "Result<mtproto::TransportType> ConnectionCreator::get_transport_type(");
  auto normalized = normalize_for_contract(raw_ip_request);

  ASSERT_TRUE(normalized.find("resolve_raw_ip_connection_route(proxy,proxy_ip_address_,ip_address)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("resolve_raw_ip_transport_type(proxy,transport_type)") != td::string::npos);
  ASSERT_TRUE(normalized.find("SocketFd::open(route.socket_ip_address)") != td::string::npos);
  ASSERT_TRUE(normalized.find("RawConnection::create(ip_address,std::move(connection_data.buffered_socket_fd),"
                              "effective_transport_type,nullptr)") != td::string::npos);
  ASSERT_TRUE(normalized.find("prepare_connection(route.socket_ip_address,std::move(socket_fd),proxy,"
                              "route.mtproto_ip_address,effective_transport_type,\"Raw\"") != td::string::npos);

  ASSERT_TRUE(normalized.find("SocketFd::open(ip_address)") == td::string::npos);
  ASSERT_TRUE(normalized.find("route.mtproto_ip_address,transport_type,\"Raw\"") == td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "RawConnection::create(ip_address,std::move(connection_data.buffered_socket_fd),transport_type,") ==
              td::string::npos);
}

TEST(ConnectionCreatorProxyRouteSourceContract, PingProxyResolvesEffectiveProxyBeforeDirectBranch) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto ping_proxy = extract_source_region(source, "void ConnectionCreator::ping_proxy(",
                                          "void ConnectionCreator::ping_proxy_resolved(");

  auto resolve_pos = ping_proxy.find("resolve_effective_ping_proxy(active_proxy, requested_proxy.get())");
  auto direct_branch_pos = ping_proxy.find("if (!proxy.use_proxy())");

  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(direct_branch_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos < direct_branch_pos);
}

TEST(ConnectionCreatorProxyRouteSourceContract, PingProxyBufferedSocketLogsDoNotUseMovedFromTransportType) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto ping_proxy_buffered = extract_source_region(source, "void ConnectionCreator::ping_proxy_buffered_socket_fd(",
                                                   "void ConnectionCreator::set_active_proxy_id(");
  auto normalized = normalize_for_contract(ping_proxy_buffered);

  ASSERT_TRUE(normalized.find("autotransport_type_for_log=transport_type;") != td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          "RawConnection::create(ip_address,std::move(buffered_socket_fd),std::move(transport_type),nullptr)") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("transport_type_for_log") != td::string::npos);
  ASSERT_TRUE(normalized.find("raw_ip_transport_name(transport_type)") == td::string::npos);
  ASSERT_TRUE(normalized.find("transport_type.dc_id") == td::string::npos);
  ASSERT_TRUE(normalized.find("transport_type.secret.emulate_tls()") == td::string::npos);
}

TEST(ConnectionCreatorProxyRouteSourceContract, ProxyCheckerStampsOneTransportAndReusesItAcrossHandshakePath) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ProxyChecker.cpp");
  auto header = td::mtproto::test::read_repo_text_file("td/telegram/net/ProxyChecker.h");
  auto source_normalized = normalize_for_contract(source);
  auto header_normalized = normalize_for_contract(header);

  ASSERT_TRUE(header_normalized.find("mtproto::TransportTypetransport_type_;") != td::string::npos);
  ASSERT_TRUE(header_normalized.find("get_transport()const") == td::string::npos);

  ASSERT_TRUE(source_normalized.find("request->transport_type_=mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp,request->dc_id_,request->proxy_.secret()};") !=
              td::string::npos);
  ASSERT_TRUE(source_normalized.find("ConnectionCreator::stamp_runtime_profile_selection(request->transport_type_);") !=
              td::string::npos);
  ASSERT_TRUE(source_normalized.find("request->transport_type_,\"Test\"") != td::string::npos);
  ASSERT_TRUE(source_normalized.find("request->transport_type_,nullptr)") != td::string::npos);
  ASSERT_TRUE(source_normalized.find("request->get_transport()") == td::string::npos);
}

}  // namespace
