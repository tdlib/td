//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ProxyChecker.h"

#include "td/telegram/net/PublicRsaKeySharedMain.h"

#include "td/mtproto/DhCallback.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/RSA.h"
#include "td/mtproto/TlsInit.h"

#include "td/actor/SleepActor.h"

#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Time.h"

#include <memory>

namespace td {

ProxyChecker::ProxyChecker(ActorShared<> parent) : parent_(std::move(parent)) {
}

void ProxyChecker::test_proxy(Proxy &&proxy, int32 dc_id, double timeout, Promise<Unit> &&promise) {
  auto start_time = Time::now();

  IPAddress ip_address;
  auto status = ip_address.init_host_port(proxy.server(), proxy.port());
  if (status.is_error()) {
    return promise.set_error(400, status.public_message());
  }
  auto r_socket_fd = SocketFd::open(ip_address);
  if (r_socket_fd.is_error()) {
    return promise.set_error(400, r_socket_fd.error().public_message());
  }

  auto dc_options = ConnectionCreator::get_default_dc_options(false);
  IPAddress mtproto_ip_address;
  for (auto &dc_option : dc_options.dc_options) {
    if (dc_option.get_dc_id().get_raw_id() == dc_id) {
      mtproto_ip_address = dc_option.get_ip_address();
      break;
    }
  }
  if (!mtproto_ip_address.is_valid()) {
    return promise.set_error(400, "Invalid datacenter identifier specified");
  }

  auto request_id = ++test_proxy_request_id_;
  auto request = make_unique<TestProxyRequest>();
  request->proxy_ = std::move(proxy);
  request->dc_id_ = static_cast<int16>(dc_id);
  request->promise_ = std::move(promise);

  auto connection_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), request_id](Result<ConnectionCreator::ConnectionData> r_data) {
        send_closure(actor_id, &ProxyChecker::on_test_proxy_connection_data, request_id, std::move(r_data));
      });
  request->child_ = ConnectionCreator::prepare_connection(
      ip_address, r_socket_fd.move_as_ok(), request->proxy_, mtproto_ip_address, request->get_transport(), "Test",
      "TestPingDC2", nullptr, {}, false, std::move(connection_promise));

  test_proxy_requests_.emplace(request_id, std::move(request));

  create_actor<SleepActor>("TestProxyTimeoutActor", timeout + start_time - Time::now(),
                           PromiseCreator::lambda([actor_id = actor_id(this), request_id](Unit) {
                             send_closure(actor_id, &ProxyChecker::on_test_proxy_timeout, request_id);
                           }))
      .release();
}

void ProxyChecker::on_test_proxy_connection_data(uint64 request_id, Result<ConnectionCreator::ConnectionData> r_data) {
  auto it = test_proxy_requests_.find(request_id);
  if (it == test_proxy_requests_.end()) {
    return;
  }
  auto *request = it->second.get();
  if (r_data.is_error()) {
    auto promise = std::move(request->promise_);
    test_proxy_requests_.erase(it);
    return promise.set_error(r_data.move_as_error());
  }

  class HandshakeContext final : public mtproto::AuthKeyHandshakeContext {
   public:
    mtproto::DhCallback *get_dh_callback() final {
      return nullptr;
    }
    mtproto::PublicRsaKeyInterface *get_public_rsa_key_interface() final {
      return public_rsa_key_.get();
    }

   private:
    std::shared_ptr<mtproto::PublicRsaKeyInterface> public_rsa_key_ = PublicRsaKeySharedMain::create(false);
  };
  auto handshake = make_unique<mtproto::AuthKeyHandshake>(request->dc_id_, 3600);
  auto data = r_data.move_as_ok();
  auto raw_connection = mtproto::RawConnection::create(data.ip_address, std::move(data.buffered_socket_fd),
                                                       request->get_transport(), nullptr);
  request->child_ = create_actor<mtproto::HandshakeActor>(
      "HandshakeActor", std::move(handshake), std::move(raw_connection), make_unique<HandshakeContext>(), 10.0,
      PromiseCreator::lambda(
          [actor_id = actor_id(this), request_id](Result<unique_ptr<mtproto::RawConnection>> raw_connection) {
            send_closure(actor_id, &ProxyChecker::on_test_proxy_handshake_connection, request_id,
                         std::move(raw_connection));
          }),
      PromiseCreator::lambda(
          [actor_id = actor_id(this), request_id](Result<unique_ptr<mtproto::AuthKeyHandshake>> handshake) {
            send_closure(actor_id, &ProxyChecker::on_test_proxy_handshake, request_id, std::move(handshake));
          }));
}

void ProxyChecker::on_test_proxy_handshake_connection(uint64 request_id,
                                                      Result<unique_ptr<mtproto::RawConnection>> r_raw_connection) {
  if (r_raw_connection.is_error()) {
    auto it = test_proxy_requests_.find(request_id);
    if (it == test_proxy_requests_.end()) {
      return;
    }
    auto promise = std::move(it->second->promise_);
    test_proxy_requests_.erase(it);
    return promise.set_error(400, r_raw_connection.move_as_error().public_message());
  }
}

void ProxyChecker::on_test_proxy_handshake(uint64 request_id,
                                           Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake) {
  auto it = test_proxy_requests_.find(request_id);
  if (it == test_proxy_requests_.end()) {
    return;
  }
  auto promise = std::move(it->second->promise_);
  test_proxy_requests_.erase(it);

  if (r_handshake.is_error()) {
    return promise.set_error(400, r_handshake.move_as_error().public_message());
  }
  auto handshake = r_handshake.move_as_ok();
  if (!handshake->is_ready_for_finish()) {
    return promise.set_error(400, "Handshake is not ready");
  }
  promise.set_value(Unit());
}

void ProxyChecker::on_test_proxy_timeout(uint64 request_id) {
  auto it = test_proxy_requests_.find(request_id);
  if (it == test_proxy_requests_.end()) {
    return;
  }
  auto promise = std::move(it->second->promise_);
  test_proxy_requests_.erase(it);

  promise.set_error(400, "Timeout expired");
}

}  // namespace td
