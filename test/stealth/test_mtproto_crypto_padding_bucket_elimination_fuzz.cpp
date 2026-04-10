// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/Transport.h"

#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

using td::mtproto::AuthKey;
using td::mtproto::PacketInfo;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;
using td::mtproto::Transport;

constexpr size_t kCryptoRawHeaderSize = 24;
constexpr size_t kStealthEncryptedFloor = 128;

class BytesStorer final : public td::Storer {
 public:
  explicit BytesStorer(size_t size) : payload_(size, 'z') {
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

AuthKey make_auth_key() {
  return AuthKey(0x1111222233334444ULL, td::string(256, 'q'));
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

TEST(MtprotoCryptoPaddingBucketEliminationFuzz, LightFuzzRandomPayloadSizeProducesAlignedAndBoundedPackets) {
  MockRng rng(17);
  PacketInfo packet_info;
  packet_info.use_random_padding = true;
  packet_info.use_stealth_padding = true;
  packet_info.stealth_padding_min_bytes = 12;
  packet_info.stealth_padding_max_bytes = 480;

  for (size_t attempt = 0; attempt < 1000; attempt++) {
    auto payload_size = static_cast<size_t>(rng.bounded(65536));
    auto packet_size = write_packet_size(payload_size, packet_info);
    auto min_encrypted_size = align16(std::max(kStealthEncryptedFloor, static_cast<size_t>(16 + payload_size + 12)));
    auto max_encrypted_size = align16(std::max(kStealthEncryptedFloor, static_cast<size_t>(16 + payload_size + 480)));

    ASSERT_EQ(0u, (packet_size - kCryptoRawHeaderSize) % 16u);
    ASSERT_TRUE(packet_size >= kCryptoRawHeaderSize + min_encrypted_size);
    ASSERT_TRUE(packet_size <= kCryptoRawHeaderSize + max_encrypted_size);
  }
}

TEST(MtprotoCryptoPaddingBucketEliminationFuzz, RejectsPaddingPolicyThatWouldOverflowFailClosedBounds) {
  MockRng rng(18);
  auto config = StealthConfig::default_config(rng);
  config.crypto_padding_policy.min_padding_bytes = 12;
  config.crypto_padding_policy.max_padding_bytes = std::numeric_limits<td::uint16>::max();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
}

}  // namespace