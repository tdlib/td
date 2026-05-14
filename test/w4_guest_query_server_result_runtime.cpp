// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/telegram_api.h"

#include "td/tl/tl_object_store.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"
#include "td/utils/tl_parsers.h"

#include <cstdint>

namespace {

td::BufferSlice make_guest_query_result_packet(
    const td::telegram_api::object_ptr<td::telegram_api::InputBotInlineMessageID> &message_id) {
  td::TlStorerCalcLength calc;
  td::TlStoreBoxedUnknown<td::TlStoreObject>::store(message_id, calc);

  td::BufferSlice payload(calc.get_length());
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  td::TlStoreBoxedUnknown<td::TlStoreObject>::store(message_id, storer);
  return payload;
}

td::BufferSlice make_unknown_constructor_packet(std::int32_t constructor) {
  td::BufferSlice payload(sizeof(std::int32_t));
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  td::TlStoreBinary::store(constructor, storer);
  return payload;
}

td::Result<td::string> parse_guest_query_result_inline_message_id(const td::BufferSlice &packet) {
  td::BufferSlice packet_copy(packet.as_slice().str());
  td::TlBufferParser parser(&packet_copy);
  auto message_id = td::telegram_api::messages_setBotGuestChatResult::fetch_result(parser);
  parser.fetch_end();
  if (parser.get_error()) {
    return parser.get_status();
  }
  auto inline_message_id = td::InlineQueriesManager::get_inline_message_id(std::move(message_id));
  if (inline_message_id.empty()) {
    return td::Status::Error("Receive invalid inline message identifier in guest query result");
  }
  return inline_message_id;
}

TEST(W4GuestQueryServerResultRuntime, ParsesInputBotInlineMessageIdResultIntoInlineMessageId) {
  constexpr td::int32 dc_id = 4;
  constexpr td::int64 inline_message_id = 1234567890123;
  constexpr td::int64 access_hash = 998877665544;

  auto packet = make_guest_query_result_packet(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID>(dc_id, inline_message_id, access_hash));
  auto expected = td::InlineQueriesManager::get_inline_message_id(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID>(dc_id, inline_message_id, access_hash));

  auto parsed = parse_guest_query_result_inline_message_id(packet);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(expected, parsed.move_as_ok());
}

TEST(W4GuestQueryServerResultRuntime, ParsesInputBotInlineMessageId64ResultIntoInlineMessageId) {
  constexpr td::int32 dc_id = 6;
  constexpr td::int64 owner_id = 700100200300;
  constexpr td::int32 inline_message_id = 777;
  constexpr td::int64 access_hash = 1234567898765;

  auto packet = make_guest_query_result_packet(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID64>(dc_id, owner_id, inline_message_id, access_hash));
  auto expected = td::InlineQueriesManager::get_inline_message_id(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID64>(dc_id, owner_id, inline_message_id, access_hash));

  auto parsed = parse_guest_query_result_inline_message_id(packet);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(expected, parsed.move_as_ok());
}

TEST(W4GuestQueryServerResultRuntime, RejectsInputBotInlineMessageIdResultWithInvalidDcIdFailClosed) {
  constexpr td::int32 invalid_dc_id = 0;

  auto packet = make_guest_query_result_packet(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID>(invalid_dc_id, 1234567890123, 998877665544));

  auto parsed = parse_guest_query_result_inline_message_id(packet);
  ASSERT_TRUE(parsed.is_error());
  ASSERT_TRUE(parsed.error().message().str().find("invalid inline message identifier") != td::string::npos);
}

TEST(W4GuestQueryServerResultRuntime, RejectsInputBotInlineMessageId64ResultWithInvalidDcIdFailClosed) {
  constexpr td::int32 invalid_dc_id = -17;

  auto packet = make_guest_query_result_packet(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID64>(invalid_dc_id, 700100200300, 777, 1234567898765));

  auto parsed = parse_guest_query_result_inline_message_id(packet);
  ASSERT_TRUE(parsed.is_error());
  ASSERT_TRUE(parsed.error().message().str().find("invalid inline message identifier") != td::string::npos);
}

TEST(W4GuestQueryServerResultRuntime, RejectsUnknownConstructorFailClosed) {
  auto packet = make_unknown_constructor_packet(static_cast<std::int32_t>(0x4f00b11c));

  auto parsed = parse_guest_query_result_inline_message_id(packet);
  ASSERT_TRUE(parsed.is_error());
  ASSERT_TRUE(parsed.error().message().str().find("Unknown constructor found") != td::string::npos);
}

TEST(W4GuestQueryServerResultRuntime, RejectsTruncatedResultPacketFailClosed) {
  auto packet =
      make_guest_query_result_packet(td::make_tl_object<td::telegram_api::inputBotInlineMessageID>(5, 9001, 1234567));
  ASSERT_TRUE(packet.size() > 1);

  auto truncated = packet.as_slice().str();
  truncated.pop_back();

  auto parsed = parse_guest_query_result_inline_message_id(td::BufferSlice(truncated));
  ASSERT_TRUE(parsed.is_error());
}

TEST(W4GuestQueryServerResultRuntime, RejectsTrailingGarbageFailClosed) {
  auto packet =
      make_guest_query_result_packet(td::make_tl_object<td::telegram_api::inputBotInlineMessageID>(8, 42, 7777777));

  auto mutated = packet.as_slice().str();
  mutated.push_back(static_cast<char>(0x7f));

  auto parsed = parse_guest_query_result_inline_message_id(td::BufferSlice(mutated));
  ASSERT_TRUE(parsed.is_error());
}

TEST(W4GuestQueryServerResultRuntime, StressRepeatedMalformedPacketsRemainFailClosed) {
  auto unknown_constructor_packet = make_unknown_constructor_packet(static_cast<std::int32_t>(0x5f00b11c));
  auto valid_packet = make_guest_query_result_packet(
      td::make_tl_object<td::telegram_api::inputBotInlineMessageID64>(3, 1000, 11, 222233334444));

  auto truncated = valid_packet.as_slice().str();
  ASSERT_TRUE(truncated.size() > 1);
  truncated.pop_back();

  constexpr td::int32 kIterations = 3000;
  for (td::int32 i = 0; i < kIterations; i++) {
    auto parsed_unknown = parse_guest_query_result_inline_message_id(unknown_constructor_packet);
    ASSERT_TRUE(parsed_unknown.is_error());

    auto parsed_truncated = parse_guest_query_result_inline_message_id(td::BufferSlice(truncated));
    ASSERT_TRUE(parsed_truncated.is_error());
  }
}

}  // namespace