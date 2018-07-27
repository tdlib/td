//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/crypto.h"

#include "td/utils/buffer.h"
#include "td/utils/int_types.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Storer;

namespace mtproto {

class AuthKeyHandshakeContext {
 public:
  virtual ~AuthKeyHandshakeContext() = default;
  virtual DhCallback *get_dh_callback() = 0;
  virtual PublicRsaKeyInterface *get_public_rsa_key_interface() = 0;
};

class AuthKeyHandshake {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual void send_no_crypto(const Storer &storer) = 0;
  };
  using Context = AuthKeyHandshakeContext;
  enum class Mode { Unknown, Main, Temp };
  AuthKey auth_key;
  double server_time_diff = 0;
  uint64 server_salt = 0;

  bool is_ready_for_start();
  Status start_main(Callback *connection) TD_WARN_UNUSED_RESULT;
  Status start_tmp(Callback *connection, int32 expire_in) TD_WARN_UNUSED_RESULT;

  bool is_ready_for_message(const UInt128 &message_nonce);

  bool is_ready_for_finish();
  void on_finish();

  AuthKeyHandshake(int32 dc_id, int32 expire_in) {
    dc_id_ = dc_id;
    if (expire_in == 0) {
      mode_ = Mode::Main;
    } else {
      mode_ = Mode::Temp;
      expire_in_ = expire_in;
    }
  }
  void init_main() {
    clear();
    mode_ = Mode::Main;
  }
  void init_temp(int32 expire_in) {
    clear();
    mode_ = Mode::Temp;
    expire_in_ = expire_in;
  }
  void resume(Callback *connection);
  Status on_message(Slice message, Callback *connection, Context *context) TD_WARN_UNUSED_RESULT;
  bool is_ready() {
    return is_ready_for_finish();
  }
  void clear();

 private:
  using State = enum { Start, ResPQ, ServerDHParams, DHGenResponse, Finish };
  State state_ = Start;
  Mode mode_ = Mode::Unknown;
  int32 dc_id_;
  int32 expire_in_;
  double expire_at_ = 0;

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
