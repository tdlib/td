// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/Transport.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace {

using td::mtproto::AuthKey;
using td::mtproto::PacketInfo;
using td::mtproto::stealth::CryptoPaddingPolicy;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;
using td::mtproto::Transport;

constexpr size_t kCryptoRawHeaderSize = 24;
constexpr size_t kStealthEncryptedFloor = 128;

class BytesStorer final : public td::Storer {
 public:
  explicit BytesStorer(size_t size) : payload_(size, 'p') {
  }

  size_t size() const final {
    return payload_.size();
  }

  size_t store(td::uint8 *ptr) const final {
    std::memcpy(ptr, payload_.data(), payload_.size());
    return payload_.size();
  }

 private:
  td::string payload_;
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
};

AuthKey make_auth_key() {
  return AuthKey(0x0102030405060708ULL, td::string(256, 's'));
}

size_t align16(size_t value) {
  return (value + 15u) & ~static_cast<size_t>(15u);
}

size_t write_packet_size(size_t payload_size, PacketInfo packet_info) {
  packet_info.version = 2;
  packet_info.no_crypto_flag = false;
  packet_info.salt = 0x1112131415161718ULL;
  packet_info.session_id = 0x2122232425262728ULL;
  BytesStorer storer(payload_size);
  auto packet = Transport::write(storer, make_auth_key(), &packet_info);
  return packet.size();
}

DecoratorFixture make_test_decorator(const StealthConfig &config, bool inner_random_padding) {
  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->use_random_padding_result = inner_random_padding;
  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                                     td::make_unique<MockClock>());
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr};
}

TEST(MtprotoCryptoPaddingBucketEliminationAdversarial,
     DisabledCryptoPaddingPolicyPreservesInnerRandomPaddingWithoutStealthEscalation) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.crypto_padding_policy.enabled = false;

  auto fixture = make_test_decorator(config, true);

  PacketInfo packet_info;
  fixture.decorator->configure_packet_info(&packet_info);

  ASSERT_TRUE(fixture.decorator->use_random_padding());
  ASSERT_TRUE(packet_info.use_random_padding);
  ASSERT_FALSE(packet_info.use_stealth_padding);
  ASSERT_EQ(0, packet_info.stealth_padding_min_bytes);
  ASSERT_EQ(0, packet_info.stealth_padding_max_bytes);
}

TEST(MtprotoCryptoPaddingBucketEliminationAdversarial, StealthPaddingAcceptsMaximumFailClosedBoundExactly) {
  PacketInfo packet_info;
  packet_info.use_random_padding = true;
  packet_info.use_stealth_padding = true;
  packet_info.stealth_padding_min_bytes = CryptoPaddingPolicy::kMaxPaddingBytes;
  packet_info.stealth_padding_max_bytes = CryptoPaddingPolicy::kMaxPaddingBytes;

  for (size_t payload_size : std::array<size_t, 4>{{0, 4, 255, 4096}}) {
    auto packet_size = write_packet_size(payload_size, packet_info);
    auto expected_encrypted_size =
        align16(std::max(kStealthEncryptedFloor,
                         static_cast<size_t>(16 + payload_size + CryptoPaddingPolicy::kMaxPaddingBytes)));

    ASSERT_EQ(kCryptoRawHeaderSize + expected_encrypted_size, packet_size);
  }
}

TEST(MtprotoCryptoPaddingBucketEliminationAdversarial, StealthPaddingRangeEndpointsRemainReachableAcrossSamples) {
  PacketInfo packet_info;
  packet_info.use_random_padding = true;
  packet_info.use_stealth_padding = true;
  packet_info.stealth_padding_min_bytes = CryptoPaddingPolicy::kMinPaddingBytes;
  packet_info.stealth_padding_max_bytes = CryptoPaddingPolicy::kDefaultMaxPaddingBytes;

  const size_t payload_size = 4096;
  const auto expected_min =
      kCryptoRawHeaderSize + align16(16 + payload_size + CryptoPaddingPolicy::kMinPaddingBytes);
  const auto expected_max =
      kCryptoRawHeaderSize + align16(16 + payload_size + CryptoPaddingPolicy::kDefaultMaxPaddingBytes);

  size_t min_seen = std::numeric_limits<size_t>::max();
  size_t max_seen = 0;
  std::set<size_t> unique_sizes;
  for (size_t attempt = 0; attempt < 2048; attempt++) {
    auto packet_size = write_packet_size(payload_size, packet_info);
    min_seen = std::min(min_seen, packet_size);
    max_seen = std::max(max_seen, packet_size);
    unique_sizes.insert(packet_size);
  }

  ASSERT_TRUE(min_seen <= expected_min + 32);
  ASSERT_TRUE(max_seen >= expected_max - 32);
  ASSERT_TRUE(unique_sizes.size() >= 20u);
}

TEST(MtprotoCryptoPaddingBucketEliminationAdversarial, StealthPaddingPayloadSweepDefeatsLegacyBucketCollapse) {
  std::set<size_t> legacy_sizes;
  std::set<size_t> stealth_sizes;

  PacketInfo stealth_packet_info;
  stealth_packet_info.use_random_padding = true;
  stealth_packet_info.use_stealth_padding = true;
  stealth_packet_info.stealth_padding_min_bytes = CryptoPaddingPolicy::kMinPaddingBytes;
  stealth_packet_info.stealth_padding_max_bytes = CryptoPaddingPolicy::kDefaultMaxPaddingBytes;

  for (size_t payload_size = 4; payload_size <= 512; payload_size++) {
    legacy_sizes.insert(write_packet_size(payload_size, PacketInfo{}));
    for (size_t attempt = 0; attempt < 4; attempt++) {
      stealth_sizes.insert(write_packet_size(payload_size, stealth_packet_info));
    }
  }

  ASSERT_TRUE(legacy_sizes.size() < 12u);
  ASSERT_TRUE(stealth_sizes.size() >= 32u);
  ASSERT_TRUE(stealth_sizes.size() > legacy_sizes.size() * 3u);
}

}  // namespace