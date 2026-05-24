// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/telegram_api.h"

#include "td/tl/tl_object_store.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using UpdatePtr = td::telegram_api::object_ptr<td::telegram_api::Update>;

struct ParsedUpdate {
  UpdatePtr update;
  td::Status status;
};

constexpr std::int32_t kTlVectorConstructor = 481674261;

template <class StorerT>
void store_message_empty_fixture(StorerT &storer, td::int32 message_id) {
  storer.store_int(td::telegram_api::messageEmpty::ID);
  storer.store_int(0);
  storer.store_int(message_id);
}

td::BufferSlice make_guest_update_fixture(td::int64 query_id, bool include_reference_messages, td::int32 qts) {
  auto flags = include_reference_messages ? 1 : 0;

  td::TlStorerCalcLength calc;
  calc.store_int(td::telegram_api::updateBotGuestChatQuery::ID);
  calc.store_int(flags);
  calc.store_long(query_id);
  store_message_empty_fixture(calc, 1);
  if (include_reference_messages) {
    calc.store_int(kTlVectorConstructor);
    calc.store_int(1);
    store_message_empty_fixture(calc, 2);
  }
  calc.store_int(qts);

  td::BufferSlice payload(calc.get_length());
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  storer.store_int(td::telegram_api::updateBotGuestChatQuery::ID);
  storer.store_int(flags);
  storer.store_long(query_id);
  store_message_empty_fixture(storer, 1);
  if (include_reference_messages) {
    storer.store_int(kTlVectorConstructor);
    storer.store_int(1);
    store_message_empty_fixture(storer, 2);
  }
  storer.store_int(qts);

  return payload;
}

td::BufferSlice make_constructor_only_fixture(std::int32_t constructor) {
  td::BufferSlice payload(sizeof(std::int32_t));
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  td::TlStoreBinary::store(constructor, storer);
  return payload;
}

td::BufferSlice make_negative_flags_fixture() {
  td::BufferSlice payload(sizeof(std::int32_t) * 2);
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  td::TlStoreBinary::store(td::telegram_api::updateBotGuestChatQuery::ID, storer);
  td::TlStoreBinary::store(static_cast<std::int32_t>(-1), storer);
  return payload;
}

ParsedUpdate parse_update_fixture(td::Slice payload) {
  td::BufferSlice owned_payload(payload.str());
  td::TlBufferParser parser(&owned_payload);

  ParsedUpdate result;
  result.update = td::telegram_api::Update::fetch(parser);
  parser.fetch_end();
  result.status = parser.get_status();
  return result;
}

td::string flip_payload_byte(td::Slice payload, std::size_t index, unsigned char mask) {
  auto mutated = payload.str();
  CHECK(index < mutated.size());
  auto byte = static_cast<unsigned char>(mutated[index]);
  mutated[index] = static_cast<char>(byte ^ mask);
  return mutated;
}

std::size_t find_pattern(td::Slice payload, const std::array<unsigned char, 4> &pattern) {
  if (payload.size() < pattern.size()) {
    return td::string::npos;
  }
  for (std::size_t pos = 0; pos + pattern.size() <= payload.size(); pos++) {
    bool matched = true;
    for (std::size_t i = 0; i < pattern.size(); i++) {
      if (static_cast<unsigned char>(payload[pos + i]) != pattern[i]) {
        matched = false;
        break;
      }
    }
    if (matched) {
      return pos;
    }
  }
  return td::string::npos;
}

TEST(GuestQueryRuntimeAdversarial, ParsesWellFormedGuestQueryFixtureWithReferences) {
  auto fixture = make_guest_update_fixture(777777, true, 41);

  auto parsed = parse_update_fixture(fixture.as_slice());
  ASSERT_TRUE(parsed.status.is_ok());
  ASSERT_TRUE(parsed.update != nullptr);
  ASSERT_EQ(td::telegram_api::updateBotGuestChatQuery::ID, parsed.update->get_id());

  auto guest_query_update =
      td::telegram_api::move_object_as<td::telegram_api::updateBotGuestChatQuery>(std::move(parsed.update));
  ASSERT_EQ(777777, guest_query_update->query_id_);
  ASSERT_EQ(41, guest_query_update->qts_);
  ASSERT_TRUE(guest_query_update->message_ != nullptr);
  ASSERT_EQ(1u, guest_query_update->reference_messages_.size());
}

TEST(GuestQueryRuntimeAdversarial, RejectsUnknownConstructorFixtureFailClosed) {
  auto fixture = make_constructor_only_fixture(static_cast<std::int32_t>(0x7f00a11c));

  auto parsed = parse_update_fixture(fixture.as_slice());
  ASSERT_TRUE(parsed.update == nullptr);
  ASSERT_TRUE(parsed.status.is_error());
  ASSERT_TRUE(parsed.status.message().str().find("Unknown constructor found") != td::string::npos);
}

TEST(GuestQueryRuntimeAdversarial, RejectsNegativeFlagsFixtureFailClosed) {
  auto fixture = make_negative_flags_fixture();

  auto parsed = parse_update_fixture(fixture.as_slice());
  ASSERT_TRUE(parsed.update == nullptr);
  ASSERT_TRUE(parsed.status.is_error());
  ASSERT_TRUE(parsed.status.message().str().find("can't be negative") != td::string::npos);
}

TEST(GuestQueryRuntimeAdversarial, RejectsTruncatedGuestQueryFixtureFailClosed) {
  auto fixture = make_guest_update_fixture(12345, true, 9);
  ASSERT_TRUE(fixture.size() > 1);

  auto truncated = fixture.as_slice().str();
  truncated.pop_back();

  auto parsed = parse_update_fixture(td::Slice(truncated));
  ASSERT_TRUE(parsed.update == nullptr);
  ASSERT_TRUE(parsed.status.is_error());
}

TEST(GuestQueryRuntimeAdversarial, RejectsCorruptedReferenceVectorMarkerFailClosed) {
  auto fixture = make_guest_update_fixture(70001, true, 23);
  constexpr std::array<unsigned char, 4> kVectorConstructor = {{0x15, 0xC4, 0xB5, 0x1C}};

  auto vector_pos = find_pattern(fixture.as_slice(), kVectorConstructor);
  ASSERT_TRUE(vector_pos != td::string::npos);

  auto corrupted = flip_payload_byte(fixture.as_slice(), vector_pos, 0x80);
  auto parsed = parse_update_fixture(td::Slice(corrupted));
  ASSERT_TRUE(parsed.update == nullptr);
  ASSERT_TRUE(parsed.status.is_error());
}

TEST(GuestQueryRuntimeAdversarial, LightFuzzDeterministicBitFlipMutationsAreFailClosedOrParsable) {
  auto fixture = make_guest_update_fixture(424242, true, 17);
  ASSERT_TRUE(fixture.size() > 0);

  constexpr td::int32 kIterations = 10000;
  for (td::int32 i = 0; i < kIterations; i++) {
    auto index = static_cast<std::size_t>(i) % fixture.size();
    auto mask = static_cast<unsigned char>(1u << (i % 8));
    auto mutated = flip_payload_byte(fixture.as_slice(), index, mask);
    auto parsed = parse_update_fixture(td::Slice(mutated));
    if (parsed.update != nullptr) {
      ASSERT_EQ(td::telegram_api::updateBotGuestChatQuery::ID, parsed.update->get_id());
      if (parsed.status.is_error()) {
        ASSERT_TRUE(parsed.status.message().str().find("Too much data to fetch") != td::string::npos);
      }
    } else {
      ASSERT_TRUE(parsed.status.is_error());
    }
  }
}

TEST(GuestQueryRuntimeAdversarial, StressRepeatedMalformedFixturesRemainFailClosed) {
  auto negative_flags_fixture = make_negative_flags_fixture();
  auto truncated_fixture = make_guest_update_fixture(3003, true, 11).as_slice().str();
  ASSERT_TRUE(truncated_fixture.size() > 1);
  truncated_fixture.pop_back();

  constexpr td::int32 kIterations = 3000;
  for (td::int32 i = 0; i < kIterations; i++) {
    auto parsed_negative = parse_update_fixture(negative_flags_fixture.as_slice());
    ASSERT_TRUE(parsed_negative.update == nullptr);
    ASSERT_TRUE(parsed_negative.status.is_error());

    auto parsed_truncated = parse_update_fixture(td::Slice(truncated_fixture));
    ASSERT_TRUE(parsed_truncated.update == nullptr);
    ASSERT_TRUE(parsed_truncated.status.is_error());
  }
}

}  // namespace
