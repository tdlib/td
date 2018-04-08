//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Time.h"

namespace td {
namespace mtproto {
class AuthKey {
 public:
  AuthKey() = default;
  AuthKey(uint64 auth_key_id, string &&auth_key) : auth_key_id_(auth_key_id), auth_key_(auth_key) {
  }

  bool empty() const {
    return auth_key_.empty();
  }
  const string &key() const {
    return auth_key_;
  }
  uint64 id() const {
    return auth_key_id_;
  }
  bool auth_flag() const {
    return auth_flag_;
  }
  bool was_auth_flag() const {
    return was_auth_flag_;
  }
  void set_auth_flag(bool new_auth_flag) {
    if (new_auth_flag == false) {
      clear();
    } else {
      was_auth_flag_ = true;
    }
    auth_flag_ = new_auth_flag;
  }

  bool need_header() const {
    return need_header_;
  }
  void set_need_header(bool need_header) {
    need_header_ = need_header;
  }
  double expire_at() const {
    return expire_at_;
  }
  void set_expire_at(double expire_at) {
    expire_at_ = expire_at;
    // expire_at_ = Time::now() + 60 * 60 + 10 * 60;
  }
  void clear() {
    auth_key_.clear();
  }

  enum { AUTH_FLAG = 1, WAS_AUTH_FLAG = 2 };
  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_binary(auth_key_id_);
    storer.store_binary((auth_flag_ ? AUTH_FLAG : 0) | (was_auth_flag_ ? WAS_AUTH_FLAG : 0));
    storer.store_string(auth_key_);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    auth_key_id_ = parser.fetch_long();
    auto flags = parser.fetch_int();
    auth_flag_ = (flags & AUTH_FLAG) != 0;
    was_auth_flag_ = (flags & WAS_AUTH_FLAG) != 0 || auth_flag_;
    auth_key_ = parser.template fetch_string<string>();
    // just in case
    need_header_ = true;
  }

 private:
  uint64 auth_key_id_ = 0;
  string auth_key_;
  bool auth_flag_ = false;
  bool was_auth_flag_ = false;
  bool need_header_ = true;
  double expire_at_ = 0;
};

}  // namespace mtproto
}  // namespace td
