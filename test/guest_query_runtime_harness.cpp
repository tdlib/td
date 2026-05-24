// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/GuestQueryQtsUpdate.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

enum class HarnessEvent : std::uint8_t {
  ConvertReference,
  ConvertMain,
  SendUpdate,
};

td::telegram_api::object_ptr<td::telegram_api::Message> make_message_empty(td::int32 id) {
  auto message = td::make_tl_object<td::telegram_api::messageEmpty>();
  message->id_ = id;
  return message;
}

td::td_api::object_ptr<td::td_api::message> make_message_object() {
  return td::td_api::make_object<td::td_api::message>();
}

TEST(GuestQueryRuntimeHarness, RejectsNonPositiveIdentifierBeforeAnyConversion) {
  td::vector<td::telegram_api::object_ptr<td::telegram_api::Message>> references;
  references.push_back(make_message_empty(10));

  std::size_t convert_calls = 0;
  std::size_t send_calls = 0;
  auto result = td::dispatch_guest_query_qts_update(
      0, std::move(references), make_message_empty(20),
      [&](td::telegram_api::object_ptr<td::telegram_api::Message> &&message)
          -> td::td_api::object_ptr<td::td_api::message> {
        static_cast<void>(message);
        convert_calls++;
        return make_message_object();
      },
      [&](td::int64 query_id, td::td_api::object_ptr<td::td_api::message> &&message,
          td::vector<td::td_api::object_ptr<td::td_api::message>> &&reference_messages) {
        static_cast<void>(query_id);
        static_cast<void>(message);
        static_cast<void>(reference_messages);
        send_calls++;
      });

  ASSERT_EQ(static_cast<int>(td::GuestQueryQtsUpdateResult::InvalidQueryId), static_cast<int>(result));
  ASSERT_EQ(0u, convert_calls);
  ASSERT_EQ(0u, send_calls);
}

TEST(GuestQueryRuntimeHarness, ConvertsMainMessageBeforeReferenceMessagesAndDispatchesOnce) {
  td::vector<td::telegram_api::object_ptr<td::telegram_api::Message>> references;
  references.push_back(make_message_empty(11));

  std::vector<HarnessEvent> events;
  td::int64 sent_query_id = 0;
  std::size_t sent_reference_count = 0;

  auto result = td::dispatch_guest_query_qts_update(
      77, std::move(references), make_message_empty(22),
      [&](td::telegram_api::object_ptr<td::telegram_api::Message> &&message)
          -> td::td_api::object_ptr<td::td_api::message> {
        auto *message_empty = static_cast<const td::telegram_api::messageEmpty *>(message.get());
        events.push_back(message_empty->id_ == 22 ? HarnessEvent::ConvertMain : HarnessEvent::ConvertReference);
        return make_message_object();
      },
      [&](td::int64 query_id, td::td_api::object_ptr<td::td_api::message> &&message,
          td::vector<td::td_api::object_ptr<td::td_api::message>> &&reference_messages) {
        static_cast<void>(message);
        events.push_back(HarnessEvent::SendUpdate);
        sent_query_id = query_id;
        sent_reference_count = reference_messages.size();
      });

  ASSERT_EQ(static_cast<int>(td::GuestQueryQtsUpdateResult::UpdateSent), static_cast<int>(result));
  ASSERT_EQ(3u, events.size());
  ASSERT_EQ(static_cast<int>(HarnessEvent::ConvertMain), static_cast<int>(events[0]));
  ASSERT_EQ(static_cast<int>(HarnessEvent::ConvertReference), static_cast<int>(events[1]));
  ASSERT_EQ(static_cast<int>(HarnessEvent::SendUpdate), static_cast<int>(events[2]));
  ASSERT_EQ(77, sent_query_id);
  ASSERT_EQ(1u, sent_reference_count);
}

TEST(GuestQueryRuntimeHarness, SkipsNullReferenceMessagesButStillDispatchesMainMessage) {
  td::vector<td::telegram_api::object_ptr<td::telegram_api::Message>> references;
  references.push_back(make_message_empty(12));
  references.push_back(make_message_empty(13));

  std::size_t convert_calls = 0;
  std::size_t sent_reference_count = 0;

  auto result = td::dispatch_guest_query_qts_update(
      88, std::move(references), make_message_empty(23),
      [&](td::telegram_api::object_ptr<td::telegram_api::Message> &&message)
          -> td::td_api::object_ptr<td::td_api::message> {
        auto *message_empty = static_cast<const td::telegram_api::messageEmpty *>(message.get());
        convert_calls++;
        if (message_empty->id_ == 12) {
          return nullptr;
        }
        return make_message_object();
      },
      [&](td::int64 query_id, td::td_api::object_ptr<td::td_api::message> &&message,
          td::vector<td::td_api::object_ptr<td::td_api::message>> &&reference_messages) {
        static_cast<void>(query_id);
        static_cast<void>(message);
        sent_reference_count = reference_messages.size();
      });

  ASSERT_EQ(static_cast<int>(td::GuestQueryQtsUpdateResult::UpdateSent), static_cast<int>(result));
  ASSERT_EQ(3u, convert_calls);
  ASSERT_EQ(1u, sent_reference_count);
}

TEST(GuestQueryRuntimeHarness, FailsClosedWhenMainMessageConversionReturnsNull) {
  td::vector<td::telegram_api::object_ptr<td::telegram_api::Message>> references;
  references.push_back(make_message_empty(14));

  std::size_t main_convert_calls = 0;
  std::size_t reference_convert_calls = 0;
  std::size_t send_calls = 0;

  auto result = td::dispatch_guest_query_qts_update(
      99, std::move(references), make_message_empty(24),
      [&](td::telegram_api::object_ptr<td::telegram_api::Message> &&message)
          -> td::td_api::object_ptr<td::td_api::message> {
        auto *message_empty = static_cast<const td::telegram_api::messageEmpty *>(message.get());
        if (message_empty->id_ == 24) {
          main_convert_calls++;
          return nullptr;
        }
        reference_convert_calls++;
        return make_message_object();
      },
      [&](td::int64 query_id, td::td_api::object_ptr<td::td_api::message> &&message,
          td::vector<td::td_api::object_ptr<td::td_api::message>> &&reference_messages) {
        static_cast<void>(query_id);
        static_cast<void>(message);
        static_cast<void>(reference_messages);
        send_calls++;
      });

  ASSERT_EQ(static_cast<int>(td::GuestQueryQtsUpdateResult::EmptyMessage), static_cast<int>(result));
  ASSERT_EQ(1u, main_convert_calls);
  ASSERT_EQ(0u, reference_convert_calls);
  ASSERT_EQ(0u, send_calls);
}

}  // namespace