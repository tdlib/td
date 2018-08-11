//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/crypto.h"

#include "td/mtproto/mtproto_api.h"

#include "td/utils/crypto.h"
#include "td/utils/int_types.h"  // for UInt256, UInt128, etc
#include "td/utils/logging.h"
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

  auto *rsa = RSA_new();
  if (rsa == nullptr) {
    return Status::Error("Cannot create RSA");
  }
  SCOPE_EXIT {
    RSA_free(rsa);
  };

  if (!PEM_read_bio_RSAPublicKey(bio, &rsa, nullptr, nullptr)) {
    return Status::Error("Error while reading rsa pubkey");
  }

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

Status DhHandshake::check_config(Slice prime_str, const BigNum &prime, int32 g_int, BigNumContext &ctx,
                                 DhCallback *callback) {
  // check that 2^2047 <= p < 2^2048
  if (prime.get_num_bits() != 2048) {
    return Status::Error("p is not 2048-bit number");
  }

  // g generates a cyclic subgroup of prime order (p - 1) / 2, i.e. is a quadratic residue mod p.
  // Since g is always equal to 2, 3, 4, 5, 6 or 7, this is easily done using quadratic reciprocity law,
  // yielding a simple condition on
  // * p mod 4g - namely, p mod 8 = 7 for g = 2; p mod 3 = 2 for g = 3;
  // * no extra condition for g = 4;
  // * p mod 5 = 1 or 4 for g = 5;
  // * p mod 24 = 19 or 23 for g = 6;
  // * p mod 7 = 3, 5 or 6 for g = 7.

  bool mod_ok;
  uint32 mod_r;
  switch (g_int) {
    case 2:
      mod_ok = prime % 8 == 7u;
      break;
    case 3:
      mod_ok = prime % 3 == 2u;
      break;
    case 4:
      mod_ok = true;
      break;
    case 5:
      mod_ok = (mod_r = prime % 5) == 1u || mod_r == 4u;
      break;
    case 6:
      mod_ok = (mod_r = prime % 24) == 19u || mod_r == 23u;
      break;
    case 7:
      mod_ok = (mod_r = prime % 7) == 3u || mod_r == 5u || mod_r == 6u;
      break;
    default:
      mod_ok = false;
  }
  if (!mod_ok) {
    return Status::Error("Bad prime mod 4g");
  }

  // check whether p is a safe prime (meaning that both p and (p - 1) / 2 are prime)
  int is_good_prime = -1;
  if (callback) {
    is_good_prime = callback->is_good_prime(prime_str);
  }
  if (is_good_prime != -1) {
    return is_good_prime ? Status::OK() : Status::Error("p or (p - 1) / 2 is not a prime number");
  }
  if (!prime.is_prime(ctx)) {
    if (callback) {
      callback->add_bad_prime(prime_str);
    }
    return Status::Error("p is not a prime number");
  }

  BigNum half_prime = prime;
  half_prime -= 1;
  half_prime /= 2;
  if (!half_prime.is_prime(ctx)) {
    if (callback) {
      callback->add_bad_prime(prime_str);
    }
    return Status::Error("(p - 1) / 2 is not a prime number");
  }
  if (callback) {
    callback->add_good_prime(prime_str);
  }
  return Status::OK();
}

Status DhHandshake::dh_check(const BigNum &prime, const BigNum &g_a, const BigNum &g_b) {
  // IMPORTANT: Apart from the conditions on the Diffie-Hellman prime dh_prime and generator g, both sides are
  // to check that g, g_a and g_b are greater than 1 and less than dh_prime - 1.
  // We recommend checking that g_a and g_b are between 2^{2048-64} and dh_prime - 2^{2048-64} as well.

  CHECK(prime.get_num_bits() == 2048);
  BigNum left;
  left.set_value(0);
  left.set_bit(2048 - 64);

  BigNum right;
  BigNum::sub(right, prime, left);

  if (BigNum::compare(left, g_a) > 0 || BigNum::compare(g_a, right) > 0 || BigNum::compare(left, g_b) > 0 ||
      BigNum::compare(g_b, right) > 0) {
    std::string x(2048, '0');
    std::string y(2048, '0');
    for (int i = 0; i < 2048; i++) {
      if (g_a.is_bit_set(i)) {
        x[i] = '1';
      }
      if (g_b.is_bit_set(i)) {
        y[i] = '1';
      }
    }
    LOG(ERROR) << x;
    LOG(ERROR) << y;
    return Status::Error("g^a or g^b is not between 2^{2048-64} and dh_prime - 2^{2048-64}");
  }

  return Status::OK();
}

void DhHandshake::set_config(int32 g_int, Slice prime_str) {
  has_config_ = true;
  prime_ = BigNum::from_binary(prime_str);
  prime_str_ = prime_str.str();

  b_ = BigNum();
  g_b_ = BigNum();

  BigNum::random(b_, 2048, -1, 0);

  // g^b
  g_int_ = g_int;
  g_.set_value(g_int_);

  BigNum::mod_exp(g_b_, g_, b_, prime_, ctx_);
}

Status DhHandshake::check_config(int32 g_int, Slice prime_str, DhCallback *callback) {
  BigNumContext ctx;
  auto prime = BigNum::from_binary(prime_str);
  return check_config(prime_str, prime, g_int, ctx, callback);
}

void DhHandshake::set_g_a_hash(Slice g_a_hash) {
  has_g_a_hash_ = true;
  ok_g_a_hash_ = false;
  CHECK(!has_g_a_);
  g_a_hash_ = g_a_hash.str();
}

void DhHandshake::set_g_a(Slice g_a_str) {
  has_g_a_ = true;
  if (has_g_a_hash_) {
    string g_a_hash(32, ' ');
    sha256(g_a_str, g_a_hash);
    ok_g_a_hash_ = g_a_hash == g_a_hash_;
  }
  g_a_ = BigNum::from_binary(g_a_str);
}

string DhHandshake::get_g_a() const {
  CHECK(has_g_a_);
  return g_a_.to_binary();
}

string DhHandshake::get_g_b() const {
  CHECK(has_config_);
  return g_b_.to_binary();
}
string DhHandshake::get_g_b_hash() const {
  string g_b_hash(32, ' ');
  sha256(get_g_b(), g_b_hash);
  return g_b_hash;
}

Status DhHandshake::run_checks(bool skip_config_check, DhCallback *callback) {
  CHECK(has_g_a_ && has_config_);

  if (has_g_a_hash_ && !ok_g_a_hash_) {
    return Status::Error("g_a_hash mismatch");
  }

  if (!skip_config_check) {
    TRY_STATUS(check_config(prime_str_, prime_, g_int_, ctx_, callback));
  }

  return dh_check(prime_, g_a_, g_b_);
}

BigNum DhHandshake::get_g() const {
  CHECK(has_config_);
  return g_;
}

BigNum DhHandshake::get_p() const {
  CHECK(has_config_);
  return prime_;
}

BigNum DhHandshake::get_b() const {
  CHECK(has_config_);
  return b_;
}

BigNum DhHandshake::get_g_ab() {
  CHECK(has_g_a_ && has_config_);
  BigNum g_ab;
  BigNum::mod_exp(g_ab, g_a_, b_, prime_, ctx_);
  return g_ab;
}

std::pair<int64, string> DhHandshake::gen_key() {
  string key = get_g_ab().to_binary(2048 / 8);
  auto key_id = calc_key_id(key);
  return std::pair<int64, string>(key_id, std::move(key));
}

int64 DhHandshake::calc_key_id(const string &auth_key) {
  UInt<160> auth_key_sha1;
  sha1(auth_key, auth_key_sha1.raw);
  return as<int64>(auth_key_sha1.raw + 12);
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
  Slice msg_key_slice(msg_key.raw, sizeof(msg_key.raw));

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
