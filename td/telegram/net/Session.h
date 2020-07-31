//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/StateManager.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/AuthKey.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/SessionConnection.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/CancellationToken.h"
#include "td/utils/common.h"
#include "td/utils/List.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

namespace mtproto {
class RawConnection;
}  // namespace mtproto

namespace detail {
class GenAuthKeyActor;
}  // namespace detail

class Session final
    : public NetQueryCallback
    , private mtproto::SessionConnection::Callback {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual void on_failed() = 0;
    virtual void on_closed() = 0;
    virtual void request_raw_connection(unique_ptr<mtproto::AuthData> auth_data,
                                        Promise<unique_ptr<mtproto::RawConnection>>) = 0;
    virtual void on_tmp_auth_key_updated(mtproto::AuthKey auth_key) = 0;
    virtual void on_server_salt_updated(std::vector<mtproto::ServerSalt> server_salts) {
    }
    // one still have to call close after on_closed
    virtual void on_result(NetQueryPtr net_query) = 0;
  };

  Session(unique_ptr<Callback> callback, std::shared_ptr<AuthDataShared> shared_auth_data, int32 raw_dc_id, int32 dc_id,
          bool is_main, bool use_pfs, bool is_cdn, bool need_destroy, const mtproto::AuthKey &tmp_auth_key,
          std::vector<mtproto::ServerSalt> server_salts);
  void send(NetQueryPtr &&query);
  void on_network(bool network_flag, uint32 network_generation);
  void on_online(bool online_flag);
  void close();

 private:
  struct Query : private ListNode {
    uint64 container_id;
    NetQueryPtr query;

    bool ack = false;
    bool unknown = false;

    int8 connection_id;
    double sent_at_;
    Query(uint64 message_id, NetQueryPtr &&q, int8 connection_id, double sent_at)
        : container_id(message_id), query(std::move(q)), connection_id(connection_id), sent_at_(sent_at) {
    }

    ListNode *get_list_node() {
      return static_cast<ListNode *>(this);
    }
    static Query *from_list_node(ListNode *list_node) {
      return static_cast<Query *>(list_node);
    }
  };

  // When connection is closed, mark all queries without ack as unknown.
  // Ask state of all unknown queries when new connection is created.
  //
  // Just re-ask answer_id each time we get information about it.
  // Though mtproto::Connection must ensure delivery of such query.

  int32 raw_dc_id_;
  int32 dc_id_;
  enum class Mode : int8 { Tcp, Http } mode_ = Mode::Tcp;
  bool is_main_;
  bool is_cdn_;
  bool need_destroy_;
  bool was_on_network_ = false;
  bool network_flag_ = false;
  uint32 network_generation_ = 0;
  bool online_flag_ = false;
  bool connection_online_flag_ = false;
  uint64 being_binded_tmp_auth_key_id_ = 0;
  uint64 being_checked_main_auth_key_id_ = 0;
  uint64 last_bind_query_id_ = 0;
  uint64 last_check_query_id_ = 0;
  double last_activity_timestamp_ = 0;
  size_t dropped_size_ = 0;

  std::unordered_set<uint64> unknown_queries_;
  std::vector<int64> to_cancel_;

  // Do not invalidate iterators of these two containers!
  // TODO: better data structures
  std::deque<NetQueryPtr> pending_queries_;
  std::map<uint64, Query> sent_queries_;
  std::deque<NetQueryPtr> pending_invoke_after_queries_;
  ListNode sent_queries_list_;

  struct ConnectionInfo {
    int8 connection_id;
    Mode mode;
    enum class State : int8 { Empty, Connecting, Ready } state = State::Empty;
    CancellationTokenSource cancellation_token_source_;
    unique_ptr<mtproto::SessionConnection> connection;
    bool ask_info;
    double wakeup_at = 0;
    double created_at = 0;
  };

  ConnectionInfo *current_info_;
  ConnectionInfo main_connection_;
  ConnectionInfo long_poll_connection_;
  StateManager::ConnectionToken connection_token_;

  double cached_connection_timestamp_ = 0;
  unique_ptr<mtproto::RawConnection> cached_connection_;

  std::shared_ptr<Callback> callback_;
  mtproto::AuthData auth_data_;
  bool use_pfs_{false};
  bool need_check_main_key_{false};
  TempAuthKeyWatchdog::RegisteredAuthKey registered_temp_auth_key_;
  std::shared_ptr<AuthDataShared> shared_auth_data_;
  bool close_flag_ = false;

  static constexpr double ACTIVITY_TIMEOUT = 60 * 5;
  static constexpr size_t MAX_INFLIGHT_QUERIES = 1024;

  struct ContainerInfo {
    size_t ref_cnt;
    std::vector<uint64> message_ids;
  };
  std::unordered_map<uint64, ContainerInfo> sent_containers_;

  friend class GenAuthKeyActor;
  struct HandshakeInfo {
    bool flag_ = false;
    ActorOwn<detail::GenAuthKeyActor> actor_;
    unique_ptr<mtproto::AuthKeyHandshake> handshake_;
  };
  enum HandshakeId : int32 { MainAuthKeyHandshake = 0, TmpAuthKeyHandshake = 1 };
  std::array<HandshakeInfo, 2> handshake_info_;

  double wakeup_at_;
  void on_handshake_ready(Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake);
  void create_gen_auth_key_actor(HandshakeId handshake_id);
  void auth_loop();

  // mtproto::Connection::Callback
  void on_connected() override;
  void on_closed(Status status) override;

  Status on_pong() override;

  void on_auth_key_updated() override;
  void on_tmp_auth_key_updated() override;
  void on_server_salt_updated() override;
  void on_server_time_difference_updated() override;

  void on_session_created(uint64 unique_id, uint64 first_id) override;
  void on_session_failed(Status status) override;

  void on_container_sent(uint64 container_id, vector<uint64> msg_ids) override;

  void on_message_ack(uint64 id) override;
  Status on_message_result_ok(uint64 id, BufferSlice packet, size_t original_size) override;
  void on_message_result_error(uint64 id, int error_code, BufferSlice message) override;
  void on_message_failed(uint64 id, Status status) override;

  void on_message_info(uint64 id, int32 state, uint64 answer_id, int32 answer_size) override;

  Status on_destroy_auth_key() override;

  void flush_pending_invoke_after_queries();
  bool has_queries() const;

  void dec_container(uint64 message_id, Query *query);
  void cleanup_container(uint64 id, Query *query);
  void mark_as_known(uint64 id, Query *query);
  void mark_as_unknown(uint64 id, Query *query);

  void on_message_ack_impl(uint64 id, int32 type);
  void on_message_ack_impl_inner(uint64 id, int32 type, bool in_container);
  void on_message_failed_inner(uint64 id, bool in_container);

  // send NetQueryPtr to parent
  void return_query(NetQueryPtr &&query);
  void add_query(NetQueryPtr &&net_query);
  void resend_query(NetQueryPtr query);

  void connection_open(ConnectionInfo *info, bool ask_info = false);
  void connection_add(unique_ptr<mtproto::RawConnection> raw_connection);
  void connection_check_mode(ConnectionInfo *info);
  void connection_open_finish(ConnectionInfo *info, Result<unique_ptr<mtproto::RawConnection>> r_raw_connection);

  void connection_online_update(bool force = false);
  void connection_close(ConnectionInfo *info);
  void connection_flush(ConnectionInfo *info);
  void connection_send_query(ConnectionInfo *info, NetQueryPtr &&net_query, uint64 message_id = 0);
  bool need_send_bind_key() const;
  bool need_send_query() const;
  bool can_destroy_auth_key() const;
  bool connection_send_bind_key(ConnectionInfo *info);
  bool need_send_check_main_key() const;
  bool connection_send_check_main_key(ConnectionInfo *info);

  void on_result(NetQueryPtr query) override;

  void on_bind_result(NetQueryPtr query);
  void on_check_key_result(NetQueryPtr query);

  void start_up() override;
  void loop() override;
  void hangup() override;
  void raw_event(const Event::Raw &event) override;

  friend StringBuilder &operator<<(StringBuilder &sb, Mode mode) {
    return sb << (mode == Mode::Http ? "Http" : "Tcp");
  }
};

}  // namespace td
