//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
  void break_key() {
    auth_key_id_++;
    auth_key_[0]++;
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
    was_auth_flag_ |= new_auth_flag;
    auth_flag_ = new_auth_flag;
  }

  bool need_header() const {
    return need_header_;
  }
  void set_need_header(bool need_header) {
    need_header_ = need_header;
  }
  double expires_at() const {
    return expires_at_;
  }
  double created_at() const {
    return created_at_;
  }

  void set_expires_at(double expires_at) {
    expires_at_ = expires_at;
    // expires_at_ = Time::now() + 60 * 60 + 10 * 60;
  }
  void set_created_at(double created_at) {
    created_at_ = created_at;
  }
  void clear() {
    auth_key_.clear();
  }

  static constexpr int32 AUTH_FLAG = 1;
  static constexpr int32 WAS_AUTH_FLAG = 2;
  static constexpr int32 HAS_CREATED_AT = 4;

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_binary(auth_key_id_);
    bool has_created_at = created_at_ != 0;
    storer.store_binary(static_cast<int32>((auth_flag_ ? AUTH_FLAG : 0) | (was_auth_flag_ ? WAS_AUTH_FLAG : 0) |
                                           (has_created_at ? HAS_CREATED_AT : 0)));
    storer.store_string(auth_key_);
    if (has_created_at) {
      storer.store_binary(created_at_);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    auth_key_id_ = parser.fetch_long();
    auto flags = parser.fetch_int();
    auth_flag_ = (flags & AUTH_FLAG) != 0;
    was_auth_flag_ = (flags & WAS_AUTH_FLAG) != 0 || auth_flag_;
    auth_key_ = parser.template fetch_string<string>();
    if ((flags & HAS_CREATED_AT) != 0) {
      created_at_ = parser.fetch_double();
    }
    // just in case
    need_header_ = true;
  }

 private:
  uint64 auth_key_id_{0};
  string auth_key_;
  bool auth_flag_{false};
  bool was_auth_flag_{false};
  bool need_header_{true};
  double expires_at_{0};
  double created_at_{0};
};

}  // namespace mtproto
}  // namespace td
