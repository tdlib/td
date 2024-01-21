//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/RSA.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class PublicRsaKeySharedMain final : public mtproto::PublicRsaKeyInterface {
 public:
  explicit PublicRsaKeySharedMain(bool is_test);

  Result<RsaKey> get_rsa_key(const vector<int64> &fingerprints) final;

  void drop_keys() final;

 private:
  vector<RsaKey> keys_;

  const RsaKey *get_rsa_key_unsafe(int64 fingerprint) const;
};

}  // namespace td
