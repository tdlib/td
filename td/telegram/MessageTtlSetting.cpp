//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageTtlSetting.h"

namespace td {

bool MessageTtlSetting::is_empty() const {
  return ttl_period_ == 0;
}

int32 MessageTtlSetting::get_message_ttl_setting_object() const {
  return ttl_period_;
}

bool operator==(const MessageTtlSetting &lhs, const MessageTtlSetting &rhs) {
  return lhs.ttl_period_ == rhs.ttl_period_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTtlSetting &message_ttl_setting) {
  return string_builder << "MessageTtlSetting[" << message_ttl_setting.ttl_period_ << "]";
}

}  // namespace td
