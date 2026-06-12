// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {

// Release-critical fields whose generated value a release gate must check
// against reviewed evidence. Each maps to a status field on the baseline plus
// either an ExactInvariants entry (Exact) or a SetMembershipCatalog entry
// (Catalog).
enum class ReleaseCriticalField : uint8 {
  CipherSuites,
  ExtensionSet,
  SupportedVersions,
};

// Lightweight match helpers over a single reviewed FamilyLaneBaseline. The
// matchers compose with UpstreamRuleVerifiers for upstream-legality checks
// and with the reviewed invariants table for exact-match checks.
class FamilyLaneMatcher final {
 public:
  explicit FamilyLaneMatcher(const baselines::FamilyLaneBaseline &baseline) : baseline_(baseline) {
  }

  // Matches every field in ExactInvariants that is populated. An empty
  // list on the invariants side means "no exact invariant for this
  // field" (for multi-source baselines where sources disagree) and is
  // not enforced. Fields with values must match the observed hello.
  bool matches_exact_invariants(const ParsedClientHello &hello) const;

  // Verifies one release-critical field against its reviewed evidence,
  // dispatching on the field's EvidenceFieldStatus so that a Catalog-status
  // field (empty ExactInvariants entry, multi-source disagreement) is not
  // silently skipped by matches_exact_invariants:
  //   Exact   -> the field has a non-empty exact invariant and the observed
  //              value equals it (cipher suites and supported versions are
  //              order-sensitive; the extension set is order-insensitive).
  //   Catalog -> the observed value is a member of the per-field observed
  //              catalog in SetMembershipCatalog.
  //   Policy  -> no named policy matcher is defined yet, so this fails closed.
  // Unavailable and Mixed always fail closed. Returns true only when the
  // generated hello satisfies the field's enforceable reviewed evidence.
  bool matches_release_critical_field(const ParsedClientHello &hello, ReleaseCriticalField field) const;

  // Delegates to UpstreamRuleVerifiers for the ExtensionOrder,
  // KeyShareStructure, EchPayload and AlpsType rule categories. Returns
  // true only when every category permits the observed hello.
  bool passes_upstream_rule_legality(const ParsedClientHello &hello) const;

  // Returns true iff the length has been observed in the reviewed
  // ECH-payload-length catalog for this family/lane.
  bool covers_observed_ech_payload_length(uint16 length) const;

  // Returns true iff `wire_length` is within `tolerance_percent` of any
  // observed wire-length sample for this family/lane. Used by the nightly
  // self-calibrated Monte Carlo diagnostic, not the release gate.
  bool within_wire_length_envelope(size_t wire_length, double tolerance_percent) const;

  // Release-facing wire-length similarity check expressed in BYTES rather than a
  // broad percentage. Returns true iff `wire_length` is within `max_byte_delta`
  // bytes of some observed wire-length sample for this family/lane. The bound is
  // not byte-exact because TlsHelloBuilder injects 0..255 bytes of anti-DPI
  // padding-target entropy by design; `max_byte_delta` is meant to be the sum of
  // that documented entropy budget and a small fixture-derived SNI-length delta,
  // i.e. the maximum by which a faithfully generated ClientHello of the same
  // structure can differ from a reviewed capture. Fails closed on an empty
  // catalog.
  bool within_wire_length_byte_model(size_t wire_length, size_t max_byte_delta) const;

  // Returns true iff the observed extension order appears in the
  // reviewed template catalog for this family/lane.
  bool covers_observed_extension_order_template(const vector<uint16> &observed) const;

  const baselines::FamilyLaneBaseline &baseline() const {
    return baseline_;
  }

 private:
  const baselines::FamilyLaneBaseline &baseline_;
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
