//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/RSA.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>

namespace td {

class PublicRsaKeySharedMain final : public mtproto::PublicRsaKeyInterface {
 public:
  explicit PublicRsaKeySharedMain(vector<RsaKey> &&keys) : keys_(std::move(keys)) {
  }

  static std::shared_ptr<PublicRsaKeySharedMain> create(bool is_test);
  static size_t expected_entry_count(bool is_test);
  static Status validate_entry_count(size_t observed_entry_count, bool is_test);
  static Status check_catalog_entry(int64 fingerprint, bool is_test);

  Result<RsaKey> get_rsa_key(const vector<int64> &fingerprints) final;
  bool uses_static_main_keyset() const final {
    return true;
  }

  void drop_keys() final;

 private:
  vector<RsaKey> keys_;
};

}  // namespace td
