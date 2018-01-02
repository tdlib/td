//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/mtproto/AuthKey.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <set>

namespace td {
namespace mtproto {

struct ServerSalt {
  int64 salt;
  double valid_since;
  double valid_until;
};
template <class StorerT>
void store(const ServerSalt &salt, StorerT &storer) {
  storer.template store_binary<int64>(salt.salt);
  storer.template store_binary<double>(salt.valid_since);
  storer.template store_binary<double>(salt.valid_until);
}
template <class ParserT>
void parse(ServerSalt &salt, ParserT &parser) {
  salt.salt = parser.fetch_long();
  salt.valid_since = parser.fetch_double();
  salt.valid_until = parser.fetch_double();
}

class MessageIdDuplicateChecker {
 public:
  Status check(int64 message_id) {
    // In addition, the identifiers (msg_id) of the last N messages received from the other side must be stored, and if
    // a message comes in with msg_id lower than all or equal to any of the stored values, that message is to be
    // ignored. Otherwise, the new message msg_id is added to the set, and, if the number of stored msg_id values is
    // greater than N, the oldest (i. e. the lowest) is forgotten.
    if (saved_message_ids_.size() == MAX_SAVED_MESSAGE_IDS) {
      auto oldest_message_id = *saved_message_ids_.begin();
      if (message_id < oldest_message_id) {
        return Status::Error(1, PSLICE() << "Ignore very old message_id " << tag("oldest message_id", oldest_message_id)
                                         << tag("got message_id", message_id));
      }
    }
    if (saved_message_ids_.count(message_id) != 0) {
      return Status::Error(1, PSLICE() << "Ignore duplicated_message id " << tag("message_id", message_id));
    }

    saved_message_ids_.insert(message_id);
    if (saved_message_ids_.size() > MAX_SAVED_MESSAGE_IDS) {
      saved_message_ids_.erase(saved_message_ids_.begin());
    }
    return Status::OK();
  }

 private:
  static constexpr size_t MAX_SAVED_MESSAGE_IDS = 1000;
  std::set<int64> saved_message_ids_;
};

class AuthData {
 public:
  AuthData() {
    server_salt_.salt = Random::secure_int64();
    server_salt_.valid_since = -1e10;
    server_salt_.valid_until = -1e10;
  }
  AuthData(const AuthData &) = delete;
  AuthData &operator=(const AuthData &) = delete;
  virtual ~AuthData() = default;

  bool is_ready(double now) {
    if (!has_main_auth_key()) {
      LOG(INFO) << "Need main auth key";
      return false;
    }
    if (use_pfs() && !has_tmp_auth_key(now)) {
      LOG(INFO) << "Need tmp auth key";
      return false;
    }
    if (!has_salt(now)) {
      LOG(INFO) << "no salt";
      return false;
    }
    return true;
  }

  uint64 session_id_;
  void set_main_auth_key(AuthKey auth_key) {
    main_auth_key_ = std::move(auth_key);
  }
  const AuthKey &get_main_auth_key() const {
    // CHECK(has_main_auth_key());
    return main_auth_key_;
  }
  bool has_main_auth_key() const {
    return !main_auth_key_.empty();
  }
  bool need_main_auth_key() const {
    return !has_main_auth_key();
  }

  void set_tmp_auth_key(AuthKey auth_key) {
    CHECK(!auth_key.empty());
    tmp_auth_key_ = std::move(auth_key);
  }
  const AuthKey &get_tmp_auth_key() const {
    // CHECK(has_tmp_auth_key());
    return tmp_auth_key_;
  }
  bool was_tmp_auth_key() const {
    return use_pfs() && !tmp_auth_key_.empty();
  }
  bool need_tmp_auth_key(double now) const {
    if (!use_pfs()) {
      return false;
    }
    if (tmp_auth_key_.empty()) {
      return true;
    }
    if (now > tmp_auth_key_.expire_at() - 60 * 60 * 2 /*2 hours*/) {
      return true;
    }
    if (!has_tmp_auth_key(now)) {
      return true;
    }
    return false;
  }
  void drop_main_auth_key() {
    main_auth_key_ = AuthKey();
  }
  void drop_tmp_auth_key() {
    tmp_auth_key_ = AuthKey();
  }
  bool has_tmp_auth_key(double now) const {
    if (!use_pfs()) {
      return false;
    }
    if (tmp_auth_key_.empty()) {
      return false;
    }
    if (now > tmp_auth_key_.expire_at() - 60 * 60 /*1 hour*/) {
      return false;
    }
    return true;
  }

  const AuthKey &get_auth_key() const {
    if (use_pfs()) {
      return get_tmp_auth_key();
    }
    return get_main_auth_key();
  }
  bool has_auth_key(double now) const {
    if (use_pfs()) {
      return has_tmp_auth_key(now);
    }
    return has_main_auth_key();
  }

  bool get_auth_flag() const {
    return main_auth_key_.auth_flag();
  }
  void set_auth_flag(bool auth_flag) {
    main_auth_key_.set_auth_flag(auth_flag);
    if (!auth_flag) {
      tmp_auth_key_.set_auth_flag(auth_flag);
    }
  }

  bool get_bind_flag() const {
    return !use_pfs() || tmp_auth_key_.auth_flag();
  }
  void on_bind() {
    CHECK(use_pfs());
    tmp_auth_key_.set_auth_flag(true);
  }

  Slice header() {
    if (use_pfs()) {
      return tmp_auth_key_.need_header() ? Slice(header_) : Slice();
    } else {
      return main_auth_key_.need_header() ? Slice(header_) : Slice();
    }
  }
  void set_header(std::string header) {
    header_ = std::move(header);
  }
  void on_api_response() {
    if (use_pfs()) {
      if (tmp_auth_key_.auth_flag()) {
        tmp_auth_key_.set_need_header(false);
      }
    } else {
      if (main_auth_key_.auth_flag()) {
        main_auth_key_.set_need_header(false);
      }
    }
  }

  uint64 get_session_id() const {
    return session_id_;
  }

  double get_server_time(double now) const {
    return server_time_difference_ + now;
  }

  double get_server_time_difference() const {
    return server_time_difference_;
  }

  // diff == msg_id / 2^32 - now == old_server_now - now <= server_now - now
  // server_time_difference >= max{diff}
  bool update_server_time_difference(double diff) {
    if (!server_time_difference_was_updated_) {
      server_time_difference_was_updated_ = true;
      LOG(DEBUG) << "UPDATE_SERVER_TIME_DIFFERENCE: " << server_time_difference_ << " -> " << diff;
      server_time_difference_ = diff;
    } else if (server_time_difference_ + 1e-4 < diff) {
      LOG(DEBUG) << "UPDATE_SERVER_TIME_DIFFERENCE: " << server_time_difference_ << " -> " << diff;
      server_time_difference_ = diff;
    } else {
      return false;
    }
    LOG(DEBUG) << "SERVER_TIME: " << format::as_hex(static_cast<int>(get_server_time(Time::now_cached())));
    return true;
  }

  void set_server_time_difference(double diff) {
    server_time_difference_was_updated_ = false;
    server_time_difference_ = diff;
  }

  uint64 get_server_salt(double now) {
    update_salt(now);
    return server_salt_.salt;
  }

  void set_server_salt(uint64 salt, double now) {
    server_salt_.salt = salt;
    double server_time = get_server_time(now);
    server_salt_.valid_since = server_time;
    server_salt_.valid_until = server_time + 60 * 10;
    future_salts_.clear();
  }

  bool is_server_salt_valid(double now) {
    return server_salt_.valid_until > get_server_time(now) + 60;
  }

  bool has_salt(double now) {
    update_salt(now);
    return is_server_salt_valid(now);
  }

  bool need_future_salts(double now) {
    update_salt(now);
    return future_salts_.empty() || !is_server_salt_valid(now);
  }

  virtual void set_future_salts(const std::vector<ServerSalt> &salts, double now) {
    if (salts.empty()) {
      return;
    }
    future_salts_ = salts;
    std::sort(future_salts_.begin(), future_salts_.end(),
              [](const ServerSalt &a, const ServerSalt &b) { return a.valid_since > b.valid_since; });
    update_salt(now);
  }

  std::vector<ServerSalt> get_future_salts() const {
    auto res = future_salts_;
    res.push_back(server_salt_);
    return res;
  }

  int64 next_message_id(double now) {
    double server_time = get_server_time(now);
    int64 t = static_cast<int64>(server_time * (1ll << 32));

    // randomize lower bits for clocks with low precision
    // TODO(perf) do not do this for systems with good precision?..
    auto rx = Random::secure_int32();
    auto to_xor = rx & ((1 << 22) - 1);
    auto to_mul = ((rx >> 22) & 1023) + 1;

    t ^= to_xor;
    auto result = t & -4;
    if (last_message_id_ >= result) {
      result = last_message_id_ + 8 * to_mul;
    }
    last_message_id_ = result;
    return result;
  }

  bool is_valid_outbound_msg_id(int64 id, double now) {
    double server_time = get_server_time(now);
    auto id_time = static_cast<double>(id / (1ll << 32));
    return server_time - 300 / 2 < id_time && id_time < server_time + 60 / 2;
  }
  bool is_valid_inbound_msg_id(int64 id, double now) {
    double server_time = get_server_time(now);
    auto id_time = static_cast<double>(id / (1ll << 32));
    return server_time - 300 < id_time && id_time < server_time + 30;
  }

  Status check_packet(int64 session_id, int64 message_id, double now, bool &time_difference_was_updated) {
    // Client is to check that the session_id field in the decrypted message indeed equals to that of an active session
    // created by the client.
    if (get_session_id() != static_cast<uint64>(session_id)) {
      return Status::Error(PSLICE() << "Got packet from different session "
                                    << tag("current session_id", get_session_id())
                                    << tag("got session_id", session_id));
    }

    // Client must check that msg_id has even parity for messages from client to server, and odd parity for messages
    // from server to client.
    if ((message_id & 1) == 0) {
      return Status::Error(PSLICE() << "Got invalid message_id " << tag("message_id", message_id));
    }

    TRY_STATUS(duplicate_checker_.check(message_id));

    time_difference_was_updated = update_server_time_difference(static_cast<uint32>(message_id >> 32) - now);

    // In addition, msg_id values that belong over 30 seconds in the future or over 300 seconds in the past are to be
    // ignored (recall that msg_id approximately equals unixtime * 2^32). This is especially important for the server.
    // The client would also find this useful (to protect from a replay attack), but only if it is certain of its time
    // (for example, if its time has been synchronized with that of the server).
    if (server_time_difference_was_updated_ && !is_valid_inbound_msg_id(message_id, now)) {
      return Status::Error(PSLICE() << "Ignore message with too old or too new message_id "
                                    << tag("message_id", message_id));
    }

    return Status::OK();
  }

  Status check_update(int64 message_id) {
    return updates_duplicate_checker_.check(message_id);
  }

  int32 next_seq_no(bool is_content_related) {
    int32 res = seq_no_;
    if (is_content_related) {
      res |= 1;
      seq_no_ += 2;
    }
    return res;
  }

  void clear_seq_no() {
    seq_no_ = 0;
  }

  void set_use_pfs(bool use_pfs) {
    use_pfs_ = use_pfs;
  }
  bool use_pfs() const {
    return use_pfs_;
  }

 private:
  bool use_pfs_ = true;
  AuthKey main_auth_key_;
  AuthKey tmp_auth_key_;
  bool server_time_difference_was_updated_ = false;
  double server_time_difference_ = 0;
  ServerSalt server_salt_;
  int64 last_message_id_ = 0;
  int32 seq_no_ = 0;
  std::string header_;

  std::vector<ServerSalt> future_salts_;

  MessageIdDuplicateChecker duplicate_checker_;
  MessageIdDuplicateChecker updates_duplicate_checker_;

  void update_salt(double now) {
    double server_time = get_server_time(now);
    while (!future_salts_.empty() && (future_salts_.back().valid_since < server_time)) {
      server_salt_ = future_salts_.back();
      future_salts_.pop_back();
    }
  }
};

}  // namespace mtproto
}  // namespace td
