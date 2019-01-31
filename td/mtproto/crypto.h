//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/BigNum.h"
#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/UInt.h"

#include <utility>

namespace td {

/*** RSA ***/
class RSA {
 public:
  RSA clone() const;
  int64 get_fingerprint() const;
  size_t size() const;
  size_t encrypt(unsigned char *from, size_t from_len, unsigned char *to) const;

  void decrypt(Slice from, MutableSlice to) const;

  static Result<RSA> from_pem(Slice pem);

 private:
  RSA(BigNum n, BigNum e);
  BigNum n_;
  BigNum e_;
};

/*** PublicRsaKeyInterface ***/
class PublicRsaKeyInterface {
 public:
  virtual ~PublicRsaKeyInterface() = default;
  virtual Result<std::pair<RSA, int64>> get_rsa(const vector<int64> &fingerprints) = 0;
  virtual void drop_keys() = 0;
};

/*** KDF ***/
void KDF(const string &auth_key, const UInt128 &msg_key, int X, UInt256 *aes_key, UInt256 *aes_iv);
void tmp_KDF(const UInt128 &server_nonce, const UInt256 &new_nonce, UInt256 *tmp_aes_key, UInt256 *tmp_aes_iv);
void KDF2(Slice auth_key, const UInt128 &msg_key, int X, UInt256 *aes_key, UInt256 *aes_iv);

}  // namespace td
