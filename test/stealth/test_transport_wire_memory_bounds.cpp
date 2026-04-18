#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::parse_tls_client_hello;

td::string build_reference_wire() {
  return build_default_tls_client_hello("www.google.com", "0123456789abcdef", 1712345678,
                                        NetworkRouteHints{.is_known = true, .is_ru = false});
}

TEST(TransportWireMemorySafety, RejectsTruncatedRecordHeaderAtEveryByteBoundary) {
  const auto wire = build_reference_wire();
  ASSERT_TRUE(wire.size() > 5);

  for (size_t len = 0; len < 5; len++) {
    auto parsed = parse_tls_client_hello(td::Slice(wire).substr(0, len));
    ASSERT_TRUE(parsed.is_error());
  }
}

TEST(TransportWireMemorySafety, RejectsHandshakeBodyTruncationNearRecordBoundary) {
  const auto wire = build_reference_wire();
  ASSERT_TRUE(wire.size() > 9);

  for (size_t cut = wire.size() - 1; cut + 4 >= wire.size() && cut > 8; cut--) {
    auto parsed = parse_tls_client_hello(td::Slice(wire).substr(0, cut));
    ASSERT_TRUE(parsed.is_error());
  }
}

TEST(TransportWireMemorySafety, RejectsOversizedLengthField) {
  auto mutable_wire = build_reference_wire();
  ASSERT_TRUE(mutable_wire.size() >= 5);

  mutable_wire[3] = '\xff';
  mutable_wire[4] = '\xff';

  auto parsed = parse_tls_client_hello(mutable_wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TransportWireMemorySafety, StructureParsingPreservesInvariants) {
  auto wire = build_reference_wire();

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto hello = parsed.move_as_ok();

  // Verify basic structure is sound
  ASSERT_EQ(static_cast<int>(0x16), hello.record_type);
  ASSERT_TRUE(hello.key_share_groups.size() > 0);
}

}  // namespace
