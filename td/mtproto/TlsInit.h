// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"

#include "td/net/TransparentProxy.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

namespace test {
// Forward declaration for the test-only access peer defined in
// `test/stealth/TlsInitTestPeer.h`. The peer needs friendship to drive the
// private TLS hello state machine without resorting to ABI-fragile hacks like
// `#define private public`, which break under MSVC because the Microsoft C++
// ABI encodes access modifiers into mangled symbol names.
struct TlsInitTestPeer;
}  // namespace test

class Grease {
 public:
  static void init(MutableSlice res);
};

class TlsInit final : public TransparentProxy {
  friend struct ::td::mtproto::test::TlsInitTestPeer;

 public:
  TlsInit(SocketFd socket_fd, string domain, string secret, unique_ptr<Callback> callback, ActorShared<> parent,
          double server_time_difference, stealth::NetworkRouteHints route_hints = {})
      : TransparentProxy(std::move(socket_fd), IPAddress(), std::move(domain), std::move(secret), std::move(callback),
                         std::move(parent))
      , server_time_difference_(server_time_difference)
      , route_hints_(route_hints) {
  }

 private:
  double server_time_difference_{0};
  stealth::NetworkRouteHints route_hints_;
  int32 hello_unix_time_{0};
  bool hello_uses_ech_{false};
  enum class State {
    SendHello,
    WaitHelloResponse,
  } state_ = State::SendHello;
  std::string hello_rand_;

  void send_hello();
  Status wait_hello_response();

  Status loop_impl() final;
};

}  // namespace mtproto
}  // namespace td
