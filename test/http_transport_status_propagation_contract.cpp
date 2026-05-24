// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/Transport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

}  // namespace

TEST(HttpTransportStatusPropagationContract, StreamTransportInterfaceCarriesOptionalHttpStatusCodeOutParam) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/mtproto/IStreamTransport.h"));
  ASSERT_TRUE(source.find("virtualResult<size_t>read_next(BufferSlice*message,uint32*quick_ack)=0;") !=
              td::string::npos);
  ASSERT_TRUE(source.find("virtualResult<size_t>read_next(BufferSlice*message,uint32*quick_ack,int32*error_code){") !=
              td::string::npos);
  ASSERT_TRUE(source.find("if(error_code!=nullptr){*error_code=0;}") != td::string::npos);
  ASSERT_TRUE(source.find("returnread_next(message,quick_ack);") != td::string::npos);
}

TEST(HttpTransportStatusPropagationContract, HttpTransportReportsResponseStatusCodeThroughReadNext) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/mtproto/HttpTransport.cpp"));
  ASSERT_TRUE(source.find("Result<size_t>Transport::read_next(BufferSlice*message,uint32*quick_ack)") !=
              td::string::npos);
  ASSERT_TRUE(
      source.find("Result<size_t>Transport::read_next(BufferSlice*message,uint32*quick_ack,int32*error_code)") !=
      td::string::npos);
  ASSERT_TRUE(source.find("if(error_code!=nullptr){*error_code=http_query_.code_;}") != td::string::npos);
}

TEST(HttpTransportStatusPropagationContract, RawConnectionPropagatesHttpStatusIntoTransportReadParser) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/mtproto/RawConnection.cpp"));
  ASSERT_TRUE(source.find("int32error_code=0;") != td::string::npos);
  ASSERT_TRUE(source.find("transport_->read_next(&packet,&quick_ack,&error_code)") != td::string::npos);
  ASSERT_TRUE(source.find("Transport::read(packet.as_mutable_slice(),error_code,auth_key,&packet_info)") !=
              td::string::npos);
}

TEST(HttpTransportStatusPropagationContract, TransportReadMapsHttpFailureStatusToDeterministicErrorCode) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/mtproto/Transport.cpp"));
  ASSERT_TRUE(source.find("Result<Transport::ReadResult>Transport::read(MutableSlicemessage,int32error_code,"
                          "constAuthKey&auth_key,PacketInfo*packet_info)") != td::string::npos);
  ASSERT_TRUE(source.find("if(is_http_status_transport_error(error_code)){returnmake_http_status_transport_error_"
                          "status(error_code);}") != td::string::npos);
}

TEST(HttpTransportStatusPropagationContract, Http404DoesNotAliasMtprotoAuthKeyNotFoundErrorCodeAtRuntime) {
  td::mtproto::PacketInfo packet_info;
  auto read_result = td::mtproto::Transport::read(td::MutableSlice(), 404, td::mtproto::AuthKey(), &packet_info);
  ASSERT_TRUE(read_result.is_error());
  ASSERT_EQ(404, read_result.error().code());
}

TEST(HttpTransportStatusPropagationContract, HandshakeConnectionUsesMtprotoSpecificAuthKeyNotFoundGuard) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/mtproto/HandshakeConnection.h"));
  ASSERT_TRUE(source.find("is_mtproto_auth_key_not_found_status(status)") != td::string::npos);
  ASSERT_EQ(td::string::npos, source.find("status.code()==-404"));
}

TEST(HttpTransportStatusPropagationContract, SessionUsesMtprotoSpecificAuthKeyNotFoundGuard) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
  ASSERT_TRUE(source.find("is_mtproto_auth_key_not_found_status(status)") != td::string::npos);
  ASSERT_EQ(td::string::npos, source.find("status.is_error()&&status.code()==-404"));
  ASSERT_EQ(td::string::npos, source.find("status.is_ok()||status.code()!=-404"));
}

TEST(HttpTransportStatusPropagationContract, ConnectionCreatorUsesMtprotoSpecificAuthKeyNotFoundGuard) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp"));
  ASSERT_TRUE(source.find("is_mtproto_auth_key_not_found_status(r_raw_connection.error())") != td::string::npos);
  ASSERT_EQ(td::string::npos, source.find("r_raw_connection.error().code()==-404"));
}

TEST(HttpTransportStatusPropagationContract, DarwinHttpRejectsHttpStatusesAtOrAbove400EvenWithoutNSError) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("tdnet/td/net/DarwinHttp.mm"));
  ASSERT_TRUE(source.find("autostatus_code=[responseisKindOfClass:[NSHTTPURLResponseclass]]") != td::string::npos);
  ASSERT_TRUE(source.find("if(error==nil&&status_code<400)") != td::string::npos);
  ASSERT_TRUE(source.find("callback.set_error(status_code,\"HTTPrequestfailed\")") != td::string::npos);
}
