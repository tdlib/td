// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {
namespace verifiers {

// Verifies that an observed non-GREASE extension-order sequence is a legal
// permutation under the upstream rules of a given family. Chromium and
// Apple-TLS families permute every extension except the two pinned
// positions (SNI 0x0000 for Chromium, ALPN 0x0010 for Chromium) while
// Firefox emits a fixed order that must match exactly.
class ExtensionOrderVerifier final {
 public:
  static const ExtensionOrderVerifier &get_for_family(Slice family_id);

  bool is_legal_permutation(const vector<uint16> &non_grease_extensions) const;

  // Allow tests to introspect the underlying pinned-position constraints.
  Slice family_id() const {
    return family_id_;
  }

 private:
  enum class Mode { Permutation, Fixed, NoConstraint };

  ExtensionOrderVerifier(Slice family_id, Mode mode, vector<uint16> allowed_extensions,
                         vector<uint16> fixed_order)
      : family_id_(family_id)
      , mode_(mode)
      , allowed_extensions_(std::move(allowed_extensions))
      , fixed_order_(std::move(fixed_order)) {
  }

  Slice family_id_;
  Mode mode_;
  vector<uint16> allowed_extensions_;
  vector<uint16> fixed_order_;
};

// Verifies that the observed non-GREASE key-share entries match a legal
// (group, length) tuple set and that the PQ group comes first when the
// family's upstream rules pin the PQ group to the front.
class KeyShareStructureVerifier final {
 public:
  static const KeyShareStructureVerifier &get_for_family(Slice family_id);

  bool is_legal_structure(const vector<ParsedKeyShareEntry> &key_share_entries) const;

  Slice family_id() const {
    return family_id_;
  }

 private:
  struct LegalEntry {
    uint16 group;
    uint16 length;
  };

  KeyShareStructureVerifier(Slice family_id, vector<LegalEntry> legal_entries, bool pq_group_first)
      : family_id_(family_id)
      , legal_entries_(std::move(legal_entries))
      , pq_group_first_(pq_group_first) {
  }

  Slice family_id_;
  vector<LegalEntry> legal_entries_;
  bool pq_group_first_;
};

// Verifies that an ECH payload length sits in the family's legal bucket set
// and that the advertised (kdf_id, aead_id) pair is one the family is
// allowed to emit.
class EchPayloadVerifier final {
 public:
  static const EchPayloadVerifier &get_for_family(Slice family_id);

  bool is_legal_ech_payload_length(uint16 payload_bytes) const;
  bool is_legal_ech_aead_kdf_pair(uint16 kdf_id, uint16 aead_id) const;
  bool family_advertises_ech() const {
    return advertises_;
  }

  Slice family_id() const {
    return family_id_;
  }

 private:
  EchPayloadVerifier(Slice family_id, bool advertises, vector<uint16> payload_buckets,
                     vector<uint16> aead_ids, vector<uint16> kdf_ids)
      : family_id_(family_id)
      , advertises_(advertises)
      , payload_buckets_(std::move(payload_buckets))
      , aead_ids_(std::move(aead_ids))
      , kdf_ids_(std::move(kdf_ids)) {
  }

  Slice family_id_;
  bool advertises_;
  vector<uint16> payload_buckets_;
  vector<uint16> aead_ids_;
  vector<uint16> kdf_ids_;
};

// Verifies that an advertised ALPS extension type is one the family's
// upstream rules permit for the given profile version band.
class AlpsTypeVerifier final {
 public:
  static const AlpsTypeVerifier &get_for_family(Slice family_id);

  bool is_legal_alps_type(uint16 alps_type, Slice family_version) const;
  bool family_advertises_alps() const {
    return !legal_types_.empty();
  }

  Slice family_id() const {
    return family_id_;
  }

 private:
  AlpsTypeVerifier(Slice family_id, vector<uint16> legal_types)
      : family_id_(family_id), legal_types_(std::move(legal_types)) {
  }

  Slice family_id_;
  vector<uint16> legal_types_;
};

}  // namespace verifiers
}  // namespace test
}  // namespace mtproto
}  // namespace td
