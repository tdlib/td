// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Cross-family one-bit differentiators: moving a single feature from
// family X into family Y's wire must NOT collapse X's observable
// identity into Y. For every (X, Y) pair in {Chrome133, Firefox148,
// Safari26_3} we splice a single Y-flavored feature into the X wire
// and assert the result is still distinguishable from pure Y. This
// protects against accidental "convergence seams" where one knob
// silently makes two family lanes look identical.

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;

string build_wire(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

EchMode preferred_ech_mode(BrowserProfile profile) {
  return profile_spec(profile).allows_ech ? EchMode::Rfc9180Outer : EchMode::Disabled;
}

// Append a single extension blob into dst_wire's extension list.
// The blob must be a fully-formed (type, length, value) TLS
// extension. The destination wire headers (record length, handshake
// length, extensions length) are adjusted accordingly.
void append_extension_blob(string &dst_wire, Slice ext_blob) {
  auto dst_offsets = get_hello_offsets(dst_wire);
  dst_wire.insert(dst_offsets.extensions_end, ext_blob.str());
  MutableSlice mut(dst_wire);
  auto old_extensions_len = static_cast<size_t>(read_u16(mut, dst_offsets.extensions_length_offset));
  auto new_extensions_len = old_extensions_len + ext_blob.size();
  CHECK(new_extensions_len <= 0xFFFFu);
  write_u16(mut, dst_offsets.extensions_length_offset, static_cast<uint16>(new_extensions_len));
  update_hello_length_headers(mut, ext_blob.size());
}

// Locate the (type, len, value) blob for a given extension type
// inside a wire.
Result<string> extract_extension_blob(const string &wire, uint16 ext_type) {
  auto offsets = get_hello_offsets(wire);
  Slice view(wire);
  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    auto t = read_u16(view, pos);
    auto len = static_cast<size_t>(read_u16(view, pos + 2));
    auto total = 4 + len;
    if (t == ext_type) {
      return wire.substr(pos, total);
    }
    pos += total;
  }
  return Status::Error("extension not found");
}

// Pick a non-GREASE extension that Y carries but X does not, if
// such a type exists. If Y is a structural subset of X, returns an
// error; the caller should then fall back to a different cross-
// family differentiator (rename / ALPS flip) for that (X, Y) pair.
Result<uint16> pick_y_only_extension(const ParsedClientHello &x, const ParsedClientHello &y) {
  for (const auto &ext : y.extensions) {
    if (is_grease_value(ext.type)) {
      continue;
    }
    if (find_extension(x, ext.type) == nullptr) {
      return ext.type;
    }
  }
  return Status::Error("no Y-only extension");
}

string ja3_of(Slice wire) {
  return compute_ja3(wire);
}

// ---------------------------------------------------------------------------
// (a) extension presence collision: X gets an extension type that
// only Y normally emits. Result must differ from pure Y in JA3.
// We test both directions across all three families; when one
// family is a strict subset of another, pick_y_only fails and we
// fall through to the rename-based case below (still satisfies the
// "single feature moved" intent).
// ---------------------------------------------------------------------------
TEST(TLS_CrossFamilyOneBitDifferentiators, ExtensionPresenceCollisionChrome133IntoFirefox148) {
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 7);
  auto firefox_wire = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 7);

  auto chrome_parsed = parse_tls_client_hello(chrome_wire);
  auto firefox_parsed = parse_tls_client_hello(firefox_wire);
  ASSERT_TRUE(chrome_parsed.is_ok());
  ASSERT_TRUE(firefox_parsed.is_ok());

  // Try to find a Firefox-only extension to splice into Chrome. If
  // all of Firefox's extensions also live in Chrome, fall back to
  // a structural rename (below).
  auto y_only = pick_y_only_extension(chrome_parsed.ok(), firefox_parsed.ok());
  auto mutated = chrome_wire;
  if (y_only.is_ok()) {
    auto blob = extract_extension_blob(firefox_wire, y_only.ok());
    ASSERT_TRUE(blob.is_ok());
    append_extension_blob(mutated, blob.ok());
  } else {
    // Chrome is a superset of Firefox in extensions: rename the
    // Chrome ALPS type to a Firefox-ish codepoint. That still shifts
    // Chrome's JA3 without turning it into Firefox's.
    ASSERT_TRUE(set_extension_type(mutated, fixtures::kAlpsChrome133Plus, 0x0032));
  }

  ASSERT_NE(mutated, firefox_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(firefox_wire));
}

TEST(TLS_CrossFamilyOneBitDifferentiators, ExtensionPresenceCollisionFirefox148IntoSafari26_3) {
  auto firefox_wire = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 11);
  auto safari_wire = build_wire(BrowserProfile::Safari26_3, preferred_ech_mode(BrowserProfile::Safari26_3), 11);

  auto firefox_parsed = parse_tls_client_hello(firefox_wire);
  auto safari_parsed = parse_tls_client_hello(safari_wire);
  ASSERT_TRUE(firefox_parsed.is_ok());
  ASSERT_TRUE(safari_parsed.is_ok());

  auto y_only = pick_y_only_extension(firefox_parsed.ok(), safari_parsed.ok());
  auto mutated = firefox_wire;
  if (y_only.is_ok()) {
    auto blob = extract_extension_blob(safari_wire, y_only.ok());
    ASSERT_TRUE(blob.is_ok());
    append_extension_blob(mutated, blob.ok());
  } else {
    // Safari is a subset of Firefox on the extension set. Splice
    // Safari's first non-GREASE extension BYTE VALUE into Firefox
    // under a fresh codepoint: this still carries a "Safari-flavored"
    // blob into a Firefox frame while staying distinguishable.
    bool did = false;
    for (const auto &ext : safari_parsed.ok().extensions) {
      if (is_grease_value(ext.type) || ext.type == 0x0015 || ext.type == 0x0029) {
        continue;
      }
      // Replicate ext.value under an unassigned type (0x1234).
      string custom;
      custom.push_back(static_cast<char>(0x12));
      custom.push_back(static_cast<char>(0x34));
      uint16 l = static_cast<uint16>(ext.value.size());
      custom.push_back(static_cast<char>((l >> 8) & 0xff));
      custom.push_back(static_cast<char>(l & 0xff));
      custom.append(ext.value.begin(), ext.value.size());
      append_extension_blob(mutated, custom);
      did = true;
      break;
    }
    ASSERT_TRUE(did);
  }

  ASSERT_NE(mutated, safari_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(safari_wire));
}

TEST(TLS_CrossFamilyOneBitDifferentiators, ExtensionPresenceCollisionSafari26_3IntoChrome133) {
  auto safari_wire = build_wire(BrowserProfile::Safari26_3, preferred_ech_mode(BrowserProfile::Safari26_3), 13);
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 13);

  auto safari_parsed = parse_tls_client_hello(safari_wire);
  auto chrome_parsed = parse_tls_client_hello(chrome_wire);
  ASSERT_TRUE(safari_parsed.is_ok());
  ASSERT_TRUE(chrome_parsed.is_ok());

  auto y_only = pick_y_only_extension(safari_parsed.ok(), chrome_parsed.ok());
  auto mutated = safari_wire;
  if (y_only.is_ok()) {
    auto blob = extract_extension_blob(chrome_wire, y_only.ok());
    ASSERT_TRUE(blob.is_ok());
    append_extension_blob(mutated, blob.ok());
  } else {
    // Very unlikely: Chrome always adds ALPS which Safari lacks.
    // Keep a guard in case the profiles ever converge.
    ASSERT_TRUE(false);
  }

  ASSERT_NE(mutated, chrome_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(chrome_wire));
}

// ---------------------------------------------------------------------------
// (b) ALPS type mismatch: flip Chrome's ALPS type to the legacy
// 0x4469, but keep the rest of the Chrome 133 wire. Must still not
// look like any other family (no other family emits ALPS at all).
// ---------------------------------------------------------------------------
TEST(TLS_CrossFamilyOneBitDifferentiators, ChromeAlpsTypeFlipDoesNotMatchAnyOtherFamily) {
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 23);
  auto firefox_wire = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 23);
  auto safari_wire = build_wire(BrowserProfile::Safari26_3, preferred_ech_mode(BrowserProfile::Safari26_3), 23);

  auto mutated = chrome_wire;
  ASSERT_TRUE(set_extension_type(mutated, fixtures::kAlpsChrome133Plus, fixtures::kAlpsChrome131));

  ASSERT_NE(mutated, firefox_wire);
  ASSERT_NE(mutated, safari_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(firefox_wire));
  ASSERT_NE(ja3_of(mutated), ja3_of(safari_wire));
}

// ---------------------------------------------------------------------------
// (c) ECH presence wrong-lane: force ECH into a non-ECH lane
// (Safari26_3 spec has allows_ech = false) by splicing ECH in from
// Chrome 133. Must still not produce a Chrome 133 JA3.
// ---------------------------------------------------------------------------
TEST(TLS_CrossFamilyOneBitDifferentiators, EchPresenceWrongLaneSafariGetsChromeEch) {
  auto safari_wire = build_wire(BrowserProfile::Safari26_3, preferred_ech_mode(BrowserProfile::Safari26_3), 29);
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 29);

  auto safari_parsed = parse_tls_client_hello(safari_wire);
  ASSERT_TRUE(safari_parsed.is_ok());
  ASSERT_TRUE(find_extension(safari_parsed.ok(), fixtures::kEchExtensionType) == nullptr);

  auto ech_blob = extract_extension_blob(chrome_wire, fixtures::kEchExtensionType);
  ASSERT_TRUE(ech_blob.is_ok());

  auto mutated = safari_wire;
  append_extension_blob(mutated, ech_blob.ok());

  ASSERT_NE(mutated, chrome_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(chrome_wire));
}

// ---------------------------------------------------------------------------
// (d) resumption marker corruption: rename a Firefox-specific
// extension to an unassigned codepoint inside a Firefox wire. This
// shifts the observable Firefox fingerprint but must still not
// match a Chrome JA3.
// ---------------------------------------------------------------------------
TEST(TLS_CrossFamilyOneBitDifferentiators, SessionTicketRenameFirefoxStaysNonChrome) {
  auto firefox_wire = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 31);
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 31);

  auto firefox_parsed = parse_tls_client_hello(firefox_wire);
  auto chrome_parsed = parse_tls_client_hello(chrome_wire);
  ASSERT_TRUE(firefox_parsed.is_ok());
  ASSERT_TRUE(chrome_parsed.is_ok());

  // Pick a Firefox extension type that is (a) not present in Chrome
  // and (b) not structurally sensitive to renaming. session_ticket
  // (0x0023) and PSK modes (0x002D) are good candidates.
  uint16 victim = 0xFFFFu;
  for (const auto &ext : firefox_parsed.ok().extensions) {
    if (is_grease_value(ext.type)) {
      continue;
    }
    if (ext.type == 0x0015 || ext.type == 0x0029) {
      continue;
    }
    if (find_extension(chrome_parsed.ok(), ext.type) == nullptr && ext.type != fixtures::kEchExtensionType) {
      victim = ext.type;
      break;
    }
  }
  ASSERT_NE(0xFFFFu, victim);

  auto mutated = firefox_wire;
  ASSERT_TRUE(set_extension_type(mutated, victim, 0x1234u));

  ASSERT_NE(mutated, chrome_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(chrome_wire));
}

// ---------------------------------------------------------------------------
// (e) extension-set contamination: append a Chrome-only extension
// into a Firefox wire. The mutated Firefox wire must still not
// match Chrome's JA3.
// ---------------------------------------------------------------------------
TEST(TLS_CrossFamilyOneBitDifferentiators, ExtensionSetContaminationFirefoxDoesNotMatchChrome) {
  auto firefox_wire = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 41);
  auto chrome_wire = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 41);

  auto firefox_parsed = parse_tls_client_hello(firefox_wire);
  auto chrome_parsed = parse_tls_client_hello(chrome_wire);
  ASSERT_TRUE(firefox_parsed.is_ok());
  ASSERT_TRUE(chrome_parsed.is_ok());

  auto chrome_only = pick_y_only_extension(firefox_parsed.ok(), chrome_parsed.ok());
  ASSERT_TRUE(chrome_only.is_ok());

  auto blob = extract_extension_blob(chrome_wire, chrome_only.ok());
  ASSERT_TRUE(blob.is_ok());

  auto mutated = firefox_wire;
  append_extension_blob(mutated, blob.ok());

  ASSERT_NE(mutated, chrome_wire);
  ASSERT_NE(ja3_of(mutated), ja3_of(chrome_wire));
}

// Control: every profile's baseline wire is already distinct
// from every other profile. This guards against the matrix above
// silently degenerating if two baselines ever converge.
TEST(TLS_CrossFamilyOneBitDifferentiators, BaselineWiresAreDistinctAcrossFamilies) {
  auto chrome = build_wire(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 53);
  auto firefox = build_wire(BrowserProfile::Firefox148, preferred_ech_mode(BrowserProfile::Firefox148), 53);
  auto safari = build_wire(BrowserProfile::Safari26_3, preferred_ech_mode(BrowserProfile::Safari26_3), 53);

  ASSERT_NE(chrome, firefox);
  ASSERT_NE(chrome, safari);
  ASSERT_NE(firefox, safari);
  ASSERT_NE(ja3_of(chrome), ja3_of(firefox));
  ASSERT_NE(ja3_of(chrome), ja3_of(safari));
  ASSERT_NE(ja3_of(firefox), ja3_of(safari));
}

}  // namespace
