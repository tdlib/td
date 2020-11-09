//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/utils/port/ServerSocketFd.h"
#include "td/utils/port/SocketFd.h"

namespace td {

class TcpListener final : public Actor {
 public:
  class Callback : public Actor {
   public:
    virtual void accept(SocketFd fd) = 0;
  };

  TcpListener(int port, ActorShared<Callback> callback, Slice server_address = Slice("0.0.0.0"));
  void hangup() override;

 private:
  int port_;
  ServerSocketFd server_fd_;
  ActorShared<Callback> callback_;
  const string server_address_;
  void start_up() override;
  void tear_down() override;
  void loop() override;
};

}  // namespace td
