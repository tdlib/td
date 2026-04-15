//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/RsaKeyVault.h"

#include "td/mtproto/PacketAlignmentSeeds.h"
#include "td/mtproto/ProtocolFingerprintTable.h"

#include "td/net/SessionTicketSeeds.h"

#include "td/telegram/net/ConfigCacheSeeds.h"

#include "td/telegram/StaticCatalog.h"

#include "td/utils/EntropyMixTable.h"
#include "td/utils/HashIndexSeeds.h"

#include "td/utils/crypto.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/UInt.h"

namespace td {
namespace mtproto {
namespace {

template <size_t N>
Slice byte_slice(const unsigned char (&bytes)[N]) {
  return Slice(reinterpret_cast<const char *>(bytes), N);
}

template <size_t N>
void append_bytes(string &target, const unsigned char (&bytes)[N]) {
  target.append(reinterpret_cast<const char *>(bytes), N);
}

void secure_zero(MutableSlice data) {
  volatile unsigned char *ptr = reinterpret_cast<volatile unsigned char *>(data.ubegin());
  for (size_t i = 0; i < data.size(); i++) {
    ptr[i] = 0;
  }
}

uint8 role_byte(VaultKeyRole role) {
  return static_cast<uint8>(role);
}

Slice shard_a(VaultKeyRole role) {
  switch (role) {
    case VaultKeyRole::MainMtproto:
      return byte_slice(vault_detail::kProtocolFingerprintTableMainMtproto);
    case VaultKeyRole::TestMtproto:
      return byte_slice(vault_detail::kProtocolFingerprintTableTestMtproto);
    case VaultKeyRole::SimpleConfig:
      return byte_slice(vault_detail::kProtocolFingerprintTableSimpleConfig);
    default:
      return Slice();
  }
}

Slice shard_b(VaultKeyRole role) {
  switch (role) {
    case VaultKeyRole::MainMtproto:
      return byte_slice(vault_detail::kEntropyMixTableMainMtproto);
    case VaultKeyRole::TestMtproto:
      return byte_slice(vault_detail::kEntropyMixTableTestMtproto);
    case VaultKeyRole::SimpleConfig:
      return byte_slice(vault_detail::kEntropyMixTableSimpleConfig);
    default:
      return Slice();
  }
}

struct DerivedKeys final {
  UInt256 aes_key;
  UInt256 mac_key;
};

DerivedKeys derive_keys() {
  string key_material;
  key_material.reserve(128);
  append_bytes(key_material, vault_detail::kHashIndexSeeds);
  append_bytes(key_material, vault_detail::kSessionTicketSeeds);
  append_bytes(key_material, vault_detail::kPacketAlignmentSeeds);
  append_bytes(key_material, vault_detail::kConfigCacheSeeds);

  DerivedKeys result;
  hmac_sha256(Slice(key_material), Slice("rsa_vault_v1_key"), as_mutable_slice(result.aes_key));
  hmac_sha256(Slice(key_material), Slice("rsa_vault_v1_mac"), as_mutable_slice(result.mac_key));
  secure_zero(MutableSlice(key_material));
  return result;
}

Result<string> reassemble_blob(VaultKeyRole role) {
  auto left = shard_a(role);
  auto right = shard_b(role);
  if (left.empty() || right.empty()) {
    return Status::Error("Unknown vault key role");
  }
  if (left.size() != right.size()) {
    return Status::Error("Shard size mismatch");
  }

  string blob(left.size(), '\0');
  for (size_t i = 0; i < left.size(); i++) {
    blob[i] = static_cast<char>(static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]));
  }
  return blob;
}

Result<string> remove_pkcs7_padding(string plaintext);

Result<string> reassemble_and_decrypt_impl(VaultKeyRole role) {
  TRY_RESULT(blob, reassemble_blob(role));
  if (blob.size() < 48 || (blob.size() - 48) % 16 != 0) {
    return Status::Error("Invalid encrypted key blob size");
  }

  auto ciphertext_size = blob.size() - 48;
  auto derived_keys = derive_keys();

  string mac_input = Slice(blob).substr(0, 16 + ciphertext_size).str();
  mac_input.push_back(static_cast<char>(role_byte(role)));
  string computed_mac(32, '\0');
  hmac_sha256(as_slice(derived_keys.mac_key), mac_input, MutableSlice(computed_mac));
  secure_zero(MutableSlice(mac_input));
  if (!constant_time_equals(Slice(computed_mac), Slice(blob).substr(16 + ciphertext_size, 32))) {
    secure_zero(MutableSlice(computed_mac));
    secure_zero(MutableSlice(blob));
    return Status::Error("Encrypted key blob MAC mismatch");
  }
  secure_zero(MutableSlice(computed_mac));

  string plaintext(ciphertext_size, '\0');
  string iv = Slice(blob).substr(0, 16).str();
  aes_cbc_decrypt(as_slice(derived_keys.aes_key), MutableSlice(iv), Slice(blob).substr(16, ciphertext_size),
                  MutableSlice(plaintext));
  secure_zero(MutableSlice(iv));
  secure_zero(MutableSlice(blob));

  return remove_pkcs7_padding(std::move(plaintext));
}

Result<string> remove_pkcs7_padding(string plaintext) {
  if (plaintext.empty() || plaintext.size() % 16 != 0) {
    return Status::Error("Invalid plaintext block size");
  }
  auto padding = static_cast<uint8>(plaintext.back());
  if (padding == 0 || padding > 16 || padding > plaintext.size()) {
    secure_zero(MutableSlice(plaintext));
    return Status::Error("Invalid PKCS#7 padding");
  }
  for (size_t i = plaintext.size() - padding; i < plaintext.size(); i++) {
    if (static_cast<uint8>(plaintext[i]) != padding) {
      secure_zero(MutableSlice(plaintext));
      return Status::Error("Invalid PKCS#7 padding");
    }
  }
  plaintext.resize(plaintext.size() - padding);
  return plaintext;
}

Result<RSA> unseal_once(VaultKeyRole role) {
  TRY_RESULT(pem, reassemble_and_decrypt_impl(role));
  SCOPE_EXIT {
    secure_zero(MutableSlice(pem));
  };

  TRY_RESULT(rsa, RSA::from_pem_public_key(pem));
  auto fingerprint = rsa.get_fingerprint();
  if (fingerprint != RsaKeyVault::expected_fingerprint(role)) {
    return Status::Error("RSA fingerprint mismatch");
  }
  return rsa;
}

const Result<RSA> &cached_unseal(VaultKeyRole role) {
  switch (role) {
    case VaultKeyRole::MainMtproto: {
      static const Result<RSA> result = unseal_once(VaultKeyRole::MainMtproto);
      return result;
    }
    case VaultKeyRole::TestMtproto: {
      static const Result<RSA> result = unseal_once(VaultKeyRole::TestMtproto);
      return result;
    }
    case VaultKeyRole::SimpleConfig: {
      static const Result<RSA> result = unseal_once(VaultKeyRole::SimpleConfig);
      return result;
    }
    default: {
      static const Result<RSA> result = Status::Error("Unknown vault key role");
      return result;
    }
  }
}

}  // namespace

int64 RsaKeyVault::expected_fingerprint(VaultKeyRole role) {
  return StaticCatalog::pinned_slot(role);
}

Result<RSA> RsaKeyVault::unseal(VaultKeyRole role) {
  const auto &result = cached_unseal(role);
  if (result.is_error()) {
    return result.error().clone();
  }
  return result.ok().clone();
}

Status RsaKeyVault::verify_integrity() {
  for (auto role : {VaultKeyRole::MainMtproto, VaultKeyRole::TestMtproto, VaultKeyRole::SimpleConfig}) {
    const auto &result = cached_unseal(role);
    if (result.is_error()) {
      return result.error().clone();
    }
  }
  return Status::OK();
}

Result<string> RsaKeyVault::reassemble_and_decrypt(VaultKeyRole role) {
  return reassemble_and_decrypt_impl(role);
}

}  // namespace mtproto
}  // namespace td