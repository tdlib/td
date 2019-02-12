//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/crypto.h"

#include "td/mtproto/mtproto_api.h"

#include "td/utils/as.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tl_storers.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <cstring>

namespace td {

/*** RSA ***/
RSA::RSA(BigNum n, BigNum e) : n_(std::move(n)), e_(std::move(e)) {
  e_.ensure_const_time();
}

RSA RSA::clone() const {
  return RSA(n_.clone(), e_.clone());
}

Result<RSA> RSA::from_pem(Slice pem) {
  init_crypto();

  auto *bio =
      BIO_new_mem_buf(const_cast<void *>(static_cast<const void *>(pem.ubegin())), narrow_cast<int32>(pem.size()));
  if (bio == nullptr) {
    return Status::Error("Cannot create BIO");
  }
  SCOPE_EXIT {
    BIO_free(bio);
  };

  auto rsa = PEM_read_bio_RSAPublicKey(bio, nullptr, nullptr, nullptr);
  if (rsa == nullptr) {
    return Status::Error("Error while reading rsa pubkey");
  }
  SCOPE_EXIT {
    RSA_free(rsa);
  };

  if (RSA_size(rsa) != 256) {
    return Status::Error("RSA_size != 256");
  }

  const BIGNUM *n_num;
  const BIGNUM *e_num;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  n_num = rsa->n;
  e_num = rsa->e;
#else
  RSA_get0_key(rsa, &n_num, &e_num, nullptr);
#endif

  auto n = static_cast<void *>(BN_dup(n_num));
  auto e = static_cast<void *>(BN_dup(e_num));
  if (n == nullptr || e == nullptr) {
    return Status::Error("Cannot dup BIGNUM");
  }

  return RSA(BigNum::from_raw(n), BigNum::from_raw(e));
}

int64 RSA::get_fingerprint() const {
  mtproto_api::rsa_public_key public_key;
  // string objects are necessary, because mtproto_api::rsa_public_key contains Slice inside
  string n_str = n_.to_binary();
  string e_str = e_.to_binary();
  public_key.n_ = n_str;
  public_key.e_ = e_str;
  size_t size = tl_calc_length(public_key);
  std::vector<unsigned char> tmp(size);
  size = tl_store_unsafe(public_key, tmp.data());
  CHECK(size == tmp.size());
  unsigned char key_sha1[20];
  sha1(Slice(tmp.data(), tmp.size()), key_sha1);
  return as<int64>(key_sha1 + 12);
}

size_t RSA::size() const {
  // Checked in RSA::from_pem step
  return 256;
}

size_t RSA::encrypt(unsigned char *from, size_t from_len, unsigned char *to) const {
  CHECK(from_len > 0 && from_len <= 2550);
  size_t pad = (25500 - from_len - 32) % 255 + 32;
  size_t chunks = (from_len + pad) / 255;
  int bits = n_.get_num_bits();
  CHECK(bits >= 2041 && bits <= 2048);
  CHECK(chunks * 255 == from_len + pad);
  Random::secure_bytes(from + from_len, pad);

  BigNumContext ctx;
  BigNum y;
  while (chunks-- > 0) {
    BigNum x = BigNum::from_binary(Slice(from, 255));
    BigNum::mod_exp(y, x, e_, n_, ctx);
    string result = y.to_binary(256);
    std::memcpy(to, result.c_str(), 256);
    to += 256;
  }
  return chunks * 256;
}

void RSA::decrypt(Slice from, MutableSlice to) const {
  CHECK(from.size() == 256);
  BigNumContext ctx;
  BigNum x = BigNum::from_binary(from);
  BigNum y;
  BigNum::mod_exp(y, x, e_, n_, ctx);
  string result = y.to_binary(256);
  std::memcpy(to.data(), result.c_str(), 256);
}

/*** KDF ***/
void KDF(const string &auth_key, const UInt128 &msg_key, int X, UInt256 *aes_key, UInt256 *aes_iv) {
  CHECK(auth_key.size() == 2048 / 8);
  const char *auth_key_raw = auth_key.c_str();
  uint8 buf[48];
  as<UInt128>(buf) = msg_key;
  as<UInt256>(buf + 16) = as<UInt256>(auth_key_raw + X);
  uint8 sha1_a[20];
  sha1(Slice(buf, 48), sha1_a);

  as<UInt128>(buf) = as<UInt128>(auth_key_raw + X + 32);
  as<UInt128>(buf + 16) = msg_key;
  as<UInt128>(buf + 32) = as<UInt128>(auth_key_raw + X + 48);
  uint8 sha1_b[20];
  sha1(Slice(buf, 48), sha1_b);

  as<UInt256>(buf) = as<UInt256>(auth_key_raw + 64 + X);
  as<UInt128>(buf + 32) = msg_key;
  uint8 sha1_c[20];
  sha1(Slice(buf, 48), sha1_c);

  as<UInt128>(buf) = msg_key;
  as<UInt256>(buf + 16) = as<UInt256>(auth_key_raw + 96 + X);
  uint8 sha1_d[20];
  sha1(Slice(buf, 48), sha1_d);

  as<uint64>(aes_key->raw) = as<uint64>(sha1_a);
  as<UInt<96>>(aes_key->raw + 8) = as<UInt<96>>(sha1_b + 8);
  as<UInt<96>>(aes_key->raw + 20) = as<UInt<96>>(sha1_c + 4);

  as<UInt<96>>(aes_iv->raw) = as<UInt<96>>(sha1_a + 8);
  as<uint64>(aes_iv->raw + 12) = as<uint64>(sha1_b);
  as<uint32>(aes_iv->raw + 20) = as<uint32>(sha1_c + 16);
  as<uint64>(aes_iv->raw + 24) = as<uint64>(sha1_d);
}

void tmp_KDF(const UInt128 &server_nonce, const UInt256 &new_nonce, UInt256 *tmp_aes_key, UInt256 *tmp_aes_iv) {
  // tmp_aes_key := SHA1(new_nonce + server_nonce) + substr (SHA1(server_nonce + new_nonce), 0, 12);
  uint8 buf[512 / 8];
  as<UInt256>(buf) = new_nonce;
  as<UInt128>(buf + 32) = server_nonce;
  sha1(Slice(buf, 48), tmp_aes_key->raw);

  as<UInt128>(buf) = server_nonce;
  as<UInt256>(buf + 16) = new_nonce;
  uint8 sha1_server_new[20];
  sha1(Slice(buf, 48), sha1_server_new);
  as<UInt<96>>(tmp_aes_key->raw + 20) = as<UInt<96>>(sha1_server_new);

  // tmp_aes_iv := substr (SHA1(server_nonce + new_nonce), 12, 8) + SHA1(new_nonce + new_nonce) + substr (new_nonce,
  // 0,
  // 4);
  as<uint64>(tmp_aes_iv->raw) = as<uint64>(sha1_server_new + 12);

  as<UInt256>(buf) = new_nonce;
  as<UInt256>(buf + 32) = new_nonce;
  sha1(Slice(buf, 64), tmp_aes_iv->raw + 8);
  as<uint32>(tmp_aes_iv->raw + 28) = as<uint32>(new_nonce.raw);
}

// msg_key_large = SHA256 (substr (auth_key, 88+x, 32) + plaintext + random_padding);
// msg_key = substr (msg_key_large, 8, 16);

void KDF2(Slice auth_key, const UInt128 &msg_key, int X, UInt256 *aes_key, UInt256 *aes_iv) {
  uint8 buf_raw[36 + 16];
  MutableSlice buf(buf_raw, 36 + 16);
  Slice msg_key_slice = as_slice(msg_key);

  // sha256_a = SHA256 (msg_key + substr (auth_key, x, 36));
  buf.copy_from(msg_key_slice);
  buf.substr(16, 36).copy_from(auth_key.substr(X, 36));
  uint8 sha256_a_raw[32];
  MutableSlice sha256_a(sha256_a_raw, 32);
  sha256(buf, sha256_a);

  // sha256_b = SHA256 (substr (auth_key, 40+x, 36) + msg_key);
  buf.copy_from(auth_key.substr(40 + X, 36));
  buf.substr(36).copy_from(msg_key_slice);
  uint8 sha256_b_raw[32];
  MutableSlice sha256_b(sha256_b_raw, 32);
  sha256(buf, sha256_b);

  // aes_key = substr (sha256_a, 0, 8) + substr (sha256_b, 8, 16) + substr (sha256_a, 24, 8);
  MutableSlice aes_key_slice(aes_key->raw, sizeof(aes_key->raw));
  aes_key_slice.copy_from(sha256_a.substr(0, 8));
  aes_key_slice.substr(8).copy_from(sha256_b.substr(8, 16));
  aes_key_slice.substr(24).copy_from(sha256_a.substr(24, 8));

  // aes_iv = substr (sha256_b, 0, 8) + substr (sha256_a, 8, 16) + substr (sha256_b, 24, 8);
  MutableSlice aes_iv_slice(aes_iv->raw, sizeof(aes_iv->raw));
  aes_iv_slice.copy_from(sha256_b.substr(0, 8));
  aes_iv_slice.substr(8).copy_from(sha256_a.substr(8, 16));
  aes_iv_slice.substr(24).copy_from(sha256_b.substr(24, 8));
}

}  // namespace td
