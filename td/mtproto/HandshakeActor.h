//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/Status.h"

namespace td {
namespace mtproto {
class AuthKeyHandshake;
class AuthKeyHandshakeContext;
class RawConnection;
class HandshakeConnection;
// Has Raw connection. Generates new auth key. And returns it and raw_connection. Or error...
class HandshakeActor : public Actor {
 public:
  HandshakeActor(std::unique_ptr<AuthKeyHandshake> handshake, std::unique_ptr<RawConnection> raw_connection,
                 std::unique_ptr<AuthKeyHandshakeContext> context, double timeout,
                 Promise<std::unique_ptr<RawConnection>> raw_connection_promise,
                 Promise<std::unique_ptr<AuthKeyHandshake>> handshake_promise);
  void close();

 private:
  std::unique_ptr<AuthKeyHandshake> handshake_;
  std::unique_ptr<HandshakeConnection> connection_;
  double timeout_;

  Promise<std::unique_ptr<RawConnection>> raw_connection_promise_;
  Promise<std::unique_ptr<AuthKeyHandshake>> handshake_promise_;

  void start_up() override;
  void tear_down() override {
    finish(Status::OK());
  }
  void hangup() override {
    finish(Status::Error(1, "Cancelled"));
    stop();
  }
  void timeout_expired() override {
    finish(Status::Error("Timeout expired"));
    stop();
  }
  void loop() override;

  void finish(Status status) {
    // NB: order may be important for parent
    return_connection(std::move(status));
    return_handshake();
  }

  void return_connection(Status status);
  void return_handshake();
};
}  // namespace mtproto
}  // namespace td
