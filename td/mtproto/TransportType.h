// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/optional.h"

namespace td {
namespace mtproto {

struct TransportType {
  enum Type { Tcp, ObfuscatedTcp, Http } type = Tcp;
  int16 dc_id{0};
  ProxySecret secret;
  // Single-selection handoff: when set (only for emulate_tls connections), this
  // is the one runtime wire-variant snapshot chosen for the whole connection
  // attempt. It carries both the profile and the final hello_uses_ech decision
  // so TlsInit, transport shaping, and quarantine accounting all operate on the
  // same immutable attempt snapshot. Empty => each consumer self-selects
  // (legacy/test callers only).
  td::optional<stealth::RuntimeProfileSelectionDecision> selected_runtime_profile;

  TransportType() = default;
  TransportType(Type type, int16 dc_id, ProxySecret secret) : type(type), dc_id(dc_id), secret(std::move(secret)) {
  }
};

}  // namespace mtproto
}  // namespace td
