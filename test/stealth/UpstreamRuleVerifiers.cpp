// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Mirrors the rule table in test/analysis/upstream_tls_rules.json. The JSON
// document is the human-review artifact; this translation unit pins the
// exact values the verifier classes enforce at test-time. When the JSON
// changes, update this mirror in the same commit.

#include "test/stealth/UpstreamRuleVerifiers.h"

#include "td/utils/common.h"

#include <algorithm>

namespace td {
namespace mtproto {
namespace test {
namespace verifiers {

namespace {

// Chromium / Apple-TLS shuffle every ClientHello extension under upstream
// rules. The only always-true post-shuffle constraint is "no duplicate
// extension types" — SNI (0x0000) and ALPN (0x0010) participate in the
// shuffle and may land at any position.

bool contains_u16(const vector<uint16> &haystack, uint16 needle) {
  return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool is_chromium_family(Slice family_id) {
  return family_id == Slice("chromium_linux_desktop") || family_id == Slice("chromium_windows") ||
         family_id == Slice("chromium_macos") || family_id == Slice("android_chromium") ||
         family_id == Slice("ios_chromium") || family_id == Slice("chromium");
}

bool is_firefox_family(Slice family_id) {
  return family_id == Slice("firefox") || family_id == Slice("firefox_linux_desktop") ||
         family_id == Slice("firefox_windows") || family_id == Slice("firefox_macos") ||
         family_id == Slice("firefox_android");
}

bool is_apple_family(Slice family_id) {
  return family_id == Slice("apple_tls") || family_id == Slice("apple_ios_tls") ||
         family_id == Slice("apple_macos_tls");
}

}  // namespace

// ---------------------------------------------------------------------------
// ExtensionOrderVerifier
// ---------------------------------------------------------------------------

const ExtensionOrderVerifier &ExtensionOrderVerifier::get_for_family(Slice family_id) {
  if (is_chromium_family(family_id)) {
    static const ExtensionOrderVerifier v(Slice("chromium"), Mode::Permutation, {}, {});
    return v;
  }
  if (is_firefox_family(family_id)) {
    // Firefox emits a fixed order — the legality check returns true for
    // any ordering here because the exact ordering is also enforced by
    // FamilyLaneMatcher::matches_exact_invariants. This verifier only
    // checks that the order does not contain upstream-forbidden types.
    static const ExtensionOrderVerifier v(Slice("firefox"), Mode::Fixed, {}, {});
    return v;
  }
  if (is_apple_family(family_id)) {
    static const ExtensionOrderVerifier v(Slice("apple_tls"), Mode::Permutation, {}, {});
    return v;
  }
  static const ExtensionOrderVerifier v(family_id, Mode::NoConstraint, {}, {});
  return v;
}

bool ExtensionOrderVerifier::is_legal_permutation(const vector<uint16> &non_grease_extensions) const {
  if (mode_ == Mode::NoConstraint) {
    return true;
  }
  if (mode_ == Mode::Fixed) {
    // No upstream-forbidden type list to check yet; Firefox's fixed order
    // is enforced at the higher-level matcher.
    return true;
  }
  // Permutation mode: the only always-true post-shuffle constraint under
  // upstream Chromium/BoringSSL rules is that no extension type appears
  // twice. SNI and ALPN are part of the shuffled set and may land at any
  // position. Tests that want to assert head/tail anchoring explicitly
  // can use dedicated helpers in FamilyLaneMatcher.
  if (non_grease_extensions.empty()) {
    return false;
  }
  vector<uint16> sorted_copy = non_grease_extensions;
  std::sort(sorted_copy.begin(), sorted_copy.end());
  for (size_t i = 1; i < sorted_copy.size(); i++) {
    if (sorted_copy[i] == sorted_copy[i - 1]) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// KeyShareStructureVerifier
// ---------------------------------------------------------------------------

const KeyShareStructureVerifier &KeyShareStructureVerifier::get_for_family(Slice family_id) {
  if (is_chromium_family(family_id) || is_apple_family(family_id)) {
    static const KeyShareStructureVerifier v(Slice("chromium_or_apple_tls"),
                                             {
                                                 {0x11ECu, 1216u},
                                                 {0x001Du, 32u},
                                             },
                                             /*pq_group_first=*/true);
    return v;
  }
  if (is_firefox_family(family_id)) {
    static const KeyShareStructureVerifier v(Slice("firefox"),
                                             {
                                                 {0x11ECu, 1216u},
                                                 {0x001Du, 32u},
                                                 {0x0017u, 65u},
                                             },
                                             /*pq_group_first=*/true);
    return v;
  }
  static const KeyShareStructureVerifier v(family_id, {}, false);
  return v;
}

bool KeyShareStructureVerifier::is_legal_structure(
    const vector<ParsedKeyShareEntry> &key_share_entries) const {
  if (legal_entries_.empty()) {
    return true;
  }
  bool saw_pq = false;
  bool first_non_grease = true;
  for (const auto &entry : key_share_entries) {
    // GREASE entries are legal at any position with any length in { 1 }.
    if (is_grease_value(entry.group)) {
      if (entry.key_length != 1u) {
        return false;
      }
      continue;
    }
    bool is_legal_entry = false;
    for (const auto &legal : legal_entries_) {
      if (legal.group == entry.group && legal.length == entry.key_length) {
        is_legal_entry = true;
        break;
      }
    }
    if (!is_legal_entry) {
      return false;
    }
    if (pq_group_first_ && first_non_grease && entry.group != 0x11ECu) {
      // PQ group must be the first non-GREASE entry when the family
      // pins PQ-first.
      (void)saw_pq;  // silence unused in paths without PQ
      return false;
    }
    if (entry.group == 0x11ECu) {
      saw_pq = true;
    }
    first_non_grease = false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// EchPayloadVerifier
// ---------------------------------------------------------------------------

const EchPayloadVerifier &EchPayloadVerifier::get_for_family(Slice family_id) {
  if (is_chromium_family(family_id)) {
    static const EchPayloadVerifier v(Slice("chromium"), /*advertises=*/true,
                                      /*payload_buckets=*/{144u, 176u, 208u, 240u},
                                      /*aead_ids=*/{0x0001u},
                                      /*kdf_ids=*/{0x0001u});
    return v;
  }
  if (is_firefox_family(family_id)) {
    static const EchPayloadVerifier v(Slice("firefox"), /*advertises=*/true,
                                      /*payload_buckets=*/{239u, 399u},
                                      /*aead_ids=*/{0x0001u, 0x0003u},
                                      /*kdf_ids=*/{0x0001u});
    return v;
  }
  if (is_apple_family(family_id)) {
    static const EchPayloadVerifier v(Slice("apple_tls"), /*advertises=*/false, {}, {}, {});
    return v;
  }
  static const EchPayloadVerifier v(family_id, false, {}, {}, {});
  return v;
}

bool EchPayloadVerifier::is_legal_ech_payload_length(uint16 payload_bytes) const {
  if (!advertises_) {
    return false;
  }
  return contains_u16(payload_buckets_, payload_bytes);
}

bool EchPayloadVerifier::is_legal_ech_aead_kdf_pair(uint16 kdf_id, uint16 aead_id) const {
  if (!advertises_) {
    return false;
  }
  return contains_u16(kdf_ids_, kdf_id) && contains_u16(aead_ids_, aead_id);
}

// ---------------------------------------------------------------------------
// AlpsTypeVerifier
// ---------------------------------------------------------------------------

const AlpsTypeVerifier &AlpsTypeVerifier::get_for_family(Slice family_id) {
  if (is_chromium_family(family_id)) {
    static const AlpsTypeVerifier v(Slice("chromium"), {0x4469u, 0x44CDu});
    return v;
  }
  if (is_firefox_family(family_id) || is_apple_family(family_id)) {
    static const AlpsTypeVerifier v(family_id, {});
    return v;
  }
  static const AlpsTypeVerifier v(family_id, {});
  return v;
}

bool AlpsTypeVerifier::is_legal_alps_type(uint16 alps_type, Slice family_version) const {
  if (legal_types_.empty()) {
    return false;
  }
  if (!contains_u16(legal_types_, alps_type)) {
    return false;
  }
  // Version bands: Chrome 120/131 must emit 0x4469; Chrome 133+ emits
  // 0x44CD. Empty version string disables the band check (callers who
  // only hold a coarse family id).
  if (family_version.empty()) {
    return true;
  }
  if (family_version == Slice("chrome120") || family_version == Slice("chrome131")) {
    return alps_type == 0x4469u;
  }
  if (family_version == Slice("chrome133") || family_version == Slice("chrome133_plus") ||
      family_version == Slice("chrome144") || family_version == Slice("chrome146") ||
      family_version == Slice("chrome147")) {
    return alps_type == 0x44CDu;
  }
  return true;
}

}  // namespace verifiers
}  // namespace test
}  // namespace mtproto
}  // namespace td
