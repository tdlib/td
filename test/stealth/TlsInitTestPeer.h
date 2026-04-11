// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// Test-only access peer for `td::mtproto::TlsInit`.
//
// Replaces the historical `#define private public` hack that broke MSVC builds
// because the Microsoft C++ ABI mangling encodes member access modifiers into
// symbol names. Using a friend `struct` keeps name mangling consistent across
// compilation units while still granting tests controlled access to the
// private/protected internals required to drive the TLS hello state machine.
//
// `TlsInitTestPeer` is a stateless static accessor:
//   * `send_hello()` / `wait_hello_response()` invoke the corresponding
//     private member functions
//   * `fd()` returns a mutable reference to the protected `fd_` member that
//     `TlsInit` inherits from `TransparentProxy`
//   * `hello_rand()` exposes the private `hello_rand_` field for HMAC fixture
//     construction in tests
//
// No production code path imports this header, and `TlsInit` only declares
// the friendship — it does not depend on this header. The peer therefore
// adds zero runtime cost and zero ODR surface to release builds.

#pragma once

#include "td/mtproto/TlsInit.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {
namespace test {

struct TlsInitTestPeer final {
  TlsInitTestPeer() = delete;

  static void send_hello(TlsInit &tls_init) {
    tls_init.send_hello();
  }

  static Status wait_hello_response(TlsInit &tls_init) {
    return tls_init.wait_hello_response();
  }

  static BufferedFd<SocketFd> &fd(TlsInit &tls_init) {
    return tls_init.fd_;
  }

  static const std::string &hello_rand(const TlsInit &tls_init) {
    return tls_init.hello_rand_;
  }
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
