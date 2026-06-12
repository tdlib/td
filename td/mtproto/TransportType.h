//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/ProxySecret.h"

#include "td/utils/common.h"
#include "td/utils/optional.h"

namespace td {
namespace mtproto {

struct TransportType {
  enum Type { Tcp, ObfuscatedTcp, Http } type = Tcp;
  int16 dc_id{0};
  ProxySecret secret;
  // Single-selection handoff: when set (only for emulate_tls connections), this is
  // the one runtime BrowserProfile chosen for the whole connection attempt. It is
  // computed once at connection setup and threaded to both the emitted TLS
  // ClientHello (TlsInit) and the transport-shaping config (create_transport) so
  // they cannot diverge into split profile state. Empty => each consumer selects
  // its own profile (legacy behaviour; coherent only while rotation is disabled).
  td::optional<BrowserProfile> selected_profile;

  TransportType() = default;
  TransportType(Type type, int16 dc_id, ProxySecret secret) : type(type), dc_id(dc_id), secret(std::move(secret)) {
  }
};

}  // namespace mtproto
}  // namespace td
