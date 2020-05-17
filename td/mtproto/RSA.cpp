//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/RSA.h"

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

namespace td {

RSA::RSA(BigNum n, BigNum e) : n_(std::move(n)), e_(std::move(e)) {
}

RSA RSA::clone() const {
  return RSA(n_.clone(), e_.clone());
}

Result<RSA> RSA::from_pem_public_key(Slice pem) {
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
  // string objects are necessary, because mtproto_api::rsa_public_key contains Slice inside
  string n_str = n_.to_binary();
  string e_str = e_.to_binary();
  mtproto_api::rsa_public_key public_key(n_str, e_str);
  size_t size = tl_calc_length(public_key);
  std::vector<unsigned char> tmp(size);
  size = tl_store_unsafe(public_key, tmp.data());
  CHECK(size == tmp.size());
  unsigned char key_sha1[20];
  sha1(Slice(tmp.data(), tmp.size()), key_sha1);
  return as<int64>(key_sha1 + 12);
}

size_t RSA::size() const {
  // Checked in RSA::from_pem_public_key step
  return 256;
}

size_t RSA::encrypt(unsigned char *from, size_t from_len, size_t max_from_len, unsigned char *to, size_t to_len) const {
  CHECK(from_len > 0 && from_len <= 2550);
  size_t pad = (25500 - from_len - 32) % 255 + 32;
  size_t chunks = (from_len + pad) / 255;
  int bits = n_.get_num_bits();
  CHECK(bits >= 2041 && bits <= 2048);
  CHECK(chunks * 255 == from_len + pad);
  CHECK(from_len + pad <= max_from_len);
  CHECK(chunks * 256 <= to_len);
  Random::secure_bytes(from + from_len, pad);

  BigNumContext ctx;
  BigNum y;
  while (chunks-- > 0) {
    BigNum x = BigNum::from_binary(Slice(from, 255));
    BigNum::mod_exp(y, x, e_, n_, ctx);
    MutableSlice(to, 256).copy_from(y.to_binary(256));
    to += 256;
  }
  return chunks * 256;
}

void RSA::decrypt_signature(Slice from, MutableSlice to) const {
  CHECK(from.size() == 256);
  BigNumContext ctx;
  BigNum x = BigNum::from_binary(from);
  BigNum y;
  BigNum::mod_exp(y, x, e_, n_, ctx);
  to.copy_from(y.to_binary(256));
}

}  // namespace td
