//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
  uint64 auth_key_id;
  uint32 message_ack;
  UInt128 message_key;

  uint64 salt;
  uint64 session_id;

  uint64 message_id;
  int32 seq_no;
  int32 version{1};
  bool no_crypto_flag;
  bool is_creator{false};
  bool check_mod4{true};
  bool use_random_padding{false};
  uint32 size{0};
};

}  // namespace mtproto
}  // namespace td
