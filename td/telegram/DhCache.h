//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/DhHandshake.h"

#include "td/utils/Slice.h"

namespace td {

class DhCache : public DhCallback {
 public:
  int is_good_prime(Slice prime_str) const override;
  void add_good_prime(Slice prime_str) const override;
  void add_bad_prime(Slice prime_str) const override;

  static DhCallback *instance() {
    static DhCache res;
    return &res;
  }
};
}  // namespace td
