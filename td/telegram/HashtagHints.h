//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Hints.h"
#include "td/utils/Status.h"

namespace td {

class HashtagHints : public Actor {
 public:
  HashtagHints(string mode, ActorShared<> parent);

  void hashtag_used(const string &hashtag);

  void remove_hashtag(string hashtag, Promise<> promise);

  void query(const string &prefix, int32 limit, Promise<std::vector<string>> promise);

 private:
  string mode_;
  Hints hints_;
  bool sync_with_db_ = false;
  int64 counter_ = 0;

  ActorShared<> parent_;

  string get_key() const;

  void start_up() override;

  void hashtag_used_impl(const string &hashtag);
  void from_db(Result<string> data, bool dummy);
  std::vector<string> keys_to_strings(const std::vector<int64> &keys);
};

}  // namespace td
