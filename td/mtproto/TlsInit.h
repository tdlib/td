// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/net/TransparentProxy.h"

#include "td/utils/common.h"
#include "td/utils/optional.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
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
  // `selected_runtime_profile`, when set, is the single runtime wire-variant
  // snapshot chosen for this whole connection attempt at connection setup.
  // send_hello() uses it verbatim so the emitted ClientHello, transport shaping,
  // and quarantine accounting all reflect the same immutable attempt state.
  // Empty => self-select (tests / legacy callers).
  TlsInit(SocketFd socket_fd, string domain, string secret, unique_ptr<Callback> callback, ActorShared<> parent,
          double server_time_difference, stealth::NetworkRouteHints route_hints = {},
          td::optional<stealth::RuntimeProfileSelectionDecision> selected_runtime_profile = {})
      : TransparentProxy(std::move(socket_fd), IPAddress(), std::move(domain), std::move(secret), std::move(callback),
                         std::move(parent))
      , server_time_difference_(server_time_difference)
      , route_hints_(route_hints)
      , preselected_runtime_profile_(std::move(selected_runtime_profile)) {
  }

 private:
  double server_time_difference_{0};
  stealth::NetworkRouteHints route_hints_;
  td::optional<stealth::RuntimeProfileSelectionDecision> preselected_runtime_profile_;
  int32 hello_unix_time_{0};
  bool hello_uses_ech_{false};
  bool hello_profile_allows_ech_{false};
  bool hello_ech_disabled_by_route_{false};
  bool hello_ech_disabled_by_circuit_breaker_{false};
  bool hello_ech_reenabled_after_ttl_{false};
  bool hello_failure_recorded_{false};
  // Adaptive profile rotation state for the wire variant emitted this attempt.
  BrowserProfile hello_profile_{BrowserProfile::Chrome133};
  bool hello_profile_rotation_enabled_{false};
  bool hello_profile_rotation_avoided_quarantined_{false};
  uint32 hello_profile_rotation_quarantined_candidates_{0};
  bool hello_profile_failure_recorded_{false};
  enum class State {
    SendHello,
    WaitHelloResponse,
  } state_ = State::SendHello;
  std::string hello_ech_mode_name_{"disabled"};
  std::string hello_profile_name_;
  std::string hello_rand_;

  bool record_ech_failure_once();
  // Records at most one profile-quarantine failure for the emitted wire variant
  // this attempt, and only for quarantine-eligible (wire-shape) signals.
  void record_profile_failure_once(stealth::RuntimeProfileFailureSignal signal);
  void send_hello();
  Status wait_hello_response();

  void on_proxy_setup_error(const Status &status) override;
  Status loop_impl() final;
};

}  // namespace mtproto
}  // namespace td
