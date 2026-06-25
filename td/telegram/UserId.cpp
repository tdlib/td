//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"

namespace td {

vector<UserId> UserId::get_user_ids(const vector<int64> &input_user_ids, bool only_valid) {
  vector<UserId> user_ids;
  user_ids.reserve(input_user_ids.size());
  for (auto &input_user_id : input_user_ids) {
    UserId user_id(input_user_id);
    if (!only_valid || user_id.is_valid()) {
      user_ids.push_back(user_id);
    }
  }
  return user_ids;
}

vector<int64> UserId::get_input_user_ids(const vector<UserId> &user_ids) {
  return transform(user_ids, [](UserId user_id) { return user_id.get(); });
}

}  // namespace td
