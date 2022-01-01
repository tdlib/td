//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/UInt.h"

namespace td {
namespace mtproto {

struct PacketInfo {
  enum { Common, EndToEnd } type = Common;
  uint64 auth_key_id{0};
  uint32 message_ack{0};
  UInt128 message_key;

  uint64 salt{0};
  uint64 session_id{0};

  uint64 message_id{0};
  int32 seq_no{0};
  int32 version{1};
  bool no_crypto_flag{false};
  bool is_creator{false};
  bool check_mod4{true};
  bool use_random_padding{false};
  uint32 size{0};
};

}  // namespace mtproto
}  // namespace td
