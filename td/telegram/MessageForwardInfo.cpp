//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageForwardInfo.h"

namespace td {

bool operator==(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs) {
  return lhs.origin == rhs.origin && lhs.date == rhs.date && lhs.from_dialog_id == rhs.from_dialog_id &&
         lhs.from_message_id == rhs.from_message_id && lhs.psa_type == rhs.psa_type &&
         lhs.is_imported == rhs.is_imported;
}

bool operator!=(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageForwardInfo &forward_info) {
  string_builder << "MessageForwardInfo[" << (forward_info.is_imported ? "imported " : "") << forward_info.origin;
  if (!forward_info.psa_type.empty()) {
    string_builder << ", psa_type " << forward_info.psa_type;
  }
  if (forward_info.from_dialog_id.is_valid() || forward_info.from_message_id.is_valid()) {
    string_builder << ", from " << MessageFullId(forward_info.from_dialog_id, forward_info.from_message_id);
  }
  return string_builder << " at " << forward_info.date << ']';
}

}  // namespace td
