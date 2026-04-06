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
