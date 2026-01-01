//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

enum class ChannelType : uint8 { Broadcast, Megagroup, Unknown };

inline StringBuilder &operator<<(StringBuilder &string_builder, ChannelType channel_type) {
  switch (channel_type) {
    case ChannelType::Broadcast:
      return string_builder << "channel";
    case ChannelType::Megagroup:
      return string_builder << "supergroup";
    case ChannelType::Unknown:
      return string_builder << "unknown";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
