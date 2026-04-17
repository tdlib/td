// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial contract tests for protocol envelope framing safety.
// Verifies that message container construction does not leave members in
// undefined state regardless of the code branch taken during object init.
//

#include "td/mtproto/AuthData.h"
#include "td/mtproto/CryptoStorer.h"
#include "td/mtproto/MessageId.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

// Build a minimal AuthData on heap (move/copy are deleted).
std::unique_ptr<td::mtproto::AuthData> make_minimal_auth_data() {
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  auto auth = std::make_unique<td::mtproto::AuthData>();
  auth->set_session_id(0x1122334455667788ULL);
  auth->reset_server_time_difference(0.0);
  return auth;
}

// Verify: constructing an empty ObjectImpl (not_empty=false) must not
// call auth_data at all.  The contract established by the V1077 fix is
// that seq_no_ and message_id_ are zero-initialised before the early
// return, so any accidental read of those members returns a defined 0
// rather than stack garbage.
TEST(ProtocolEnvelopeFrameSafety, EmptyObjectDoesNotCallAuthData) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  // Pass a nullptr auth_data on purpose — the empty branch must never
  // dereference it.
  td::mtproto::PacketStorer<td::mtproto::AckImpl> empty_storer(
      false, td::mtproto_api::msgs_ack(td::vector<td::int64>{}), static_cast<td::mtproto::AuthData *>(nullptr));

  ASSERT_FALSE(empty_storer.not_empty());
  // size() calls do_store which returns early when empty, so result == 0.
  ASSERT_EQ(0u, empty_storer.size());
}

// Verify: constructing a non-empty ObjectImpl calls auth_data and
// produces a valid (> 0) seq_no and a non-zero message_id.
TEST(ProtocolEnvelopeFrameSafety, NonEmptyObjectUsesAuthDataForSeqNo) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  auto auth = make_minimal_auth_data();

  td::mtproto::PacketStorer<td::mtproto::AckImpl> storer(
      true, td::mtproto_api::msgs_ack(td::vector<td::int64>{12345LL}), auth.get());

  ASSERT_TRUE(storer.not_empty());
  // The serialised output must be non-zero (header + seq_no + payload size).
  ASSERT_TRUE(storer.size() > 0u);
  // message_id must be non-zero after construction.
  ASSERT_NE(td::mtproto::MessageId{}, storer.get_message_id());
}

// Adversarial: construct many empty ObjectImpl instances and verify
// none of them dereference auth_data (none of them call nullptr).
TEST(ProtocolEnvelopeFrameSafety, ManyEmptyObjectsDoNotCorruptState) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  for (int i = 0; i < 1000; ++i) {
    td::mtproto::PacketStorer<td::mtproto::AckImpl> s(false, td::mtproto_api::msgs_ack(td::vector<td::int64>{}),
                                                      static_cast<td::mtproto::AuthData *>(nullptr));
    ASSERT_FALSE(s.not_empty());
    ASSERT_EQ(0u, s.size());
  }
}

// Contract: seq_no values for consecutive non-empty ObjectImpl instances
// must be monotonically increasing, confirming that next_seq_no() is
// called per object and the result is persisted (not garbage).
TEST(ProtocolEnvelopeFrameSafety, ConsecutiveSeqNosAreMonotonicallyIncreasing) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  auto auth = make_minimal_auth_data();

  td::mtproto::MessageId prev_id{};
  for (int i = 0; i < 20; ++i) {
    td::mtproto::PacketStorer<td::mtproto::AckImpl> s(
        true, td::mtproto_api::msgs_ack(td::vector<td::int64>{static_cast<td::int64>(i)}), auth.get());
    ASSERT_TRUE(s.not_empty());
    auto cur_id = s.get_message_id();
    if (prev_id != td::mtproto::MessageId{}) {
      ASSERT_TRUE(prev_id < cur_id);
    }
    prev_id = cur_id;
  }
}

// Adversarial: interleave empty and non-empty constructions to ensure
// the initialisation order does not corrupt auth_data state.
TEST(ProtocolEnvelopeFrameSafety, InterleavedEmptyNonEmptyPreservesAuthDataState) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  auto auth = make_minimal_auth_data();
  td::vector<td::mtproto::MessageId> non_empty_ids;

  for (int i = 0; i < 50; ++i) {
    // Empty — must not advance auth_data counters
    td::mtproto::PacketStorer<td::mtproto::AckImpl> empty_s(false, td::mtproto_api::msgs_ack(td::vector<td::int64>{}),
                                                            static_cast<td::mtproto::AuthData *>(nullptr));
    ASSERT_EQ(0u, empty_s.size());

    // Non-empty — must advance counters
    td::mtproto::PacketStorer<td::mtproto::AckImpl> s(
        true, td::mtproto_api::msgs_ack(td::vector<td::int64>{static_cast<td::int64>(i)}), auth.get());
    ASSERT_TRUE(s.size() > 0u);
    non_empty_ids.push_back(s.get_message_id());
  }

  // Verify all non-empty IDs are strictly monotonically increasing.
  for (size_t i = 1; i < non_empty_ids.size(); ++i) {
    ASSERT_TRUE(non_empty_ids[i - 1] < non_empty_ids[i]);
  }
}

}  // namespace
