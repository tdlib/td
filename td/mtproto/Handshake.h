//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/DhHandshake.h"
#include "td/mtproto/RSA.h"

#include "td/utils/buffer.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StorerBase.h"
#include "td/utils/UInt.h"

namespace td {
namespace mtproto {

class AuthKeyHandshakeContext {
 public:
  virtual ~AuthKeyHandshakeContext() = default;
  virtual DhCallback *get_dh_callback() = 0;
  virtual PublicRsaKeyInterface *get_public_rsa_key_interface() = 0;
};

class AuthKeyHandshake {
  enum class Mode { Unknown, Main, Temp };

 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual void send_no_crypto(const Storer &storer) = 0;
  };

  AuthKeyHandshake(int32 dc_id, int32 expires_in) {
    dc_id_ = dc_id;
    if (expires_in == 0) {
      mode_ = Mode::Main;
    } else {
      mode_ = Mode::Temp;
      expires_in_ = expires_in;
    }
  }

  bool is_ready_for_start() const;
  Status start_main(Callback *connection) TD_WARN_UNUSED_RESULT;
  Status start_tmp(Callback *connection, int32 expires_in) TD_WARN_UNUSED_RESULT;

  bool is_ready_for_message(const UInt128 &message_nonce) const;

  bool is_ready_for_finish() const;
  void on_finish();

  void init_main() {
    clear();
    mode_ = Mode::Main;
  }

  void init_temp(int32 expires_in) {
    clear();
    mode_ = Mode::Temp;
    expires_in_ = expires_in;
  }

  void resume(Callback *connection);

  Status on_message(Slice message, Callback *connection, AuthKeyHandshakeContext *context) TD_WARN_UNUSED_RESULT;

  bool is_ready() const {
    return is_ready_for_finish();
  }

  void clear();

  const AuthKey &get_auth_key() const {
    return auth_key_;
  }

  AuthKey release_auth_key() {
    return std::move(auth_key_);
  }

  double get_server_time_diff() const {
    return server_time_diff_;
  }

  uint64 get_server_salt() const {
    return server_salt_;
  }

 private:
  using State = enum { Start, ResPQ, ServerDHParams, DHGenResponse, Finish };
  State state_ = Start;
  Mode mode_ = Mode::Unknown;
  int32 dc_id_ = 0;
  int32 expires_in_ = 0;
  double expires_at_ = 0;

  AuthKey auth_key_;
  double server_time_diff_ = 0;
  uint64 server_salt_ = 0;

  UInt128 nonce;
  UInt128 server_nonce;
  UInt256 new_nonce;
  UInt256 tmp_aes_key;
  UInt256 tmp_aes_iv;

  BufferSlice last_query_;

  template <class DataT>
  Result<size_t> fill_data_with_hash(uint8 *data_with_hash, const DataT &data) TD_WARN_UNUSED_RESULT;

  void send(Callback *connection, const Storer &storer);
  void do_send(Callback *connection, const Storer &storer);

  Status on_start(Callback *connection) TD_WARN_UNUSED_RESULT;
  Status on_res_pq(Slice message, Callback *connection, PublicRsaKeyInterface *public_rsa_key) TD_WARN_UNUSED_RESULT;
  Status on_server_dh_params(Slice message, Callback *connection, DhCallback *dh_callback) TD_WARN_UNUSED_RESULT;
  Status on_dh_gen_response(Slice message, Callback *connection) TD_WARN_UNUSED_RESULT;
};

}  // namespace mtproto
}  // namespace td
