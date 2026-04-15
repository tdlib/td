// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FamilyLaneMatchers.h"

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/UpstreamRuleVerifiers.h"

#include <algorithm>
#include <cmath>

namespace td {
namespace mtproto {
namespace test {

namespace {

vector<uint16> non_grease_cipher_suites_ordered(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  vector<uint16> out;
  out.reserve(cipher_suites.size());
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      out.push_back(cs);
    }
  }
  return out;
}

vector<uint16> non_grease_extension_types_without_padding(const ParsedClientHello &hello) {
  vector<uint16> out;
  out.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015u) {
      out.push_back(ext.type);
    }
  }
  return out;
}

vector<uint16> non_grease_supported_groups(const ParsedClientHello &hello) {
  vector<uint16> out;
  out.reserve(hello.supported_groups.size());
  for (auto g : hello.supported_groups) {
    if (!is_grease_value(g)) {
      out.push_back(g);
    }
  }
  return out;
}

vector<uint16> compress_cert_algorithms(const ParsedClientHello &hello) {
  vector<uint16> out;
  for (const auto &ext : hello.extensions) {
    if (ext.type != 0x001Bu) {
      continue;
    }
    // body = [list_len_byte=N] [algo1 (2B)] ... [algoK (2B)]
    if (ext.value.empty()) {
      return out;
    }
    const auto *bytes = reinterpret_cast<const uint8 *>(ext.value.data());
    size_t body_len = ext.value.size();
    if (body_len < 1) {
      return out;
    }
    for (size_t i = 1; i + 1 < body_len; i += 2) {
      uint16 alg = static_cast<uint16>((static_cast<uint16>(bytes[i]) << 8) | bytes[i + 1]);
      out.push_back(alg);
    }
    return out;
  }
  return out;
}

vector<string> alpn_protocols(const ParsedClientHello &hello) {
  vector<string> out;
  for (const auto &ext : hello.extensions) {
    if (ext.type != 0x0010u) {
      continue;
    }
    if (ext.value.size() < 2) {
      return out;
    }
    const auto *bytes = reinterpret_cast<const uint8 *>(ext.value.data());
    size_t list_len = (static_cast<size_t>(bytes[0]) << 8) | static_cast<size_t>(bytes[1]);
    size_t offset = 2;
    while (offset < 2 + list_len && offset < ext.value.size()) {
      uint8 name_len = bytes[offset];
      offset += 1;
      if (offset + name_len > ext.value.size()) {
        break;
      }
      out.emplace_back(reinterpret_cast<const char *>(bytes + offset), name_len);
      offset += name_len;
    }
    return out;
  }
  return out;
}

bool hello_has_ech_extension(const ParsedClientHello &hello) {
  return find_extension(hello, 0xFE0Du) != nullptr;
}

}  // namespace

bool FamilyLaneMatcher::matches_exact_invariants(const ParsedClientHello &hello) const {
  const auto &inv = baseline_.invariants;

  if (!inv.non_grease_cipher_suites_ordered.empty()) {
    if (non_grease_cipher_suites_ordered(hello) != inv.non_grease_cipher_suites_ordered) {
      return false;
    }
  }
  if (!inv.non_grease_extension_set.empty()) {
    // extension_set: compared as an unordered set because chromium lanes
    // shuffle extension order per-connection.
    auto observed = non_grease_extension_types_without_padding(hello);
    std::sort(observed.begin(), observed.end());
    auto expected = inv.non_grease_extension_set;
    std::sort(expected.begin(), expected.end());
    if (observed != expected) {
      return false;
    }
  }
  if (!inv.non_grease_supported_groups.empty()) {
    if (non_grease_supported_groups(hello) != inv.non_grease_supported_groups) {
      return false;
    }
  }
  if (!inv.alpn_protocols.empty()) {
    if (alpn_protocols(hello) != inv.alpn_protocols) {
      return false;
    }
  }
  if (!inv.compress_cert_algorithms.empty()) {
    if (compress_cert_algorithms(hello) != inv.compress_cert_algorithms) {
      return false;
    }
  }
  if (inv.ech_presence_required && !hello_has_ech_extension(hello)) {
    return false;
  }
  if (inv.tls_record_version != 0 && hello.record_legacy_version != inv.tls_record_version) {
    return false;
  }
  if (inv.client_hello_legacy_version != 0 &&
      hello.client_legacy_version != inv.client_hello_legacy_version) {
    return false;
  }
  return true;
}

bool FamilyLaneMatcher::passes_upstream_rule_legality(const ParsedClientHello &hello) const {
  Slice family_id = baseline_.family_id;

  const auto &order_verifier = verifiers::ExtensionOrderVerifier::get_for_family(family_id);
  if (!order_verifier.is_legal_permutation(non_grease_extension_types_without_padding(hello))) {
    LOG(DEBUG) << "FamilyLaneMatcher rejected by ExtensionOrderVerifier";
    return false;
  }

  const auto &keyshare_verifier = verifiers::KeyShareStructureVerifier::get_for_family(family_id);
  if (!keyshare_verifier.is_legal_structure(hello.key_share_entries)) {
    LOG(DEBUG) << "FamilyLaneMatcher rejected by KeyShareStructureVerifier";
    return false;
  }

  const auto &ech_verifier = verifiers::EchPayloadVerifier::get_for_family(family_id);
  if (hello_has_ech_extension(hello)) {
    if (!ech_verifier.family_advertises_ech()) {
      LOG(DEBUG) << "FamilyLaneMatcher rejected: family doesn't advertise ECH";
      return false;
    }
    if (!ech_verifier.is_legal_ech_payload_length(hello.ech_payload_length)) {
      LOG(DEBUG) << "FamilyLaneMatcher rejected by ECH payload length " << hello.ech_payload_length;
      return false;
    }
    if (!ech_verifier.is_legal_ech_aead_kdf_pair(hello.ech_kdf_id, hello.ech_aead_id)) {
      LOG(DEBUG) << "FamilyLaneMatcher rejected by ECH kdf/aead pair";
      return false;
    }
  }

  const auto &alps_verifier = verifiers::AlpsTypeVerifier::get_for_family(family_id);
  for (const auto &ext : hello.extensions) {
    if (ext.type == 0x4469u || ext.type == 0x44CDu) {
      if (!alps_verifier.family_advertises_alps()) {
        LOG(DEBUG) << "FamilyLaneMatcher rejected: family doesn't advertise ALPS";
        return false;
      }
      if (!alps_verifier.is_legal_alps_type(ext.type, Slice())) {
        LOG(DEBUG) << "FamilyLaneMatcher rejected by ALPS type";
        return false;
      }
    }
  }
  return true;
}

bool FamilyLaneMatcher::covers_observed_ech_payload_length(uint16 length) const {
  const auto &catalog = baseline_.set_catalog.observed_ech_payload_lengths;
  return std::find(catalog.begin(), catalog.end(), length) != catalog.end();
}

bool FamilyLaneMatcher::within_wire_length_envelope(size_t wire_length, double tolerance_percent) const {
  const auto &observed = baseline_.set_catalog.observed_wire_lengths;
  if (observed.empty()) {
    return false;
  }
  double tol = tolerance_percent / 100.0;
  for (auto sample : observed) {
    double diff = std::fabs(static_cast<double>(wire_length) - static_cast<double>(sample));
    double allowed = std::fabs(static_cast<double>(sample)) * tol;
    if (diff <= allowed) {
      return true;
    }
  }
  return false;
}

bool FamilyLaneMatcher::covers_observed_extension_order_template(const vector<uint16> &observed) const {
  const auto &templates = baseline_.set_catalog.observed_extension_order_templates;
  for (const auto &templ : templates) {
    if (templ == observed) {
      return true;
    }
  }
  return false;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
