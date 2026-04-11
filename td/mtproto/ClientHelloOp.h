// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/Slice.h"
#include "td/utils/common.h"

namespace td {
namespace mtproto {

struct ClientHelloOp {
  enum class Type {
    Bytes,
    RandomBytes,
    ZeroBytes,
    Domain,
    GreaseValue,
    X25519KeyShareEntry,
    Secp256r1KeyShareEntry,
    X25519MlKem768KeyShareEntry,
    GreaseKeyShareEntry,
    X25519PublicKey,
    Scope16Begin,
    Scope16End,
    Permutation,
    PaddingToTarget,

    LegacyVersionFromProfile,
    CipherSuitesFromProfile,
    ExtensionsFromProfile,
    ExtensionsPermutationFromProfile,
  };

  Type type{Type::Bytes};
  int length{0};
  int value{0};
  string data;
  vector<vector<ClientHelloOp>> permutation_parts;

  static ClientHelloOp bytes(Slice data) {
    ClientHelloOp op;
    op.type = Type::Bytes;
    op.data = data.str();
    return op;
  }

  static ClientHelloOp random_bytes(int length) {
    ClientHelloOp op;
    op.type = Type::RandomBytes;
    op.length = length;
    return op;
  }

  static ClientHelloOp zero_bytes(int length) {
    ClientHelloOp op;
    op.type = Type::ZeroBytes;
    op.length = length;
    return op;
  }

  static ClientHelloOp domain() {
    ClientHelloOp op;
    op.type = Type::Domain;
    return op;
  }

  static ClientHelloOp grease(int index) {
    ClientHelloOp op;
    op.type = Type::GreaseValue;
    op.value = index;
    return op;
  }

  static ClientHelloOp x25519_key_share_entry() {
    ClientHelloOp op;
    op.type = Type::X25519KeyShareEntry;
    return op;
  }

  static ClientHelloOp secp256r1_key_share_entry() {
    ClientHelloOp op;
    op.type = Type::Secp256r1KeyShareEntry;
    return op;
  }

  static ClientHelloOp x25519_ml_kem_768_key_share_entry() {
    ClientHelloOp op;
    op.type = Type::X25519MlKem768KeyShareEntry;
    return op;
  }

  // Emits a GREASE key_share entry. Wire format:
  //   group(2 bytes)  - GREASE pair (e.g. 0x4A4A, 0xDADA, 0x8A8A, ...)
  //                    sourced from the executor GREASE value pool slot
  //                    `grease_index_value`
  //   length(2 bytes) - 0x0001
  //   body(1 byte)    - 0x00
  //
  // Real Chrome / Safari / iOS captures all carry exactly this 5-byte
  // shape as the FIRST entry in the key_share extension. Any profile
  // that imitates a Chromium-family or Apple-TLS browser MUST include
  // it; without it, the wire image is detectable by both strict
  // BoringSSL parsers and DPI middleboxes that fingerprint key_share
  // entry counts.
  static ClientHelloOp grease_key_share_entry(uint8 grease_index_value) {
    ClientHelloOp op;
    op.type = Type::GreaseKeyShareEntry;
    op.value = grease_index_value;
    return op;
  }

  // Emits exactly 32 bytes of a valid X25519 public key (rejection-sampled
  // through the same Curve25519 quadratic-residue check used by
  // `x25519_key_share_entry`). Used by extensions that carry a serialized
  // X25519 point WITHOUT the 4-byte (group, length) prefix that
  // `x25519_key_share_entry` prepends — currently the ECH `enc` field.
  //
  // Replacing this op with `random_bytes(32)` produces wire-images that fail
  // strict X25519 coordinate validation on Cloudflare/Google ECH-aware
  // servers and on DPI middleboxes that ship libsodium / OpenSSL X25519
  // validators. The behaviour is enforced by
  // `test/stealth/test_ech_encapsulated_key_validity_invariants.cpp`.
  static ClientHelloOp x25519_public_key() {
    ClientHelloOp op;
    op.type = Type::X25519PublicKey;
    return op;
  }

  static ClientHelloOp scope16_begin() {
    ClientHelloOp op;
    op.type = Type::Scope16Begin;
    return op;
  }

  static ClientHelloOp scope16_end() {
    ClientHelloOp op;
    op.type = Type::Scope16End;
    return op;
  }

  static ClientHelloOp permutation(vector<vector<ClientHelloOp>> parts) {
    ClientHelloOp op;
    op.type = Type::Permutation;
    op.permutation_parts = std::move(parts);
    return op;
  }

  static ClientHelloOp padding_to_target(int target_size) {
    ClientHelloOp op;
    op.type = Type::PaddingToTarget;
    op.value = target_size;
    return op;
  }

  static ClientHelloOp legacy_version_from_profile() {
    ClientHelloOp op;
    op.type = Type::LegacyVersionFromProfile;
    return op;
  }

  static ClientHelloOp cipher_suites_from_profile() {
    ClientHelloOp op;
    op.type = Type::CipherSuitesFromProfile;
    return op;
  }

  static ClientHelloOp extensions_from_profile() {
    ClientHelloOp op;
    op.type = Type::ExtensionsFromProfile;
    return op;
  }

  static ClientHelloOp extensions_permutation_from_profile() {
    ClientHelloOp op;
    op.type = Type::ExtensionsPermutationFromProfile;
    return op;
  }
};

}  // namespace mtproto
}  // namespace td
