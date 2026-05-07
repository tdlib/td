// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// Source contract for tdjson request-ingress boundary hardening and explicit diagnostics.

#include "td/utils/common.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/tests.h"

#include "td/telegram/ClientJson.h"

#include "test/stealth/SourceContractFileReader.h"

#include <limits>

namespace {

td::string extract_source_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin);
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

td::string execute_and_get_error_message(td::Slice request) {
  auto *response = td::json_execute(request);
  CHECK(response != nullptr);

  td::string response_json = response;
  auto response_json_copy = response_json;
  auto r_json = td::json_decode(response_json_copy);
  CHECK(r_json.is_ok());

  auto response_value = r_json.move_as_ok();
  CHECK(response_value.type() == td::JsonValue::Type::Object);
  auto &response_object = response_value.get_object();

  auto r_type = response_object.get_required_string_field("@type");
  CHECK(r_type.is_ok());
  CHECK(r_type.ok() == "error");

  auto r_message = response_object.get_required_string_field("message");
  CHECK(r_message.is_ok());
  return r_message.move_as_ok();
}

TEST(ClientJsonBoundaryContract, IngressHardeningChecksAndExplicitMessagesArePinned) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ClientJson.cpp");
  auto to_request_region = extract_source_region(
      td::Slice(source), "static std::pair<td_api::object_ptr<td_api::Function>, string> to_request(Slice request) {",
      "static string from_response(const td_api::Object &object, const string &extra, int client_id) {");

  ASSERT_TRUE(to_request_region.find("ClientJson boundary: inbound request payload received") != td::string::npos);
  ASSERT_TRUE(to_request_region.find("find_first_disallowed_control_byte(request)") != td::string::npos);
  ASSERT_TRUE(to_request_region.find("Invalid request payload: unexpected control byte at offset") != td::string::npos);
  ASSERT_TRUE(to_request_region.find("Expected a JSON object request payload") != td::string::npos);
  ASSERT_TRUE(to_request_region.find("ClientJson boundary parse failure: json_decode failed") != td::string::npos);
  ASSERT_TRUE(to_request_region.find("ClientJson boundary parse failure: TDLib request conversion failed") !=
              td::string::npos);
}

TEST(ClientJsonBoundaryContract, BoundaryHexPreviewAndControlScanHelpersArePinned) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ClientJson.cpp");
  auto helper_region = extract_source_region(td::Slice(source), "constexpr std::size_t kClientJsonPreviewBytes = 32;",
                                             "static td_api::object_ptr<td_api::Function> get_return_error_function(");

  ASSERT_TRUE(helper_region.find("kClientJsonControlScanBytes = 64") != td::string::npos);
  ASSERT_TRUE(helper_region.find("get_request_hex_preview") != td::string::npos);
  ASSERT_TRUE(helper_region.find("preview_hex=") != td::string::npos);
  ASSERT_TRUE(helper_region.find("find_first_non_whitespace_byte") != td::string::npos);
  ASSERT_TRUE(helper_region.find("find_first_disallowed_control_byte") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, SendAndExecuteCallsitesLogExplicitBoundaryMessages) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ClientJson.cpp");

  ASSERT_TRUE(source.find("ClientJson::send: begin request_size=") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::send: enqueue request extra_id=") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::execute: begin request_size=") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::execute: dispatch parsed request has_extra=") != td::string::npos);
  ASSERT_TRUE(source.find("json_send: begin client_id=") != td::string::npos);
  ASSERT_TRUE(source.find("json_send: forward request client_id=") != td::string::npos);
  ASSERT_TRUE(source.find("json_execute: begin request_size=") != td::string::npos);
  ASSERT_TRUE(source.find("json_execute: dispatch parsed request has_extra=") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, ReceiveCallsitesSanitizeTimeoutAndLogCorrelationGaps) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ClientJson.cpp");

  ASSERT_TRUE(source.find("sanitize_timeout_seconds") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::receive: begin timeout=") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::receive: no response effective_timeout=") != td::string::npos);
  ASSERT_TRUE(source.find("ClientJson::receive: missing @extra correlation for response_id=") != td::string::npos);
  ASSERT_TRUE(source.find("json_receive: begin timeout=") != td::string::npos);
  ASSERT_TRUE(source.find("json_receive: no response effective_timeout=") != td::string::npos);
  ASSERT_TRUE(source.find("json_receive: missing @extra correlation for request_id=") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, CWrapperBoundaryNullClientGuardsAndLogsArePinned) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/td_json_client.cpp");

  ASSERT_TRUE(source.find("try_get_json_client") != td::string::npos);
  ASSERT_TRUE(source.find("td_json_client_send: dropping request because client pointer is null") != td::string::npos);
  ASSERT_TRUE(source.find("td_json_client_receive: returning nullptr because client pointer is null") !=
              td::string::npos);
  ASSERT_TRUE(source.find("td_json_client_destroy: null client pointer, nothing to destroy") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, RuntimeRejectsControlBytePrefixBeforeJsonDecode) {
  td::string request;
  request.push_back(static_cast<char>(0x0e));
  request += "{\"@type\":\"testSquareInt\",\"x\":2}";

  auto error_message = execute_and_get_error_message(td::Slice(request));
  ASSERT_TRUE(error_message.find("Invalid request payload: unexpected control byte at offset 0") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, RuntimeRejectsNonObjectTopLevelPayload) {
  auto error_message = execute_and_get_error_message("\"not_an_object\"");
  ASSERT_TRUE(error_message.find("Expected a JSON object request payload") != td::string::npos);
}

TEST(ClientJsonBoundaryContract, RuntimeReceiveSanitizesInvalidTimeoutValues) {
  td::ClientJson client;

  ASSERT_TRUE(client.receive(-1.0) == nullptr);
  ASSERT_TRUE(client.receive(std::numeric_limits<double>::quiet_NaN()) == nullptr);
}

}  // namespace
