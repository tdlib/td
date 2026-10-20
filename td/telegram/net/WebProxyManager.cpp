//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/WebProxyManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/ServerSocketFd.h"
#include "td/utils/Random.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"

#include <limits>

namespace td {

namespace {

// the relay frame protocol: type:u8 | stream_id:u24 | length:u32 | payload, all integers big-endian
constexpr uint8 FRAME_OPEN = 0x01;
constexpr uint8 FRAME_DATA = 0x02;
constexpr uint8 FRAME_CLOSE = 0x03;
constexpr uint8 FRAME_WINDOW = 0x04;
constexpr uint8 FRAME_PING = 0x05;
constexpr uint8 FRAME_PONG = 0x06;
constexpr uint8 FRAME_HELLO = 0x10;
constexpr uint8 FRAME_WELCOME = 0x11;
constexpr uint8 FRAME_BYE = 0x1f;

constexpr size_t FRAME_HEADER_SIZE = 8;
constexpr size_t MAX_FRAME_PAYLOAD_SIZE = 1 << 20;
constexpr size_t MAX_PING_PAYLOAD_SIZE = 64;
constexpr size_t MAX_CARRIER_DATA_SIZE = 1 << 20;
constexpr size_t MAX_CARRIER_TEXT_SIZE = 1 << 16;
constexpr size_t DATA_CHUNK_SIZE = 1 << 16;
constexpr uint32 MAX_STREAM_ID = (1 << 24) - 1;
constexpr size_t MAX_STREAMS = 64;
constexpr size_t MAX_CLOSED_STREAM_IDS = 4096;
constexpr uint32 WINDOW_FLUSH_SIZE = 256 << 10;
constexpr double WINDOW_FLUSH_DELAY = 0.02;

constexpr double HANDSHAKE_TIMEOUT = 10.0;
constexpr double MAX_HANDSHAKE_TIME = 45.0;
constexpr double MAX_TEMPORARY_HANDSHAKE_TIME = 10.0;
constexpr double RESPONSE_TIMEOUT = 10.0;
constexpr double MIN_RETRY_DELAY = 2.0;
constexpr double MAX_RETRY_DELAY = 30.0;
constexpr double PAUSED_RETRY_DELAY = 30.0;
constexpr double TEMPORARY_ENDPOINT_LIFETIME = 120.0;
constexpr double PENDING_CONNECTION_TIMEOUT = 5.0;
constexpr double EXPECTED_PORT_TIMEOUT = 60.0;
constexpr double CLOSING_STREAM_TIMEOUT = 5.0;

constexpr Slice BRIDGE_OBJECT_NAME("TelegramWebProxy");

void send_web_proxy_update(td_api::object_ptr<td_api::Update> update) {
  send_closure(G()->td(), &Td::send_update, std::move(update));
}

uint32 read_uint32(const unsigned char *data) {
  return (static_cast<uint32>(data[0]) << 24) | (static_cast<uint32>(data[1]) << 16) |
         (static_cast<uint32>(data[2]) << 8) | static_cast<uint32>(data[3]);
}

string get_json_string_field(const JsonObject &object, Slice name) {
  auto r_value = object.get_optional_string_field(name);
  return r_value.is_ok() ? r_value.move_as_ok() : string();
}

}  // namespace

class WebProxyListener final : public Actor {
 public:
  WebProxyListener(ActorId<WebProxyManager> manager, int64 endpoint_id)
      : manager_(std::move(manager)), endpoint_id_(endpoint_id) {
  }

 private:
  ActorId<WebProxyManager> manager_;
  int64 endpoint_id_;
  ServerSocketFd server_fd_;

  void start_up() final {
    auto r_server_fd = ServerSocketFd::open(0, CSlice("127.0.0.1"));
    if (r_server_fd.is_error()) {
      send_closure(manager_, &WebProxyManager::on_listener_started, endpoint_id_, r_server_fd.move_as_error());
      return stop();
    }
    server_fd_ = r_server_fd.move_as_ok();
    Scheduler::subscribe(server_fd_.get_poll_info().extract_pollable_fd(this));

    IPAddress ip_address;
    auto status = ip_address.init_socket_address(server_fd_);
    if (status.is_error()) {
      send_closure(manager_, &WebProxyManager::on_listener_started, endpoint_id_, std::move(status));
      return stop();
    }
    send_closure(manager_, &WebProxyManager::on_listener_started, endpoint_id_, std::move(ip_address));
  }

  void tear_down() final {
    if (!server_fd_.empty()) {
      Scheduler::unsubscribe_before_close(server_fd_.get_poll_info().get_pollable_fd_ref());
      server_fd_.close();
    }
  }

  void hangup() final {
    stop();
  }

  void loop() final {
    sync_with_poll(server_fd_);
    while (can_read_local(server_fd_)) {
      auto r_socket_fd = server_fd_.accept();
      if (r_socket_fd.is_error()) {
        if (r_socket_fd.error().code() != -1) {
          LOG(WARNING) << "Failed to accept web proxy connection: " << r_socket_fd.error();
        }
        continue;
      }
      send_closure(manager_, &WebProxyManager::on_connection_accepted, endpoint_id_, r_socket_fd.move_as_ok());
    }
    if (can_close_local(server_fd_)) {
      send_closure(manager_, &WebProxyManager::on_listener_started, endpoint_id_,
                   Status::Error("Web proxy listener was closed"));
      stop();
    }
  }
};

// One accepted loopback socket. It reads only within the send window granted by the relay,
// and reports written bytes so that the manager can return receive window to the relay.
class WebProxyStream final : public Actor {
 public:
  WebProxyStream(SocketFd socket_fd, ActorShared<WebProxyManager> manager)
      : fd_(std::move(socket_fd)), manager_(std::move(manager)) {
  }

  void open() {
    is_opened_ = true;
    loop();
  }

  void add_send_window(uint32 amount) {
    send_window_ = min(send_window_ + amount, static_cast<uint64>(std::numeric_limits<uint32>::max()));
    loop();
  }

  void write(BufferSlice data) {
    fd_.output_buffer().append(std::move(data));
    loop();
  }

  // the relay may send the last data and CLOSE in one message, so the data is written out before the socket is closed
  void close() {
    is_opened_ = false;
    is_closing_ = true;
    set_timeout_in(CLOSING_STREAM_TIMEOUT);
    loop();
  }

 private:
  BufferedFd<SocketFd> fd_;
  ActorShared<WebProxyManager> manager_;
  bool is_opened_ = false;
  bool is_closing_ = false;
  uint64 send_window_ = WebProxyManager::INITIAL_WINDOW;

  void start_up() final {
    Scheduler::subscribe(fd_.get_poll_info().extract_pollable_fd(this));
    loop();
  }

  void tear_down() final {
    Scheduler::unsubscribe_before_close(fd_.get_poll_info().get_pollable_fd_ref());
    fd_.close();
  }

  void hangup() final {
    stop();
  }

  void timeout_expired() final {
    stop();
  }

  void loop() final {
    sync_with_poll(fd_);
    auto status = [&] {
      if (is_opened_ && send_window_ > 0) {
        TRY_STATUS(fd_.flush_read(static_cast<size_t>(min(send_window_, static_cast<uint64>(4 * DATA_CHUNK_SIZE)))));
        auto &input = fd_.input_buffer();
        while (!input.empty()) {
          auto size = min(input.size(), DATA_CHUNK_SIZE);
          send_window_ -= size;
          send_closure(manager_, &WebProxyManager::on_stream_data, input.cut_head(size).move_as_buffer_slice());
        }
      } else if (is_closing_) {
        // closing a socket with unread data resets the connection, and the peer may then drop the data written to it
        TRY_STATUS(fd_.flush_read());
        auto &input = fd_.input_buffer();
        input.advance(input.size());
      }
      TRY_RESULT(written_size, fd_.flush_write());
      if (written_size > 0) {
        send_closure(manager_, &WebProxyManager::on_stream_written, written_size);
      }
      if (can_close_local(fd_)) {
        return Status::Error("Connection closed");
      }
      return Status::OK();
    }();
    if (status.is_error() || (is_closing_ && !fd_.need_flush_write())) {
      return stop();
    }
    if (is_opened_ && send_window_ > 0 && can_read_local(fd_)) {
      // the read was limited, so no new poll event will come for the data left in the socket
      yield();
    }
  }
};

WebProxyManager::WebProxyManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

WebProxyManager::~WebProxyManager() = default;

void WebProxyManager::register_connection(const SocketFd &socket_fd) {
  IPAddress ip_address;
  auto status = ip_address.init_socket_address(socket_fd);
  if (status.is_error()) {
    LOG(WARNING) << "Failed to get web proxy connection address: " << status;
    return;
  }
  send_closure(G()->web_proxy_manager(), &WebProxyManager::expect_connection, ip_address.get_port());
}

void WebProxyManager::start_up() {
}

void WebProxyManager::hangup() {
  vector<int64> endpoint_ids;
  for (auto &it : endpoints_) {
    endpoint_ids.push_back(it.first);
  }
  for (auto endpoint_id : endpoint_ids) {
    destroy_endpoint(endpoint_id);
  }
  pending_connections_.clear();
  stop();
}

void WebProxyManager::hangup_shared() {
  on_stream_closed(get_link_token());
}

void WebProxyManager::tear_down() {
  parent_.reset();
}

WebProxyManager::Endpoint *WebProxyManager::get_endpoint(int64 endpoint_id) {
  auto it = endpoints_.find(endpoint_id);
  if (it == endpoints_.end()) {
    return nullptr;
  }
  return it->second.get();
}

WebProxyManager::Endpoint *WebProxyManager::get_carrier_endpoint(int32 carrier_id) {
  auto it = carrier_endpoint_ids_.find(carrier_id);
  if (it == carrier_endpoint_ids_.end()) {
    return nullptr;
  }
  return get_endpoint(it->second);
}

void WebProxyManager::set_active_proxy(int32 proxy_id, Proxy proxy) {
  auto endpoint = get_endpoint(active_endpoint_id_);
  if (endpoint != nullptr && proxy.use_web_proxy() && endpoint->proxy_id == proxy_id && endpoint->proxy == proxy) {
    return;
  }
  if (active_endpoint_id_ != 0) {
    destroy_endpoint(active_endpoint_id_);
    active_endpoint_id_ = 0;
  }
  if (proxy.use_web_proxy()) {
    active_endpoint_id_ = create_endpoint(proxy_id, std::move(proxy), false);
  }
}

void WebProxyManager::get_active_proxy_address(Promise<IPAddress> promise) {
  auto endpoint = get_endpoint(active_endpoint_id_);
  if (endpoint == nullptr) {
    return promise.set_error(400, "Web proxy is not enabled");
  }
  if (endpoint->is_unsupported) {
    return promise.set_error(400, "Web proxy is unsupported by the application");
  }
  if (endpoint->ip_address.is_valid()) {
    return promise.set_value(IPAddress(endpoint->ip_address));
  }
  endpoint->address_promises.push_back(std::move(promise));
  if (endpoint->listener.empty()) {
    endpoint->listener = create_actor<WebProxyListener>("WebProxyListener", actor_id(this), endpoint->endpoint_id);
  }
}

void WebProxyManager::on_network(bool network_flag) {
  network_flag_ = network_flag;
  if (!network_flag) {
    return;
  }
  auto endpoint = get_endpoint(active_endpoint_id_);
  if (endpoint != nullptr && endpoint->carrier_id == 0 && !endpoint->is_unsupported) {
    endpoint->retry_delay = 0.0;
    open_carrier(*endpoint);
  }
}

void WebProxyManager::open_temporary_carrier(Proxy proxy, Promise<TemporaryCarrier> promise) {
  if (!proxy.use_web_proxy()) {
    return promise.set_error(400, "Proxy is not a web proxy");
  }
  auto endpoint_id = create_endpoint(0, std::move(proxy), true);
  auto endpoint = get_endpoint(endpoint_id);
  CHECK(endpoint != nullptr);
  endpoint->temporary_promise = std::move(promise);
}

void WebProxyManager::close_temporary_carrier(int64 endpoint_id) {
  auto endpoint = get_endpoint(endpoint_id);
  if (endpoint != nullptr && endpoint->is_temporary) {
    destroy_endpoint(endpoint_id);
  }
}

int64 WebProxyManager::create_endpoint(int32 proxy_id, Proxy proxy, bool is_temporary) {
  auto endpoint_id = ++max_endpoint_id_;
  auto endpoint = make_unique<Endpoint>();
  endpoint->endpoint_id = endpoint_id;
  endpoint->proxy_id = proxy_id;
  endpoint->proxy = std::move(proxy);
  endpoint->is_temporary = is_temporary;
  if (is_temporary) {
    endpoint->expires_at = Time::now() + TEMPORARY_ENDPOINT_LIFETIME;
  }
  endpoint->listener = create_actor<WebProxyListener>("WebProxyListener", actor_id(this), endpoint_id);
  auto &endpoint_ref = *endpoint;
  endpoints_.emplace(endpoint_id, std::move(endpoint));
  if (is_temporary || network_flag_) {
    open_carrier(endpoint_ref);
  }
  update_timeout();
  return endpoint_id;
}

void WebProxyManager::destroy_endpoint(int64 endpoint_id) {
  auto it = endpoints_.find(endpoint_id);
  if (it == endpoints_.end()) {
    return;
  }
  auto endpoint = std::move(it->second);
  endpoints_.erase(it);
  if (active_endpoint_id_ == endpoint_id) {
    active_endpoint_id_ = 0;
  }

  close_carrier(*endpoint);
  close_all_streams(*endpoint);
  td::remove_if(pending_connections_,
                [endpoint_id](const PendingConnection &connection) { return connection.endpoint_id == endpoint_id; });
  fail_promises(endpoint->address_promises, Status::Error(400, "Web proxy has been disabled"));
  if (endpoint->temporary_promise) {
    endpoint->temporary_promise.set_error(400, "WEB_PROXY_CARRIER_FAILED");
  }
  update_timeout();
}

void WebProxyManager::on_listener_started(int64 endpoint_id, Result<IPAddress> r_ip_address) {
  auto endpoint = get_endpoint(endpoint_id);
  if (endpoint == nullptr) {
    return;
  }
  if (r_ip_address.is_error()) {
    LOG(WARNING) << "Failed to listen for web proxy connections: " << r_ip_address.error();
    close_listener(*endpoint, Status::Error(400, "Failed to start web proxy"));
    if (endpoint->is_temporary && endpoint->temporary_promise) {
      endpoint->temporary_promise.set_error(400, "WEB_PROXY_CARRIER_FAILED");
      send_closure_later(actor_id(this), &WebProxyManager::close_temporary_carrier, endpoint_id);
    }
    return;
  }

  endpoint->ip_address = r_ip_address.move_as_ok();
  auto promises = std::move(endpoint->address_promises);
  reset_to_empty(endpoint->address_promises);
  for (auto &promise : promises) {
    promise.set_value(IPAddress(endpoint->ip_address));
  }
  resolve_temporary_carrier(*endpoint);
}

void WebProxyManager::close_listener(Endpoint &endpoint, Status error) {
  endpoint.listener.reset();
  auto endpoint_id = endpoint.endpoint_id;
  td::remove_if(pending_connections_,
                [endpoint_id](const PendingConnection &connection) { return connection.endpoint_id == endpoint_id; });
  fail_promises(endpoint.address_promises, std::move(error));
  if (endpoint.ip_address.is_valid()) {
    if (!endpoint.is_temporary) {
      // otherwise ConnectionCreator keeps connecting to the closed port until it requests the address again
      send_closure(G()->connection_creator(), &ConnectionCreator::on_web_proxy_address_closed, endpoint.ip_address);
    }
    endpoint.ip_address = IPAddress();
  }
}

void WebProxyManager::resolve_temporary_carrier(Endpoint &endpoint) {
  if (!endpoint.is_temporary || !endpoint.temporary_promise || endpoint.state != CarrierState::Ready ||
      !endpoint.ip_address.is_valid()) {
    return;
  }
  TemporaryCarrier result;
  result.endpoint_id = endpoint.endpoint_id;
  result.ip_address = endpoint.ip_address;
  endpoint.temporary_promise.set_value(std::move(result));
}

void WebProxyManager::expect_connection(int32 port) {
  for (auto it = pending_connections_.begin(); it != pending_connections_.end(); ++it) {
    if (it->port == port) {
      auto endpoint_id = it->endpoint_id;
      auto socket_fd = std::move(it->socket_fd);
      pending_connections_.erase(it);
      return accept_connection(endpoint_id, std::move(socket_fd));
    }
  }
  expected_ports_[port] = Time::now() + EXPECTED_PORT_TIMEOUT;
  update_timeout();
}

void WebProxyManager::on_connection_accepted(int64 endpoint_id, SocketFd socket_fd) {
  IPAddress peer_address;
  auto status = peer_address.init_peer_address(socket_fd);
  if (status.is_error() || !peer_address.is_ipv4() || peer_address.get_ip_str() != "127.0.0.1") {
    LOG(WARNING) << "Drop web proxy connection from an unexpected address";
    return;
  }

  auto port = peer_address.get_port();
  auto it = expected_ports_.find(port);
  if (it != expected_ports_.end()) {
    expected_ports_.erase(it);
    return accept_connection(endpoint_id, std::move(socket_fd));
  }

  // the connecting side registers the port after connect, so the connection may be accepted first
  PendingConnection connection;
  connection.endpoint_id = endpoint_id;
  connection.socket_fd = std::move(socket_fd);
  connection.port = port;
  connection.expires_at = Time::now() + PENDING_CONNECTION_TIMEOUT;
  pending_connections_.push_back(std::move(connection));
  update_timeout();
}

void WebProxyManager::accept_connection(int64 endpoint_id, SocketFd socket_fd) {
  auto endpoint = get_endpoint(endpoint_id);
  if (endpoint == nullptr) {
    return;
  }
  if (endpoint->streams.size() >= MAX_STREAMS) {
    LOG(WARNING) << "Too many web proxy streams";
    return;
  }

  auto stream_key = ++max_stream_key_;
  auto stream = make_unique<Stream>();
  stream->endpoint_id = endpoint_id;
  stream->stream_id = allocate_stream_id(*endpoint);
  stream->actor = create_actor<WebProxyStream>("WebProxyStream", std::move(socket_fd), actor_shared(this, stream_key));
  endpoint->streams.emplace(stream->stream_id, stream_key);
  auto &stream_ref = *stream;
  streams_.emplace(stream_key, std::move(stream));
  if (endpoint->state == CarrierState::Ready) {
    open_stream(*endpoint, stream_ref);
  }
}

uint32 WebProxyManager::allocate_stream_id(Endpoint &endpoint) {
  while (true) {
    auto stream_id = endpoint.next_stream_id;
    endpoint.next_stream_id = stream_id >= MAX_STREAM_ID ? 1 : stream_id + 1;
    if (endpoint.streams.count(stream_id) == 0 && endpoint.closed_stream_ids.count(stream_id) == 0) {
      return stream_id;
    }
  }
}

void WebProxyManager::open_carrier(Endpoint &endpoint) {
  if (endpoint.carrier_id != 0) {
    return;
  }
  if (max_carrier_id_ == std::numeric_limits<int32>::max()) {
    max_carrier_id_ = 0;
  }
  auto carrier_id = ++max_carrier_id_;
  endpoint.carrier_id = carrier_id;
  carrier_endpoint_ids_[carrier_id] = endpoint.endpoint_id;

  string nonce(32, '\0');
  Random::secure_bytes(nonce);
  endpoint.nonce = base64url_encode(nonce);
  endpoint.state = CarrierState::WaitInit;
  endpoint.opened_at = Time::now();
  endpoint.handshake_deadline = endpoint.opened_at + HANDSHAKE_TIMEOUT;
  endpoint.unanswered_since = 0.0;
  endpoint.window_flush_at = 0.0;
  endpoint.retry_at = 0.0;
  endpoint.is_paused = false;
  endpoint.closed_stream_ids.clear();
  endpoint.closed_stream_id_order.clear();
  endpoint.output.clear();

  const auto &proxy = endpoint.proxy;
  string origin = PSTRING() << "https://" << proxy.server();
  string path_prefix = "/";
  if (!proxy.path().empty()) {
    path_prefix = PSTRING() << '/' << proxy.path() << '/';
  }
  // "android" is the relay's frozen v1 name for its injected-bridge mode, used by every client;
  // the page reads the nonce from the fragment and returns it in its init message
  string url = PSTRING() << origin << path_prefix << "?bridge=" << proxy.get_web_proxy_bridge_capability()
                         << "#android=" << endpoint.nonce;
  LOG(INFO) << "Open web proxy carrier " << carrier_id << " for " << proxy;
  send_web_proxy_update(
      td_api::make_object<td_api::updateWebProxyCarrierOpened>(td_api::make_object<td_api::webProxyCarrier>(
          carrier_id, endpoint.proxy_id, std::move(url), std::move(origin), std::move(path_prefix),
          BRIDGE_OBJECT_NAME.str(), endpoint.is_temporary)));
  update_timeout();
}

void WebProxyManager::close_carrier(Endpoint &endpoint) {
  if (endpoint.carrier_id == 0) {
    return;
  }
  auto carrier_id = endpoint.carrier_id;
  LOG(INFO) << "Close web proxy carrier " << carrier_id;

  close_all_streams(endpoint);
  endpoint.output.clear();
  send_web_proxy_update(td_api::make_object<td_api::updateWebProxyCarrierClosed>(carrier_id));

  carrier_endpoint_ids_.erase(carrier_id);
  endpoint.carrier_id = 0;
  endpoint.state = CarrierState::None;
  endpoint.nonce.clear();
  endpoint.handshake_deadline = 0.0;
  endpoint.unanswered_since = 0.0;
  endpoint.window_flush_at = 0.0;
  endpoint.closed_stream_ids.clear();
  endpoint.closed_stream_id_order.clear();
  update_timeout();
}

void WebProxyManager::fail_carrier(Endpoint &endpoint, Slice reason, bool is_refused) {
  if (endpoint.carrier_id == 0) {
    return;
  }
  LOG(INFO) << "Web proxy carrier " << endpoint.carrier_id << " failed: " << reason;
  close_carrier(endpoint);

  if (endpoint.is_temporary) {
    if (endpoint.temporary_promise) {
      endpoint.temporary_promise.set_error(
          400, is_refused ? Slice("WEB_PROXY_CARRIER_REFUSED") : Slice("WEB_PROXY_CARRIER_FAILED"));
      // the caller may still use the endpoint, so it is destroyed later
      send_closure_later(actor_id(this), &WebProxyManager::close_temporary_carrier, endpoint.endpoint_id);
    }
    return;
  }

  schedule_retry(endpoint);
}

void WebProxyManager::schedule_retry(Endpoint &endpoint) {
  if (endpoint.is_unsupported) {
    endpoint.retry_at = 0.0;
  } else if (endpoint.is_paused) {
    endpoint.retry_at = Time::now() + PAUSED_RETRY_DELAY;
  } else {
    endpoint.retry_delay =
        endpoint.retry_delay == 0.0 ? MIN_RETRY_DELAY : min(endpoint.retry_delay * 2, MAX_RETRY_DELAY);
    endpoint.retry_at = Time::now() + endpoint.retry_delay;
  }
  update_timeout();
}

void WebProxyManager::process_carrier_message(int32 carrier_id,
                                              td_api::object_ptr<td_api::WebProxyCarrierMessage> message,
                                              Promise<Unit> promise) {
  if (message == nullptr) {
    return promise.set_error(400, "Message must be non-empty");
  }
  auto endpoint = get_carrier_endpoint(carrier_id);
  if (endpoint == nullptr) {
    return fail_unknown_carrier(carrier_id, std::move(promise));
  }
  promise.set_value(Unit());

  switch (message->get_id()) {
    case td_api::webProxyCarrierMessageText::ID:
      on_carrier_text(*endpoint, static_cast<const td_api::webProxyCarrierMessageText *>(message.get())->text_);
      break;
    case td_api::webProxyCarrierMessageBinary::ID:
      on_carrier_binary(*endpoint, static_cast<const td_api::webProxyCarrierMessageBinary *>(message.get())->data_);
      break;
    default:
      UNREACHABLE();
  }
  update_timeout();
}

void WebProxyManager::fail_unknown_carrier(int32 carrier_id, Promise<Unit> promise) {
  // the application may still be passing messages of a carrier that is already closed
  if (carrier_id > 0 && carrier_id <= max_carrier_id_) {
    return promise.set_value(Unit());
  }
  promise.set_error(400, "WEB_PROXY_CARRIER_NOT_FOUND");
}

void WebProxyManager::report_carrier_error(int32 carrier_id, td_api::object_ptr<td_api::WebProxyCarrierError> error,
                                           Promise<Unit> promise) {
  if (error == nullptr) {
    return promise.set_error(400, "Error must be non-empty");
  }
  auto endpoint = get_carrier_endpoint(carrier_id);
  if (endpoint == nullptr) {
    return fail_unknown_carrier(carrier_id, std::move(promise));
  }
  promise.set_value(Unit());

  switch (error->get_id()) {
    case td_api::webProxyCarrierErrorUnsupported::ID:
      endpoint->is_unsupported = true;
      if (!endpoint->is_temporary) {
        // fail MTProto connections at once instead of leaving them to wait for a carrier that will never open
        close_listener(*endpoint, Status::Error(400, "Web proxy is unsupported by the application"));
      }
      return fail_carrier(*endpoint, "web views are unsupported", false);
    case td_api::webProxyCarrierErrorPaused::ID:
      endpoint->is_paused = true;
      return fail_carrier(*endpoint, "paused", false);
    case td_api::webProxyCarrierErrorLoadFailed::ID:
      return fail_carrier(
          *endpoint,
          PSLICE() << "page load failed: "
                   << static_cast<const td_api::webProxyCarrierErrorLoadFailed *>(error.get())->message_,
          false);
    case td_api::webProxyCarrierErrorTerminated::ID:
      return fail_carrier(*endpoint, "web view was terminated", false);
    case td_api::webProxyCarrierErrorRefused::ID:
      return fail_carrier(*endpoint, "refused", true);
    default:
      UNREACHABLE();
  }
}

void WebProxyManager::on_carrier_text(Endpoint &endpoint, Slice text) {
  if (text.size() > MAX_CARRIER_TEXT_SIZE) {
    return fail_carrier(endpoint, "too long text message", false);
  }
  auto json = text.str();
  auto r_value = json_decode(json);
  if (r_value.is_error() || r_value.ok().type() != JsonValue::Type::Object) {
    // unknown page messages are ignored, as by the other reference clients
    return;
  }
  const auto &object = r_value.ok().get_object();
  auto type = get_json_string_field(object, "t");
  if (type == "tproxy-android-init") {
    auto r_version = object.get_optional_int_field("v");
    auto nonce = get_json_string_field(object, "nonce");
    if (endpoint.state != CarrierState::WaitInit || r_version.is_error() || r_version.ok() != 1 ||
        nonce != endpoint.nonce) {
      return fail_carrier(endpoint, "invalid init message", false);
    }
    endpoint.state = CarrierState::WaitWelcome;
    send_frame(endpoint, FRAME_HELLO, 0, Slice("\x01", 1));
  } else if (type == "close") {
    fail_carrier(endpoint, "page closed the carrier", false);
  } else if (type == "status") {
    auto state = get_json_string_field(object, "state");
    if (state == "failed") {
      fail_carrier(endpoint, "page failed to connect to the relay", false);
    } else if ((state == "connecting" || state == "reconnecting") && endpoint.state != CarrierState::Ready) {
      auto max_deadline =
          endpoint.opened_at + (endpoint.is_temporary ? MAX_TEMPORARY_HANDSHAKE_TIME : MAX_HANDSHAKE_TIME);
      endpoint.handshake_deadline =
          max(endpoint.handshake_deadline, min(Time::now() + HANDSHAKE_TIMEOUT, max_deadline));
    }
  }
}

void WebProxyManager::on_carrier_binary(Endpoint &endpoint, Slice data) {
  if (endpoint.state == CarrierState::WaitInit) {
    return fail_carrier(endpoint, "binary message before init", false);
  }
  if (data.empty()) {
    return fail_carrier(endpoint, "empty binary message", false);
  }
  endpoint.unanswered_since = 0.0;

  auto carrier_id = endpoint.carrier_id;
  auto bytes = data.ubegin();
  size_t offset = 0;
  while (offset < data.size()) {
    if (data.size() - offset < FRAME_HEADER_SIZE) {
      return fail_carrier(endpoint, "partial frame header", false);
    }
    auto header = bytes + offset;
    auto type = static_cast<uint8>(header[0]);
    auto stream_id =
        (static_cast<uint32>(header[1]) << 16) | (static_cast<uint32>(header[2]) << 8) | static_cast<uint32>(header[3]);
    auto length = read_uint32(header + 4);
    if (length > MAX_FRAME_PAYLOAD_SIZE || data.size() - offset - FRAME_HEADER_SIZE < length) {
      return fail_carrier(endpoint, "invalid frame length", false);
    }
    auto status = on_carrier_frame(endpoint, type, stream_id, data.substr(offset + FRAME_HEADER_SIZE, length));
    if (status.is_error()) {
      return fail_carrier(endpoint, status.message(), false);
    }
    if (endpoint.carrier_id != carrier_id) {
      return;
    }
    offset += FRAME_HEADER_SIZE + length;
  }
}

Status WebProxyManager::on_carrier_frame(Endpoint &endpoint, uint8 type, uint32 stream_id, Slice payload) {
  if (endpoint.state != CarrierState::Ready) {
    if (type != FRAME_WELCOME || stream_id != 0 || !payload.empty()) {
      return Status::Error("Unexpected frame before welcome");
    }
    on_carrier_welcome(endpoint);
    return Status::OK();
  }

  if (stream_id == 0) {
    switch (type) {
      case FRAME_PING:
        if (payload.size() > MAX_PING_PAYLOAD_SIZE) {
          return Status::Error("Too long ping");
        }
        send_frame(endpoint, FRAME_PONG, 0, payload);
        return Status::OK();
      case FRAME_BYE:
        return Status::Error("Relay said goodbye");
      default:
        return Status::Error(PSLICE() << "Unexpected session frame " << static_cast<int32>(type));
    }
  }

  auto it = endpoint.streams.find(stream_id);
  if (it == endpoint.streams.end()) {
    if (endpoint.closed_stream_ids.count(stream_id) == 0) {
      return Status::Error("Frame for an unknown stream");
    }
    // the frame crossed our CLOSE
    bool is_valid = (type == FRAME_DATA && !payload.empty()) ||
                    (type == FRAME_WINDOW && payload.size() == 4 && read_uint32(payload.ubegin()) != 0) ||
                    (type == FRAME_CLOSE && payload.empty());
    if (!is_valid) {
      return Status::Error("Invalid frame for a closed stream");
    }
    return Status::OK();
  }

  auto stream_key = it->second;
  auto stream_it = streams_.find(stream_key);
  CHECK(stream_it != streams_.end());
  auto &stream = *stream_it->second;
  CHECK(stream.is_opened);
  switch (type) {
    case FRAME_DATA:
      if (payload.empty() || payload.size() > stream.receive_window) {
        return Status::Error("Data beyond the receive window");
      }
      stream.receive_window -= static_cast<uint32>(payload.size());
      stream.has_received_data = true;
      send_closure(stream.actor, &WebProxyStream::write, BufferSlice(payload));
      return Status::OK();
    case FRAME_WINDOW: {
      if (payload.size() != 4) {
        return Status::Error("Invalid window frame");
      }
      auto amount = read_uint32(payload.ubegin());
      if (amount == 0) {
        return Status::Error("Empty window frame");
      }
      send_closure(stream.actor, &WebProxyStream::add_send_window, amount);
      return Status::OK();
    }
    case FRAME_CLOSE:
      if (!payload.empty()) {
        return Status::Error("Invalid close frame");
      }
      // released, because destroying the actor would drop the data that is still being written
      send_closure(stream.actor.release(), &WebProxyStream::close);
      close_stream(endpoint, stream_key, false);
      return Status::OK();
    default:
      return Status::Error(PSLICE() << "Unexpected stream frame " << static_cast<int32>(type));
  }
}

void WebProxyManager::on_carrier_welcome(Endpoint &endpoint) {
  LOG(INFO) << "Web proxy carrier " << endpoint.carrier_id << " is ready";
  endpoint.state = CarrierState::Ready;
  endpoint.handshake_deadline = 0.0;
  endpoint.retry_delay = 0.0;
  for (auto &it : endpoint.streams) {
    auto stream_it = streams_.find(it.second);
    CHECK(stream_it != streams_.end());
    open_stream(endpoint, *stream_it->second);
  }
  resolve_temporary_carrier(endpoint);
}

void WebProxyManager::open_stream(Endpoint &endpoint, Stream &stream) {
  CHECK(!stream.is_opened);
  stream.is_opened = true;
  send_frame(endpoint, FRAME_OPEN, stream.stream_id, Slice());
  send_closure(stream.actor, &WebProxyStream::open);
}

void WebProxyManager::on_stream_data(BufferSlice data) {
  auto it = streams_.find(get_link_token());
  if (it == streams_.end()) {
    return;
  }
  auto &stream = *it->second;
  auto endpoint = get_endpoint(stream.endpoint_id);
  if (endpoint == nullptr || endpoint->state != CarrierState::Ready || !stream.is_opened) {
    return;
  }
  if (!stream.has_received_data && endpoint->unanswered_since == 0.0) {
    // a new MTProto connection always gets a response, so silence after its first bytes means the carrier is dead
    endpoint->unanswered_since = Time::now();
    update_timeout();
  }
  send_frame(*endpoint, FRAME_DATA, stream.stream_id, data.as_slice());
}

void WebProxyManager::on_stream_written(size_t size) {
  auto it = streams_.find(get_link_token());
  if (it == streams_.end()) {
    return;
  }
  auto &stream = *it->second;
  auto endpoint = get_endpoint(stream.endpoint_id);
  if (endpoint == nullptr || endpoint->state != CarrierState::Ready || !stream.is_opened) {
    return;
  }
  CHECK(size <= INITIAL_WINDOW);
  stream.pending_window += static_cast<uint32>(size);
  if (stream.pending_window >= WINDOW_FLUSH_SIZE) {
    flush_stream_window(*endpoint, stream);
  } else if (endpoint->window_flush_at == 0.0) {
    endpoint->window_flush_at = Time::now() + WINDOW_FLUSH_DELAY;
    update_timeout();
  }
}

void WebProxyManager::flush_stream_window(Endpoint &endpoint, Stream &stream) {
  if (stream.pending_window == 0) {
    return;
  }
  CHECK(stream.pending_window <= INITIAL_WINDOW - stream.receive_window);
  stream.receive_window += stream.pending_window;
  char payload[4];
  payload[0] = static_cast<char>(stream.pending_window >> 24);
  payload[1] = static_cast<char>(stream.pending_window >> 16);
  payload[2] = static_cast<char>(stream.pending_window >> 8);
  payload[3] = static_cast<char>(stream.pending_window);
  stream.pending_window = 0;
  send_frame(endpoint, FRAME_WINDOW, stream.stream_id, Slice(payload, 4));
}

void WebProxyManager::flush_window(Endpoint &endpoint) {
  endpoint.window_flush_at = 0.0;
  for (auto &it : endpoint.streams) {
    auto stream_it = streams_.find(it.second);
    CHECK(stream_it != streams_.end());
    flush_stream_window(endpoint, *stream_it->second);
  }
}

void WebProxyManager::on_stream_closed(uint64 stream_key) {
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) {
    return;
  }
  auto endpoint = get_endpoint(it->second->endpoint_id);
  CHECK(endpoint != nullptr);
  close_stream(*endpoint, stream_key, true);
}

void WebProxyManager::close_stream(Endpoint &endpoint, uint64 stream_key, bool need_close_frame) {
  auto it = streams_.find(stream_key);
  CHECK(it != streams_.end());
  auto stream = std::move(it->second);
  streams_.erase(it);
  endpoint.streams.erase(stream->stream_id);
  if (stream->is_opened && endpoint.state == CarrierState::Ready) {
    if (need_close_frame) {
      send_frame(endpoint, FRAME_CLOSE, stream->stream_id, Slice());
    }
    remember_closed_stream(endpoint, stream->stream_id);
  }
}

void WebProxyManager::close_all_streams(Endpoint &endpoint) {
  for (auto &it : endpoint.streams) {
    streams_.erase(it.second);
  }
  endpoint.streams.clear();
}

void WebProxyManager::remember_closed_stream(Endpoint &endpoint, uint32 stream_id) {
  if (!endpoint.closed_stream_ids.insert(stream_id).second) {
    return;
  }
  endpoint.closed_stream_id_order.push_back(stream_id);
  if (endpoint.closed_stream_id_order.size() > MAX_CLOSED_STREAM_IDS) {
    endpoint.closed_stream_ids.erase(endpoint.closed_stream_id_order.front());
    endpoint.closed_stream_id_order.pop_front();
  }
}

void WebProxyManager::send_frame(Endpoint &endpoint, uint8 type, uint32 stream_id, Slice payload) {
  if (endpoint.carrier_id == 0) {
    return;
  }
  CHECK(stream_id <= MAX_STREAM_ID);
  CHECK(payload.size() <= MAX_FRAME_PAYLOAD_SIZE);
  if (!endpoint.output.empty() && endpoint.output.size() + FRAME_HEADER_SIZE + payload.size() > MAX_CARRIER_DATA_SIZE) {
    do_flush_output(endpoint);
  }
  auto size = static_cast<uint32>(payload.size());
  char header[FRAME_HEADER_SIZE];
  header[0] = static_cast<char>(type);
  header[1] = static_cast<char>(stream_id >> 16);
  header[2] = static_cast<char>(stream_id >> 8);
  header[3] = static_cast<char>(stream_id);
  header[4] = static_cast<char>(size >> 24);
  header[5] = static_cast<char>(size >> 16);
  header[6] = static_cast<char>(size >> 8);
  header[7] = static_cast<char>(size);
  endpoint.output.append(header, FRAME_HEADER_SIZE);
  endpoint.output.append(payload.data(), payload.size());

  // frames produced while handling one batch of events are sent to the page as one message
  if (!endpoint.is_output_flush_scheduled) {
    endpoint.is_output_flush_scheduled = true;
    send_closure_later(actor_id(this), &WebProxyManager::flush_output, endpoint.endpoint_id);
  }
}

void WebProxyManager::flush_output(int64 endpoint_id) {
  auto endpoint = get_endpoint(endpoint_id);
  if (endpoint == nullptr) {
    return;
  }
  endpoint->is_output_flush_scheduled = false;
  do_flush_output(*endpoint);
}

void WebProxyManager::do_flush_output(Endpoint &endpoint) {
  if (endpoint.output.empty()) {
    return;
  }
  if (endpoint.carrier_id == 0) {
    endpoint.output.clear();
    return;
  }
  send_web_proxy_update(
      td_api::make_object<td_api::updateWebProxyCarrierData>(endpoint.carrier_id, std::move(endpoint.output)));
  endpoint.output = string();
}

void WebProxyManager::timeout_expired() {
  auto now = Time::now();

  table_remove_if(expected_ports_, [now](const auto &it) { return it.second <= now; });
  td::remove_if(pending_connections_,
                [now](const PendingConnection &connection) { return connection.expires_at <= now; });

  vector<int64> endpoint_ids;
  for (auto &it : endpoints_) {
    endpoint_ids.push_back(it.first);
  }
  for (auto endpoint_id : endpoint_ids) {
    auto endpoint = get_endpoint(endpoint_id);
    if (endpoint == nullptr) {
      continue;
    }
    if (endpoint->is_temporary && endpoint->expires_at <= now) {
      destroy_endpoint(endpoint_id);
      continue;
    }
    if (endpoint->carrier_id != 0) {
      if (endpoint->state != CarrierState::Ready) {
        auto max_deadline =
            endpoint->opened_at + (endpoint->is_temporary ? MAX_TEMPORARY_HANDSHAKE_TIME : MAX_HANDSHAKE_TIME);
        if (min(endpoint->handshake_deadline, max_deadline) <= now) {
          fail_carrier(*endpoint, "handshake timeout expired", false);
          continue;
        }
      } else if (endpoint->unanswered_since != 0.0 && endpoint->unanswered_since + RESPONSE_TIMEOUT <= now) {
        fail_carrier(*endpoint, "no response from the relay", false);
        continue;
      }
      if (endpoint->window_flush_at != 0.0 && endpoint->window_flush_at <= now) {
        flush_window(*endpoint);
      }
    } else if (!endpoint->is_temporary && endpoint->retry_at != 0.0 && endpoint->retry_at <= now) {
      endpoint->retry_at = 0.0;
      if (network_flag_) {
        open_carrier(*endpoint);
      }
    }
  }

  update_timeout();
}

void WebProxyManager::update_timeout() {
  double wakeup_at = 0.0;
  auto relax = [&wakeup_at](double at) {
    if (at != 0.0 && (wakeup_at == 0.0 || at < wakeup_at)) {
      wakeup_at = at;
    }
  };
  for (auto &it : expected_ports_) {
    relax(it.second);
  }
  for (auto &connection : pending_connections_) {
    relax(connection.expires_at);
  }
  for (auto &it : endpoints_) {
    auto &endpoint = *it.second;
    relax(endpoint.expires_at);
    if (endpoint.carrier_id != 0) {
      if (endpoint.state != CarrierState::Ready) {
        relax(endpoint.handshake_deadline);
      } else if (endpoint.unanswered_since != 0.0) {
        relax(endpoint.unanswered_since + RESPONSE_TIMEOUT);
      }
      relax(endpoint.window_flush_at);
    } else if (network_flag_) {
      relax(endpoint.retry_at);
    }
  }

  if (wakeup_at == 0.0) {
    cancel_timeout();
  } else {
    set_timeout_at(wakeup_at);
  }
}

}  // namespace td
