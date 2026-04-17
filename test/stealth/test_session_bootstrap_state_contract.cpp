// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract and adversarial tests for session bootstrap initial state.
// Verifies that the authentication handshake initialises all cryptographic
// fields to deterministic values before the first protocol round-trip, in
// particular that the nonce fields are zero-initialised rather than left as
// stack garbage (fix for V730 / PVS-Studio finding).
//

#include "td/mtproto/DhCallback.h"
#include "td/mtproto/Handshake.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Minimal Callback to capture the raw bytes sent during resume().
// ─────────────────────────────────────────────────────────────────────────────
// Minimal HandshakeContext that returns a real public key interface.
// Used in adversarial tests where the handshake will try to process (and
// reject) a malformed server response.
class MinimalHandshakeContext final : public td::mtproto::AuthKeyHandshakeContext {
 public:
  td::mtproto::DhCallback *get_dh_callback() override {
    return nullptr;
  }
  td::mtproto::PublicRsaKeyInterface *get_public_rsa_key_interface() override {
    return public_rsa_key_.get();
  }

 private:
  std::shared_ptr<td::mtproto::PublicRsaKeyInterface> public_rsa_key_ = td::PublicRsaKeySharedMain::create(true);
};

class CapturingCallback final : public td::mtproto::AuthKeyHandshake::Callback {
 public:
  void send_no_crypto(const td::Storer &storer) override {
    td::string buf;
    buf.resize(storer.size());
    auto stored = storer.store(reinterpret_cast<td::uint8 *>(&buf[0]));
    CHECK(stored == buf.size());
    sent_messages_.push_back(std::move(buf));
  }

  const td::vector<td::string> &sent_messages() const {
    return sent_messages_;
  }

 private:
  td::vector<td::string> sent_messages_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Contract: freshly constructed handshake is in the Start state.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, FreshHandshakeIsNotReadyForFinish) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  ASSERT_FALSE(hs.is_ready_for_finish());
}

// ─────────────────────────────────────────────────────────────────────────────
// Contract: after clear() the handshake returns to a deterministic state.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, ClearedHandshakeIsNotReadyForFinish) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  hs.clear();
  ASSERT_FALSE(hs.is_ready_for_finish());
}

// ─────────────────────────────────────────────────────────────────────────────
// Black-hat test: two independent handshake instances started with the
// same DC/expires parameters must produce DIFFERENT first messages
// because the nonce is filled via cryptographically secure RNG, not from
// a zero-initialised field.  A zero nonce would be a static fingerprint.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, TwoFreshHandshakesDifferentFirstMessage) {
  td::mtproto::AuthKeyHandshake hs1(1, 0);
  td::mtproto::AuthKeyHandshake hs2(1, 0);

  CapturingCallback cb1, cb2;
  hs1.resume(&cb1);
  hs2.resume(&cb2);

  ASSERT_EQ(1u, cb1.sent_messages().size());
  ASSERT_EQ(1u, cb2.sent_messages().size());

  // The first messages carry the client nonce.  They should be different
  // with overwhelming probability due to 128-bit random nonce selection.
  ASSERT_NE(cb1.sent_messages()[0], cb2.sent_messages()[0]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial: large batch of handshakes — every first message must be
// unique.  If nonces were zero-initialised but not later overwritten by
// secure RNG, all would be identical.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, BatchHandshakesAllUniqueFirstMessages) {
  constexpr int kCount = 100;
  td::vector<td::string> first_msgs;
  first_msgs.reserve(kCount);

  for (int i = 0; i < kCount; ++i) {
    td::mtproto::AuthKeyHandshake hs(1, 0);
    CapturingCallback cb;
    hs.resume(&cb);
    ASSERT_EQ(1u, cb.sent_messages().size());
    first_msgs.push_back(cb.sent_messages()[0]);
  }

  // All messages must be distinct.
  for (int i = 0; i < kCount; ++i) {
    for (int j = i + 1; j < kCount; ++j) {
      ASSERT_NE(first_msgs[i], first_msgs[j]);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial: minimum_server_entry_count must be >= 2 — fewer server
// entries indicate a potential stripped/tampered server response.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, MinimumServerEntryCountAtLeastTwo) {
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::minimum_server_entry_count() >= 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial: should_warn_on_server_entry_count must warn for counts
// below the minimum.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, WarnOnBelowMinimumEntryCount) {
  const size_t min_count = td::mtproto::AuthKeyHandshake::minimum_server_entry_count();
  if (min_count > 0) {
    ASSERT_TRUE(td::mtproto::AuthKeyHandshake::should_warn_on_server_entry_count(min_count - 1));
  }
}

TEST(SessionBootstrapStateContract, NoWarnOnSufficientEntryCount) {
  const size_t min_count = td::mtproto::AuthKeyHandshake::minimum_server_entry_count();
  ASSERT_FALSE(td::mtproto::AuthKeyHandshake::should_warn_on_server_entry_count(min_count));
  ASSERT_FALSE(td::mtproto::AuthKeyHandshake::should_warn_on_server_entry_count(min_count + 10));
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial: sending a malformed message to a freshly created handshake
// must return an error and not cause UB/crash.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, MalformedMessageReturnsError) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  CapturingCallback cb;
  MinimalHandshakeContext ctx;
  hs.resume(&cb);

  // Feed short garbage — must return an error, not crash.
  td::string garbage(8, '\x00');
  auto status = hs.on_message(td::Slice(garbage), &cb, &ctx);
  ASSERT_TRUE(status.is_error());
}

TEST(SessionBootstrapStateContract, EmptyMessageReturnsError) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  CapturingCallback cb;
  MinimalHandshakeContext ctx;
  hs.resume(&cb);

  auto status = hs.on_message(td::Slice(), &cb, &ctx);
  ASSERT_TRUE(status.is_error());
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial black-hat: truncated valid-looking TL prefix should be
// rejected without UB.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, TruncatedTlPrefixReturnsError) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  CapturingCallback cb;
  MinimalHandshakeContext ctx;
  hs.resume(&cb);

  // Create a buffer with a plausible 4-byte constructor ID but no body.
  td::uint8 buf[4] = {0x63, 0x24, 0x16, 0x05};  // resPQ constructor
  auto status = hs.on_message(td::Slice(buf, 4), &cb, &ctx);
  ASSERT_TRUE(status.is_error());
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial black-hat: all-ones payload (boundary fill attack).
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, AllOnesBytePayloadReturnsError) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  CapturingCallback cb;
  MinimalHandshakeContext ctx;
  hs.resume(&cb);

  td::string all_ones(256, '\xff');
  auto status = hs.on_message(td::Slice(all_ones), &cb, &ctx);
  ASSERT_TRUE(status.is_error());
}

// ─────────────────────────────────────────────────────────────────────────────
// Adversarial black-hat: maximum-length payload (resource exhaustion probe).
// Must not crash/hang.
// ─────────────────────────────────────────────────────────────────────────────
TEST(SessionBootstrapStateContract, MaxLengthPayloadReturnsError) {
  td::mtproto::AuthKeyHandshake hs(1, 0);
  CapturingCallback cb;
  MinimalHandshakeContext ctx;
  hs.resume(&cb);

  // 64 KiB of zeros
  td::string big(65536, '\x00');
  auto status = hs.on_message(td::Slice(big), &cb, &ctx);
  ASSERT_TRUE(status.is_error());
}

}  // namespace
