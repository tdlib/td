//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

namespace td {
namespace mtproto {

struct TransportType {
  enum Type { Tcp, ObfuscatedTcp, Http } type = Tcp;
  int16 dc_id{0};
  string secret;
  bool emulate_tls{false};

  TransportType() = default;
  TransportType(Type type, int16 dc_id, string secret, bool emulate_tls = false)
      : type(type), dc_id(dc_id), secret(std::move(secret)), emulate_tls(emulate_tls) {
  }
};

}  // namespace mtproto
}  // namespace td
