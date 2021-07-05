//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {

enum class SecretChatLayer : int32 {
  DEFAULT_LAYER = 73,
  MTPROTO_2_LAYER = 73,
  NEW_ENTITIES_LAYER = 101,
  DELETE_MESSAGES_ON_CLOSE_LAYER = 123,
  MY_LAYER = DELETE_MESSAGES_ON_CLOSE_LAYER
};

}  // namespace td
