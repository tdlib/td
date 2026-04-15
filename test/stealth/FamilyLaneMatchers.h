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

  // Delegates to UpstreamRuleVerifiers for the ExtensionOrder,
  // KeyShareStructure, EchPayload and AlpsType rule categories. Returns
  // true only when every category permits the observed hello.
  bool passes_upstream_rule_legality(const ParsedClientHello &hello) const;

  // Returns true iff the length has been observed in the reviewed
  // ECH-payload-length catalog for this family/lane.
  bool covers_observed_ech_payload_length(uint16 length) const;

  // Returns true iff `wire_length` is within `tolerance_percent` of any
  // observed wire-length sample for this family/lane.
  bool within_wire_length_envelope(size_t wire_length, double tolerance_percent) const;

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
