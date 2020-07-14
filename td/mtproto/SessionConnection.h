//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/Query.h"
#include "td/mtproto/RawConnection.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/Named.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StorerBase.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_parsers.h"

#include <unordered_map>
#include <utility>

namespace td {
namespace mtproto_api {

class rpc_error;
class new_session_created;
class bad_msg_notification;
class bad_server_salt;
class msgs_ack;
class gzip_packed;
class pong;
class future_salts;
class msgs_state_info;
class msgs_all_info;
class msg_detailed_info;
class msg_new_detailed_info;
class DestroyAuthKeyRes;
class destroy_auth_key_ok;
class destroy_auth_key_fail;
class destroy_auth_key_none;
}  // namespace mtproto_api

namespace mtproto {

class AuthData;

struct MsgInfo {
  uint64 session_id;
  int64 message_id;
  int32 seq_no;
  size_t size;
};

inline StringBuilder &operator<<(StringBuilder &stream, const MsgInfo &id) {
  return stream << "[session_id:" << format::as_hex(id.session_id) << "] [msg_id:" << format::as_hex(id.message_id)
                << "] [seq_no:" << format::as_hex(id.seq_no) << "]";
}

class SessionConnection
    : public Named
    , private RawConnection::Callback {
 public:
  enum class Mode { Tcp, Http, HttpLongPoll };
  SessionConnection(Mode mode, unique_ptr<RawConnection> raw_connection, AuthData *auth_data);

  PollableFdInfo &get_poll_info();
  unique_ptr<RawConnection> move_as_raw_connection();

  // Interface
  Result<uint64> TD_WARN_UNUSED_RESULT send_query(BufferSlice buffer, bool gzip_flag, int64 message_id = 0,
                                                  uint64 invoke_after_id = 0, bool use_quick_ack = false);
  std::pair<uint64, BufferSlice> encrypted_bind(int64 perm_key, int64 nonce, int32 expires_at);

  void get_state_info(int64 message_id);
  void resend_answer(int64 message_id);
  void cancel_answer(int64 message_id);
  void destroy_key();

  void set_online(bool online_flag, bool is_main);

  // Callback
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;

    virtual void on_connected() = 0;
    virtual void on_closed(Status status) = 0;

    virtual void on_auth_key_updated() = 0;
    virtual void on_tmp_auth_key_updated() = 0;
    virtual void on_server_salt_updated() = 0;
    virtual void on_server_time_difference_updated() = 0;

    virtual void on_session_created(uint64 unique_id, uint64 first_id) = 0;
    virtual void on_session_failed(Status status) = 0;

    virtual void on_container_sent(uint64 container_id, vector<uint64> msgs_id) = 0;
    virtual Status on_pong() = 0;

    virtual void on_message_ack(uint64 id) = 0;
    virtual Status on_message_result_ok(uint64 id, BufferSlice packet, size_t original_size) = 0;
    virtual void on_message_result_error(uint64 id, int code, BufferSlice descr) = 0;
    virtual void on_message_failed(uint64 id, Status status) = 0;
    virtual void on_message_info(uint64 id, int32 state, uint64 answer_id, int32 answer_size) = 0;

    virtual Status on_destroy_auth_key() = 0;
  };

  double flush(SessionConnection::Callback *callback);

  // NB: Do not call force_close after on_closed callback
  void force_close(SessionConnection::Callback *callback);

 private:
  static constexpr int ACK_DELAY = 30;                  // 30s
  static constexpr double QUERY_DELAY = 0.001;          // 0.001s
  static constexpr double RESEND_ANSWER_DELAY = 0.001;  // 0.001s

  bool online_flag_ = false;
  bool is_main_ = false;

  int rtt() const {
    return max(2, static_cast<int>(raw_connection_->rtt_ * 1.5 + 1));
  }

  int32 read_disconnect_delay() const {
    return online_flag_ ? rtt() * 7 / 2 : 135;
  }

  int32 ping_disconnect_delay() const {
    return (online_flag_ && is_main_) ? rtt() * 5 / 2 : 135;
  }

  int32 ping_may_delay() const {
    return online_flag_ ? rtt() / 2 : 30;
  }

  int32 ping_must_delay() const {
    return online_flag_ ? rtt() : 60;
  }

  int http_max_wait() const {
    return 25 * 1000;  // 25s. Longer could be closed by proxy
  }
  static constexpr int HTTP_MAX_AFTER = 10;              // 0.01s
  static constexpr int HTTP_MAX_DELAY = 30;              // 0.03s
  static constexpr int TEMP_KEY_TIMEOUT = 60 * 60 * 24;  // one day

  vector<MtprotoQuery> to_send_;
  vector<int64> to_ack_;
  double force_send_at_ = 0;

  struct ServiceQuery {
    enum Type { GetStateInfo, ResendAnswer } type;
    std::vector<int64> message_ids;
  };
  std::vector<int64> to_resend_answer_;
  std::vector<int64> to_cancel_answer_;
  std::vector<int64> to_get_state_info_;
  std::unordered_map<uint64, ServiceQuery> service_queries_;

  // nobody cleans up this map. But it should be really small.
  std::unordered_map<uint64, std::vector<uint64>> container_to_service_msg_;

  double last_read_at_ = 0;
  double last_ping_at_ = 0;
  double last_pong_at_ = 0;
  int64 cur_ping_id_ = 0;
  uint64 last_ping_message_id_ = 0;
  uint64 last_ping_container_id_ = 0;

  bool need_destroy_auth_key_{false};
  bool sent_destroy_auth_key_{false};

  double wakeup_at_ = 0;
  double flush_packet_at_ = 0;

  double last_get_future_salt_at_ = 0;
  enum { Init, Run, Fail, Closed } state_;
  Mode mode_;
  bool connected_flag_ = false;

  uint64 container_id_ = 0;
  int64 main_message_id_ = 0;
  double created_at_ = 0;

  unique_ptr<RawConnection> raw_connection_;
  AuthData *auth_data_;
  SessionConnection::Callback *callback_ = nullptr;
  BufferSlice *current_buffer_slice_;

  friend class OnPacket;

  BufferSlice as_buffer_slice(Slice packet);
  auto set_buffer_slice(BufferSlice *buffer_slice) TD_WARN_UNUSED_RESULT {
    auto old_buffer_slice = current_buffer_slice_;
    current_buffer_slice_ = buffer_slice;
    return ScopeExit() + [&to = current_buffer_slice_, from = old_buffer_slice] {
      to = from;
    };
  }

  Status parse_message(TlParser &parser, MsgInfo *info, Slice *packet, bool crypto_flag = true) TD_WARN_UNUSED_RESULT;
  Status parse_packet(TlParser &parser) TD_WARN_UNUSED_RESULT;
  Status on_packet_container(const MsgInfo &info, Slice packet) TD_WARN_UNUSED_RESULT;
  Status on_packet_rpc_result(const MsgInfo &info, Slice packet) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, uint64 req_msg_id,
                   const mtproto_api::rpc_error &rpc_error) TD_WARN_UNUSED_RESULT;

  template <class T>
  Status on_packet(const MsgInfo &info, const T &packet) TD_WARN_UNUSED_RESULT;

  Status on_packet(const MsgInfo &info, const mtproto_api::rpc_error &rpc_error) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info,
                   const mtproto_api::new_session_created &new_session_created) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info,
                   const mtproto_api::bad_msg_notification &bad_msg_notification) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::bad_server_salt &bad_server_salt) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::msgs_ack &msgs_ack) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::gzip_packed &gzip_packed) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::pong &pong) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::future_salts &salts) TD_WARN_UNUSED_RESULT;

  Status on_msgs_state_info(const std::vector<int64> &ids, Slice info) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::msgs_state_info &msgs_state_info) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::msgs_all_info &msgs_all_info) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::msg_detailed_info &msg_detailed_info) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info,
                   const mtproto_api::msg_new_detailed_info &msg_new_detailed_info) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info, const mtproto_api::destroy_auth_key_ok &destroy_auth_key) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info,
                   const mtproto_api::destroy_auth_key_none &destroy_auth_key) TD_WARN_UNUSED_RESULT;
  Status on_packet(const MsgInfo &info,
                   const mtproto_api::destroy_auth_key_fail &destroy_auth_key) TD_WARN_UNUSED_RESULT;
  Status on_destroy_auth_key(const mtproto_api::DestroyAuthKeyRes &destroy_auth_key);

  Status on_slice_packet(const MsgInfo &info, Slice packet) TD_WARN_UNUSED_RESULT;
  Status on_main_packet(const PacketInfo &info, Slice packet) TD_WARN_UNUSED_RESULT;
  void on_message_failed(uint64 id, Status status);
  void on_message_failed_inner(uint64 id);

  void do_close(Status status);

  void send_ack(uint64 message_id);
  void send_crypto(const Storer &storer, uint64 quick_ack_token);
  void send_before(double tm);
  bool may_ping() const;
  bool must_ping() const;
  bool must_flush_packet();
  void flush_packet();

  Status init() TD_WARN_UNUSED_RESULT;
  Status do_flush() TD_WARN_UNUSED_RESULT;

  Status before_write() override TD_WARN_UNUSED_RESULT;
  Status on_raw_packet(const PacketInfo &info, BufferSlice packet) override;
  Status on_quick_ack(uint64 quick_ack_token) override;
  void on_read(size_t size) override;
};

}  // namespace mtproto
}  // namespace td
