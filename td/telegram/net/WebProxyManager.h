//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/Proxy.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <deque>

namespace td {

class WebProxyListener;
class WebProxyStream;

// A web proxy is a stock MTProxy reached through an HTTPS relay page, which the application loads in a hidden web view.
// MTProto connects to a listener on 127.0.0.1 as if it were the MTProxy; every accepted socket becomes one stream
// of the relay frame protocol, and the frames are exchanged with the page through updates and app requests.
class WebProxyManager final : public Actor {
 public:
  explicit WebProxyManager(ActorShared<> parent);
  WebProxyManager(const WebProxyManager &) = delete;
  WebProxyManager &operator=(const WebProxyManager &) = delete;
  WebProxyManager(WebProxyManager &&) = delete;
  WebProxyManager &operator=(WebProxyManager &&) = delete;
  ~WebProxyManager() final;

  struct TemporaryCarrier {
    int64 endpoint_id = 0;
    IPAddress ip_address;
  };

  // must be called for every socket connected to an address returned by the manager before it can be accepted
  static void register_connection(const SocketFd &socket_fd);

  void set_active_proxy(int32 proxy_id, Proxy proxy);

  void get_active_proxy_address(Promise<IPAddress> promise);

  void on_network(bool network_flag);

  void open_temporary_carrier(Proxy proxy, Promise<TemporaryCarrier> promise);

  void close_temporary_carrier(int64 endpoint_id);

  void process_carrier_message(int32 carrier_id, td_api::object_ptr<td_api::WebProxyCarrierMessage> message,
                               Promise<Unit> promise);

  void report_carrier_error(int32 carrier_id, td_api::object_ptr<td_api::WebProxyCarrierError> error,
                            Promise<Unit> promise);

 private:
  friend class WebProxyListener;
  friend class WebProxyStream;

  static constexpr uint32 INITIAL_WINDOW = 4 << 20;

  enum class CarrierState : int32 { None, WaitInit, WaitWelcome, Ready };

  struct Stream {
    int64 endpoint_id = 0;
    uint32 stream_id = 0;
    ActorOwn<WebProxyStream> actor;
    uint32 receive_window = INITIAL_WINDOW;
    uint32 pending_window = 0;
    bool is_opened = false;
    bool has_received_data = false;
  };

  struct Endpoint {
    int64 endpoint_id = 0;
    int32 proxy_id = 0;
    Proxy proxy;
    bool is_temporary = false;

    ActorOwn<WebProxyListener> listener;
    IPAddress ip_address;
    vector<Promise<IPAddress>> address_promises;
    Promise<TemporaryCarrier> temporary_promise;

    int32 carrier_id = 0;
    CarrierState state = CarrierState::None;
    string nonce;
    double opened_at = 0.0;
    double handshake_deadline = 0.0;
    double unanswered_since = 0.0;
    double window_flush_at = 0.0;

    double retry_at = 0.0;
    double retry_delay = 0.0;
    bool is_paused = false;
    bool is_unsupported = false;

    FlatHashMap<uint32, uint64> streams;
    FlatHashSet<uint32> closed_stream_ids;
    std::deque<uint32> closed_stream_id_order;
    uint32 next_stream_id = 1;

    string output;
    bool is_output_flush_scheduled = false;

    double expires_at = 0.0;
  };

  ActorShared<> parent_;
  bool network_flag_ = true;

  int64 max_endpoint_id_ = 0;
  int64 active_endpoint_id_ = 0;
  FlatHashMap<int64, unique_ptr<Endpoint>> endpoints_;

  int32 max_carrier_id_ = 0;
  FlatHashMap<int32, int64> carrier_endpoint_ids_;

  uint64 max_stream_key_ = 0;
  FlatHashMap<uint64, unique_ptr<Stream>> streams_;

  // loopback peers are accepted only if their port was registered by this TDLib instance
  FlatHashMap<int32, double> expected_ports_;
  struct PendingConnection {
    int64 endpoint_id = 0;
    SocketFd socket_fd;
    int32 port = 0;
    double expires_at = 0.0;
  };
  vector<PendingConnection> pending_connections_;

  void start_up() final;
  void hangup() final;
  void hangup_shared() final;
  void timeout_expired() final;
  void tear_down() final;

  Endpoint *get_endpoint(int64 endpoint_id);
  Endpoint *get_carrier_endpoint(int32 carrier_id);

  int64 create_endpoint(int32 proxy_id, Proxy proxy, bool is_temporary);
  void destroy_endpoint(int64 endpoint_id);
  void on_listener_started(int64 endpoint_id, Result<IPAddress> r_ip_address);
  void close_listener(Endpoint &endpoint, Status error);
  void resolve_temporary_carrier(Endpoint &endpoint);

  void expect_connection(int32 port);
  void on_connection_accepted(int64 endpoint_id, SocketFd socket_fd);
  void accept_connection(int64 endpoint_id, SocketFd socket_fd);

  void open_carrier(Endpoint &endpoint);
  void close_carrier(Endpoint &endpoint);
  void fail_carrier(Endpoint &endpoint, Slice reason, bool is_refused);
  void schedule_retry(Endpoint &endpoint);
  void fail_unknown_carrier(int32 carrier_id, Promise<Unit> promise);

  void on_carrier_text(Endpoint &endpoint, Slice text);
  void on_carrier_binary(Endpoint &endpoint, Slice data);
  Status on_carrier_frame(Endpoint &endpoint, uint8 type, uint32 stream_id, Slice payload);
  void on_carrier_welcome(Endpoint &endpoint);

  void on_stream_data(BufferSlice data);
  void on_stream_written(size_t size);
  void on_stream_closed(uint64 stream_key);
  void open_stream(Endpoint &endpoint, Stream &stream);
  void close_stream(Endpoint &endpoint, uint64 stream_key, bool need_close_frame);
  void close_all_streams(Endpoint &endpoint);
  void remember_closed_stream(Endpoint &endpoint, uint32 stream_id);
  uint32 allocate_stream_id(Endpoint &endpoint);

  void send_frame(Endpoint &endpoint, uint8 type, uint32 stream_id, Slice payload);
  void flush_stream_window(Endpoint &endpoint, Stream &stream);
  void flush_window(Endpoint &endpoint);
  void flush_output(int64 endpoint_id);
  void do_flush_output(Endpoint &endpoint);

  void update_timeout();
};

}  // namespace td
