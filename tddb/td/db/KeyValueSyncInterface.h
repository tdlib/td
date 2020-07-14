//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <unordered_map>

namespace td {

class KeyValueSyncInterface {
 public:
  // SeqNo is used to restore total order on all write queries.
  // Some implementations may return 0 as SeqNo.
  using SeqNo = uint64;

  KeyValueSyncInterface() = default;
  KeyValueSyncInterface(const KeyValueSyncInterface &) = delete;
  KeyValueSyncInterface &operator=(const KeyValueSyncInterface &) = delete;
  KeyValueSyncInterface(KeyValueSyncInterface &&) = default;
  KeyValueSyncInterface &operator=(KeyValueSyncInterface &&) = default;
  virtual ~KeyValueSyncInterface() = default;

  virtual SeqNo set(string key, string value) = 0;

  virtual bool isset(const string &key) = 0;

  virtual string get(const string &key) = 0;

  virtual std::unordered_map<string, string> prefix_get(Slice prefix) = 0;

  virtual std::unordered_map<string, string> get_all() = 0;

  virtual SeqNo erase(const string &key) = 0;

  virtual void erase_by_prefix(Slice prefix) = 0;

  virtual void force_sync(Promise<> &&promise) = 0;

  virtual void close(Promise<> promise) = 0;
};

}  // namespace td
