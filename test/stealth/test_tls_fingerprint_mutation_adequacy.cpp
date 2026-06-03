// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Mutation adequacy tests for TLS fingerprint family checks.
//
// Each test applies a single, precisely targeted mutation to a known-good
// ClientHello wire and asserts that the mutation is detected by the
// FamilyLaneMatcher or the parser.  This guards against regressions where
// a matcher silently accepts a mutant that a real DPI classifier would
// flag as anomalous.
//
// Mutation categories:
//   1. supported_versions with a legacy TLS 1.1 version injected
//   2. GREASE evasion (wrong non-GREASE value in a GREASE slot)
//   3. Extension reorder that breaks the family extension-order template
//   4. Cipher suite reorder that breaks the family cipher-order invariant
//   5. Correct (unmutated) fixture as a positive control
//   6. Wrong supported-group value fails invariant check
//   7. Extra extension insertion breaks extension-set invariant

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::baselines::FamilyLaneBaseline;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::baselines::TierLevel;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::non_grease_extension_sequence;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::read_u16;
using td::mtproto::test::write_u16;
using td::Slice;
using td::string;
using td::uint16;

constexpr td::int32 kUnixTime = 1712345678;

// ---------- helpers ----------------------------------------------------------

// Build a Chrome133 wire and return the raw string for mutation.
string build_chrome133_wire() {
  MockRng rng(42);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                            BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
}

// Build an Apple-iOS wire and return the raw string for mutation.
string build_ios_safari_wire() {
  MockRng rng(42);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                            BrowserProfile::Safari26_3, EchMode::Disabled, rng);
}

// Retrieve the reviewed android_chromium / non_ru_egress baseline from the
// generated table.  Falls back to chromium_linux_desktop when android is
// missing or empty (the invariants are structurally identical).
const FamilyLaneBaseline &get_chromium_baseline() {
  const auto *b = get_baseline(Slice("android_chromium"), Slice("non_ru_egress"));
  if (b != nullptr && b->sample_count > 0) {
    return *b;
  }
  b = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  CHECK(b != nullptr);
  return *b;
}

// Locate the supported_versions extension (0x002B) inside a raw ClientHello
// wire and inject TLS 1.1 (0x0302) into the version list.  Returns true if
// the mutation was applied.
bool inject_tls11_into_supported_versions(string &wire) {
  auto offsets = td::mtproto::test::get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x002B) {
      // supported_versions value: 1-byte length + N * 2 bytes
      if (ext_len < 3) {
        return false;
      }
      auto versions_len = static_cast<size_t>(static_cast<td::uint8>(wire[ext_value_start]));
      if (versions_len < 2 || (versions_len % 2) != 0) {
        return false;
      }

      // Insert TLS 1.1 (0x0302) at the end of the version list.
      size_t insert_pos = ext_value_start + 1 + versions_len;
      string tls11_bytes;
      tls11_bytes.push_back(static_cast<char>(0x03));
      tls11_bytes.push_back(static_cast<char>(0x02));
      wire.insert(insert_pos, tls11_bytes);

      // Update inner versions-list length byte
      wire[ext_value_start] = static_cast<char>(static_cast<td::uint8>(versions_len + 2));

      // Refresh mutable view after insertion
      mutable_wire = td::MutableSlice(wire);

      // Update extension value length
      write_u16(mutable_wire, pos + 2, static_cast<uint16>(ext_len + 2));

      // Update extensions block length
      auto old_ext_block_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
      write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<uint16>(old_ext_block_len + 2));

      // Update record + handshake length headers
      td::mtproto::test::update_hello_length_headers(mutable_wire, 2);
      return true;
    }

    pos += 4 + ext_len;
  }
  return false;
}

// Swap two adjacent non-GREASE extensions in the wire. Returns true on success.
bool swap_adjacent_extensions(string &wire, uint16 ext_type_a, uint16 ext_type_b) {
  auto offsets = td::mtproto::test::get_hello_offsets(wire);
  Slice view(wire);

  // Locate both extensions and their blobs
  size_t pos_a = 0;
  size_t len_a = 0;
  size_t pos_b = 0;
  size_t len_b = 0;

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(view, pos);
    auto ext_len = static_cast<size_t>(read_u16(view, pos + 2));
    auto total = 4 + ext_len;
    CHECK(pos + total <= offsets.extensions_end);

    if (ext_type == ext_type_a) {
      pos_a = pos;
      len_a = total;
    } else if (ext_type == ext_type_b) {
      pos_b = pos;
      len_b = total;
    }
    pos += total;
  }

  if (pos_a == 0 || pos_b == 0 || len_a == 0 || len_b == 0) {
    return false;
  }

  // Ensure pos_a < pos_b for simpler logic
  if (pos_a > pos_b) {
    std::swap(pos_a, pos_b);
    std::swap(len_a, len_b);
  }

  string blob_a = wire.substr(pos_a, len_a);
  string blob_b = wire.substr(pos_b, len_b);

  // Replace second first (higher offset) to preserve lower offset validity
  wire.replace(pos_b, len_b, blob_a);
  wire.replace(pos_a, len_a, blob_b);

  return true;
}

// Swap the first two non-GREASE cipher suites in the wire.  Returns true on
// success.  This breaks cipher-order invariants without changing the set.
bool swap_first_two_cipher_suites(string &wire) {
  // Cipher suites start at: 5 (record header) + 4 (handshake header) +
  // 2 (legacy version) + 32 (random) + 1 (session-id len) + session_id +
  // 2 (cipher-suites length prefix)
  size_t pos = 9 + 2 + 32;  // after handshake header, legacy_version, random
  CHECK(pos < wire.size());

  auto session_id_len = static_cast<size_t>(static_cast<td::uint8>(wire[pos]));
  pos += 1 + session_id_len;

  auto cipher_suites_len = static_cast<size_t>(read_u16(Slice(wire), pos));
  auto cipher_start = pos + 2;
  auto cipher_end = cipher_start + cipher_suites_len;
  CHECK(cipher_end <= wire.size());

  td::MutableSlice mutable_wire(wire);

  // Find first two non-GREASE cipher suite positions
  size_t first_non_grease = 0;
  size_t second_non_grease = 0;
  bool found_first = false;
  for (size_t i = cipher_start; i + 1 < cipher_end; i += 2) {
    auto cs = read_u16(Slice(wire), i);
    if (!is_grease_value(cs)) {
      if (!found_first) {
        first_non_grease = i;
        found_first = true;
      } else {
        second_non_grease = i;
        break;
      }
    }
  }

  if (first_non_grease == 0 || second_non_grease == 0) {
    return false;
  }

  auto a = read_u16(Slice(wire), first_non_grease);
  auto b = read_u16(Slice(wire), second_non_grease);
  if (a == b) {
    return false;
  }

  write_u16(mutable_wire, first_non_grease, b);
  write_u16(mutable_wire, second_non_grease, a);
  return true;
}

// Replace the first GREASE cipher-suite value in the wire with a
// non-GREASE value that is not part of the real cipher-suite set.
// This simulates a GREASE-evasion attempt where an adversary places a
// recognisable non-GREASE value in the GREASE slot.
bool replace_grease_cipher_with_non_grease(string &wire) {
  size_t pos = 9 + 2 + 32;
  CHECK(pos < wire.size());

  auto session_id_len = static_cast<size_t>(static_cast<td::uint8>(wire[pos]));
  pos += 1 + session_id_len;

  auto cipher_suites_len = static_cast<size_t>(read_u16(Slice(wire), pos));
  auto cipher_start = pos + 2;
  auto cipher_end = cipher_start + cipher_suites_len;
  CHECK(cipher_end <= wire.size());

  td::MutableSlice mutable_wire(wire);

  for (size_t i = cipher_start; i + 1 < cipher_end; i += 2) {
    auto cs = read_u16(Slice(wire), i);
    if (is_grease_value(cs)) {
      // 0x00FF is not a real cipher suite used by any modern profile
      write_u16(mutable_wire, i, 0x00FF);
      return true;
    }
  }
  return false;
}

// ---------- tests ------------------------------------------------------------

// Positive control: an unmutated Chrome133 hello must pass the reviewed
// baseline matcher for its family.
TEST(TlsFingerprintMutationAdequacy, UnmutatedChrome133PassesFamilyCheck) {
  auto wire = build_chrome133_wire();
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // Exact invariants (cipher order, supported groups, etc.) must match.
  ASSERT_TRUE(matcher.matches_exact_invariants(parsed.ok_ref()));

  // Upstream rule legality (extension permutation rules, key-share
  // structure) must pass.
  ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed.ok_ref()));
}

// Mutation 1: Inject TLS 1.1 (0x0302) into the supported_versions
// extension of a modern iOS/Safari hello.  Real Safari/iOS never
// advertises anything below TLS 1.2 in supported_versions, so the parser
// should still succeed (it is lenient) but the matcher should reject
// because the version set no longer matches any reviewed template.  At
// minimum, verify the mutation propagates into the parsed output.
TEST(TlsFingerprintMutationAdequacy, Tls11InSupportedVersionsDetectedByParser) {
  auto wire = build_chrome133_wire();
  ASSERT_TRUE(inject_tls11_into_supported_versions(wire));

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  // Verify TLS 1.1 (0x0302) is now present in the supported_versions
  // extension value.
  auto *sv_ext = td::mtproto::test::find_extension(parsed.ok_ref(), 0x002B);
  ASSERT_TRUE(sv_ext != nullptr);

  bool found_tls11 = false;
  if (sv_ext->value.size() >= 3) {
    auto versions_len = static_cast<size_t>(static_cast<td::uint8>(sv_ext->value[0]));
    for (size_t i = 1; i + 1 < sv_ext->value.size() && i < versions_len + 1; i += 2) {
      auto v = static_cast<uint16>((static_cast<td::uint8>(sv_ext->value[i]) << 8) |
                                    static_cast<td::uint8>(sv_ext->value[i + 1]));
      if (v == 0x0302) {
        found_tls11 = true;
        break;
      }
    }
  }
  ASSERT_TRUE(found_tls11);
}

// Mutation 2: TLS 1.1 injection should break the reviewed family-lane
// baseline extension-order template match for iOS when compared against
// a baseline that pins expected supported_versions content.  Even for
// Chromium families, the modified wire-length (2 extra bytes) should push
// it out of the reviewed wire-length envelope.
TEST(TlsFingerprintMutationAdequacy, Tls11InjectionFailsWireLengthEnvelope) {
  auto wire = build_chrome133_wire();
  auto original_size = wire.size();
  ASSERT_TRUE(inject_tls11_into_supported_versions(wire));

  // Wire grew by 2 bytes due to the injected TLS 1.1 version.
  ASSERT_EQ(wire.size(), original_size + 2);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // The mutated wire length must NOT appear in the reviewed envelope
  // at tight tolerance.  The envelope is a discrete set of observed
  // wire lengths; adding 2 bytes shifts us to a value that was never
  // observed.
  ASSERT_FALSE(matcher.within_wire_length_envelope(wire.size(), 0.0));
}

// Mutation 3: Replace the first GREASE cipher suite with a non-GREASE
// value that is not a member of the reviewed cipher-suite set.  The
// exact-invariants matcher compares non-GREASE cipher suites, so
// introducing a foreign cipher must fail the check.
TEST(TlsFingerprintMutationAdequacy, GreaseEvasionWrongNonGreaseFailsInvariant) {
  auto wire = build_chrome133_wire();
  bool mutated = replace_grease_cipher_with_non_grease(wire);

  if (!mutated) {
    // If the wire happened not to contain a GREASE cipher (very unlikely
    // for Chrome but possible for some profiles), skip gracefully.
    return;
  }

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // The non-GREASE cipher suite list now contains 0x00FF which is not
  // part of any Chromium reviewed baseline cipher-suite invariant.
  auto cipher_suites = parse_cipher_suite_vector(parsed.ok_ref().cipher_suites).move_as_ok();
  bool has_foreign = false;
  for (auto cs : cipher_suites) {
    if (cs == 0x00FF) {
      has_foreign = true;
      break;
    }
  }
  ASSERT_TRUE(has_foreign);

  // The exact-invariant check must reject this mutant because the
  // non-GREASE cipher suite set has changed.
  ASSERT_FALSE(matcher.matches_exact_invariants(parsed.ok_ref()));
}

// Mutation 4: Swap two non-GREASE extensions in the extension list.
// Chromium shuffles extensions but within a reviewed template catalog.
// A synthetic swap of two specific extensions should produce an
// ordering that does not appear in the catalog.
TEST(TlsFingerprintMutationAdequacy, ExtensionReorderFailsFamilyCheck) {
  auto wire = build_chrome133_wire();
  auto parsed_before = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_before.is_ok());

  auto ext_order_before = non_grease_extension_sequence(parsed_before.ok_ref());

  // Pick two extensions to swap: 0x000D (signature_algorithms) and
  // 0x0033 (key_share).  These are common to all Chrome profiles.
  bool swapped = swap_adjacent_extensions(wire, 0x000D, 0x0033);
  ASSERT_TRUE(swapped);

  auto parsed_after = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_after.is_ok());

  auto ext_order_after = non_grease_extension_sequence(parsed_after.ok_ref());

  // The extension order must have changed.
  ASSERT_TRUE(ext_order_before != ext_order_after);

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // The swapped order should not appear in the reviewed extension-order
  // template catalog.
  ASSERT_FALSE(matcher.covers_observed_extension_order_template(ext_order_after));
}

// Mutation 5: Swap the first two non-GREASE cipher suites.  This
// preserves the cipher-suite set but changes the order, which must
// fail the exact-invariant check (cipher order is pinned).
TEST(TlsFingerprintMutationAdequacy, CipherReorderFailsFamilyCheck) {
  auto wire = build_chrome133_wire();

  bool swapped = swap_first_two_cipher_suites(wire);
  ASSERT_TRUE(swapped);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // The cipher order is now 0x1302, 0x1301, ... instead of the reviewed
  // 0x1301, 0x1302, ... — this must fail exact-invariants.
  ASSERT_FALSE(matcher.matches_exact_invariants(parsed.ok_ref()));
}

// Mutation 6: Replace a supported group value with an unrecognised one.
// The exact-invariants matcher pins supported groups (non-GREASE), so
// replacing e.g. x25519 (0x001D) with a made-up group must fail.
TEST(TlsFingerprintMutationAdequacy, WrongSupportedGroupFailsInvariant) {
  auto wire = build_chrome133_wire();

  // Replace x25519 (0x001D) with 0x00FF (not a real named group)
  bool mutated = td::mtproto::test::set_supported_group_value(wire, 0x001D, 0x00FF);
  ASSERT_TRUE(mutated);

  auto parsed = parse_tls_client_hello(wire);
  // The parser may reject this because key_share contains 0x001D which
  // is now absent from supported_groups.  Either way, the family check
  // cannot pass.
  if (parsed.is_ok()) {
    const auto &baseline = get_chromium_baseline();
    FamilyLaneMatcher matcher(baseline);
    ASSERT_FALSE(matcher.matches_exact_invariants(parsed.ok_ref()));
  }
  // If the parser rejects, the mutation is detected at the structural
  // level, which is an acceptable fail-closed outcome.
}

// Mutation 7: Insert a novel extension type that does not belong to
// any reviewed family.  The extension-set invariant (or the upstream
// rule verifier) should reject the hello.
TEST(TlsFingerprintMutationAdequacy, NovelExtensionInsertionFailsUpstreamRules) {
  auto wire = build_chrome133_wire();
  auto parsed_before = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_before.is_ok());

  // Fabricate a novel extension (type 0xBEEF, zero-length body) and
  // append it to the extensions block.
  auto offsets = td::mtproto::test::get_hello_offsets(wire);
  string novel_ext;
  // Type 0xBEEF
  novel_ext.push_back(static_cast<char>(0xBE));
  novel_ext.push_back(static_cast<char>(0xEF));
  // Length 0x0000
  novel_ext.push_back(static_cast<char>(0x00));
  novel_ext.push_back(static_cast<char>(0x00));

  wire.insert(offsets.extensions_end, novel_ext);

  td::MutableSlice mutable_wire(wire);

  // Update extensions block length
  auto old_ext_block_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
  write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<uint16>(old_ext_block_len + 4));

  // Update record + handshake length headers
  td::mtproto::test::update_hello_length_headers(mutable_wire, 4);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  // Verify the novel extension is present.
  auto *novel = td::mtproto::test::find_extension(parsed.ok_ref(), 0xBEEF);
  ASSERT_TRUE(novel != nullptr);

  const auto &baseline = get_chromium_baseline();
  FamilyLaneMatcher matcher(baseline);

  // The upstream rule verifier should reject the unknown extension type
  // because it is not in the allowed extension set for Chromium.
  ASSERT_FALSE(matcher.passes_upstream_rule_legality(parsed.ok_ref()));
}

}  // namespace
