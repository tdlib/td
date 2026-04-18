//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <random>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;

constexpr td::uint16 kSupportedGroupsExtensionType = 0x000A;
constexpr td::uint16 kKeyShareExtensionType = 0x0033;

static td::string build_reference_wire() {
  return build_default_tls_client_hello("www.google.com", "0123456789abcdef", 1712345678,
                                        NetworkRouteHints{.is_known = true, .is_ru = false});
}

static void assert_basic_hello_invariants(const ParsedClientHello &hello, size_t wire_size) {
  ASSERT_EQ(static_cast<int>(0x16), hello.record_type);
  ASSERT_EQ(static_cast<int>(0x01), hello.handshake_type);
  ASSERT_EQ(static_cast<size_t>(hello.record_length) + 5u, wire_size);
  ASSERT_TRUE(hello.handshake_length + 4u <= hello.record_length);
  ASSERT_TRUE(hello.session_id.size() <= 32u);
}

TEST(TransportWireParserAdversarial, RejectsVerySmallInputsFailClosed) {
  const auto reference = build_reference_wire();
  for (size_t len = 0; len < 10 && len < reference.size(); len++) {
    auto parsed = parse_tls_client_hello(td::Slice(reference).substr(0, len));
    ASSERT_TRUE(parsed.is_error());
  }
}

TEST(TransportWireParserAdversarial, RejectsRecordLengthClaimThatExceedsBuffer) {
  auto wire = build_reference_wire();
  ASSERT_TRUE(wire.size() > 5);

  wire[3] = static_cast<char>(0xFF);
  wire[4] = static_cast<char>(0xFF);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TransportWireParserAdversarial, RejectsHandshakeLengthClaimThatExceedsRecord) {
  auto wire = build_reference_wire();
  ASSERT_TRUE(wire.size() > 9);

  wire[6] = static_cast<char>(0xFF);
  wire[7] = static_cast<char>(0xFF);
  wire[8] = static_cast<char>(0xFF);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TransportWireParserAdversarial, RejectsCorruptedKeyShareEntryLength) {
  const auto original = build_reference_wire();
  auto parsed = parse_tls_client_hello(original);
  ASSERT_TRUE(parsed.is_ok());

  const auto *key_share = find_extension(parsed.ok_ref(), kKeyShareExtensionType);
  ASSERT_TRUE(key_share != nullptr);
  ASSERT_TRUE(key_share->value.size() >= 6);

  auto offset = static_cast<size_t>(key_share->value.begin() - parsed.ok_ref().owned_wire->data());
  auto mutated = original;
  mutated[offset + 4] = '\x00';
  mutated[offset + 5] = '\x00';

  auto mutated_parsed = parse_tls_client_hello(mutated);
  ASSERT_TRUE(mutated_parsed.is_error());
}

TEST(TransportWireParserAdversarial, RejectsDuplicateSupportedGroups) {
  const auto original = build_reference_wire();
  auto parsed = parse_tls_client_hello(original);
  ASSERT_TRUE(parsed.is_ok());

  const auto *supported_groups = find_extension(parsed.ok_ref(), kSupportedGroupsExtensionType);
  ASSERT_TRUE(supported_groups != nullptr);
  ASSERT_TRUE(supported_groups->value.size() >= 6);

  auto offset = static_cast<size_t>(supported_groups->value.begin() - parsed.ok_ref().owned_wire->data());
  auto mutated = original;
  mutated[offset + 2] = mutated[offset + 4];
  mutated[offset + 3] = mutated[offset + 5];

  auto mutated_parsed = parse_tls_client_hello(mutated);
  ASSERT_TRUE(mutated_parsed.is_error());
}

TEST(TransportWireParserAdversarial, DeterministicByteFlipStressStaysFailClosedOrValid) {
  const auto original = build_reference_wire();
  ASSERT_TRUE(!original.empty());

  for (size_t i = 0; i < original.size(); i += 7) {
    auto mutated = original;
    mutated[i] = static_cast<char>(static_cast<unsigned char>(mutated[i]) ^ 0x5A);

    auto parsed = parse_tls_client_hello(mutated);
    if (parsed.is_ok()) {
      assert_basic_hello_invariants(parsed.ok_ref(), mutated.size());
    }
  }
}

TEST(TransportWireParserAdversarial, Fuzz10kDeterministicMutationsStayFailClosedOrValid) {
  const auto original = build_reference_wire();
  ASSERT_TRUE(!original.empty());

  std::mt19937_64 rng(0xD15EA5EULL);
  std::uniform_int_distribution<size_t> pos_dist(0, original.size() - 1);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  std::uniform_int_distribution<int> action_dist(0, 2);

  constexpr size_t kIterations = 10000;
  for (size_t i = 0; i < kIterations; i++) {
    auto mutated = original;

    switch (action_dist(rng)) {
      case 0: {
        auto pos = pos_dist(rng);
        mutated[pos] = static_cast<char>(byte_dist(rng));
        break;
      }
      case 1: {
        auto pos = pos_dist(rng);
        size_t end = std::min(mutated.size(), pos + static_cast<size_t>(1 + (rng() % 32)));
        for (size_t j = pos; j < end; j++) {
          mutated[j] = static_cast<char>(byte_dist(rng));
        }
        break;
      }
      default: {
        if (mutated.size() > 1) {
          size_t cut = rng() % mutated.size();
          mutated.resize(cut);
        }
        break;
      }
    }

    auto parsed = parse_tls_client_hello(mutated);
    if (parsed.is_ok()) {
      assert_basic_hello_invariants(parsed.ok_ref(), mutated.size());
    }
  }
}

}  // namespace
