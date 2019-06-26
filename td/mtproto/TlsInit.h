//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/net/TransparentProxy.h"

namespace td {
class Grease {
 public:
  static void init(MutableSlice res);
};

class TlsInit : public TransparentProxy {
 public:
  using TransparentProxy::TransparentProxy;

 private:
  enum class State {
    SendHello,
    WaitHelloResponse,
  } state_ = State::SendHello;

  void send_hello();
  Status wait_hello_response();

  Status loop_impl() override;
};
}  // namespace td
