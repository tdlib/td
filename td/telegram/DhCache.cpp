//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DhCache.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/utils/common.h"

namespace td {

static string good_prime_key(Slice prime_str) {
  string key("good_prime:");
  key.append(prime_str.data(), prime_str.size());
  return key;
}

int DhCache::is_good_prime(Slice prime_str) const {
  string value = G()->td_db()->get_binlog_pmc()->get(good_prime_key(prime_str));
  if (value == "good") {
    return 1;
  }
  if (value == "bad") {
    return 0;
  }
  CHECK(value == "");
  return -1;
}

void DhCache::add_good_prime(Slice prime_str) const {
  G()->td_db()->get_binlog_pmc()->set(good_prime_key(prime_str), "good");
}

void DhCache::add_bad_prime(Slice prime_str) const {
  G()->td_db()->get_binlog_pmc()->set(good_prime_key(prime_str), "bad");
}

}  // namespace td
