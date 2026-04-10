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
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/Transport.h"

#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <cstring>
#include <limits>
#include <set>

namespace {

using td::mtproto::AuthKey;
using td::mtproto::PacketInfo;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;
using td::mtproto::Transport;

constexpr size_t kCryptoRawHeaderSize = 24;
constexpr td::uint16 kDefaultStealthPaddingMin = 12;
constexpr td::uint16 kDefaultStealthPaddingMax = 480;
constexpr size_t kStealthEncryptedFloor = 128;

class BytesStorer final : public td::Storer {
 public:
  explicit BytesStorer(size_t size) : payload_(size, 'x') {
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

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                                     td::make_unique<MockClock>());
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr};
}

AuthKey make_auth_key() {
  return AuthKey(0x0102030405060708ULL, td::string(256, 'k'));
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

size_t minimum_total_packet_size(size_t payload_size, td::uint16 min_padding_bytes) {
  auto encrypted_size =
      align16(std::max(kStealthEncryptedFloor, static_cast<size_t>(16 + payload_size + min_padding_bytes)));
  return kCryptoRawHeaderSize + encrypted_size;
}

TEST(MtprotoCryptoPaddingBucketElimination, BasicPaddingDefaultsToRandomWhenStealthActive) {
  auto fixture = make_test_decorator();

  PacketInfo packet_info;
  fixture.decorator->configure_packet_info(&packet_info);

  ASSERT_TRUE(packet_info.use_random_padding);
  ASSERT_TRUE(packet_info.use_stealth_padding);
  ASSERT_EQ(kDefaultStealthPaddingMin, packet_info.stealth_padding_min_bytes);
  ASSERT_EQ(kDefaultStealthPaddingMax, packet_info.stealth_padding_max_bytes);
}

TEST(MtprotoCryptoPaddingBucketElimination, PacketInfoFlagsOrthogonal) {
  RecordingTransport plain_transport;
  PacketInfo packet_info;
  plain_transport.configure_packet_info(&packet_info);
  ASSERT_FALSE(packet_info.use_random_padding);
  ASSERT_FALSE(packet_info.use_stealth_padding);

  plain_transport.use_random_padding_result = true;
  packet_info = PacketInfo{};
  plain_transport.configure_packet_info(&packet_info);
  ASSERT_TRUE(packet_info.use_random_padding);
  ASSERT_FALSE(packet_info.use_stealth_padding);

  auto fixture = make_test_decorator();
  packet_info = PacketInfo{};
  fixture.decorator->configure_packet_info(&packet_info);
  ASSERT_TRUE(packet_info.use_random_padding);
  ASSERT_TRUE(packet_info.use_stealth_padding);
}

TEST(MtprotoCryptoPaddingBucketElimination, RejectsCryptoPaddingPolicyOverflowLikeValues) {
  MockRng rng(11);
  auto config = StealthConfig::default_config(rng);
  config.crypto_padding_policy.max_padding_bytes = std::numeric_limits<td::uint16>::max();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
}

TEST(MtprotoCryptoPaddingBucketElimination, BucketQuantizationNonStealthHasFewUniqueSizes) {
  std::set<size_t> unique_sizes;
  for (size_t payload_size = 4; payload_size <= 255; payload_size++) {
    PacketInfo packet_info;
    unique_sizes.insert(write_packet_size(payload_size, packet_info));
  }

  ASSERT_TRUE(unique_sizes.size() < 10u);
}

TEST(MtprotoCryptoPaddingBucketElimination, StealthPaddingMinimumIsEnforcedAndSmallPayloadEscapesLegacyBucket) {
  PacketInfo packet_info;
  packet_info.use_random_padding = true;
  packet_info.use_stealth_padding = true;
  packet_info.stealth_padding_min_bytes = 12;
  packet_info.stealth_padding_max_bytes = 12;

  auto packet_size = write_packet_size(4, packet_info);
  ASSERT_EQ(0u, (packet_size - kCryptoRawHeaderSize) % 16u);
  ASSERT_TRUE(packet_size >= minimum_total_packet_size(4, packet_info.stealth_padding_min_bytes));
  ASSERT_TRUE(packet_size >= kCryptoRawHeaderSize + kStealthEncryptedFloor);
}

TEST(MtprotoCryptoPaddingBucketElimination, ConsecutiveSameSizePayloadsProduceDifferentWireSizes) {
  PacketInfo packet_info;
  packet_info.use_random_padding = true;
  packet_info.use_stealth_padding = true;
  packet_info.stealth_padding_min_bytes = kDefaultStealthPaddingMin;
  packet_info.stealth_padding_max_bytes = kDefaultStealthPaddingMax;

  std::set<size_t> unique_sizes;
  for (size_t attempt = 0; attempt < 128; attempt++) {
    unique_sizes.insert(write_packet_size(4, packet_info));
  }

  ASSERT_TRUE(unique_sizes.size() >= 10u);
}

TEST(MtprotoCryptoPaddingBucketElimination, LargePayloadStealthPaddingStillApplied) {
  PacketInfo baseline;
  auto baseline_size = write_packet_size(4096, baseline);

  PacketInfo stealth_packet;
  stealth_packet.use_random_padding = true;
  stealth_packet.use_stealth_padding = true;
  stealth_packet.stealth_padding_min_bytes = 480;
  stealth_packet.stealth_padding_max_bytes = 480;

  auto stealth_size = write_packet_size(4096, stealth_packet);
  ASSERT_EQ(0u, (stealth_size - kCryptoRawHeaderSize) % 16u);
  ASSERT_TRUE(stealth_size > baseline_size);
}

}  // namespace