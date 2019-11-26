//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageId.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

MessageId MessageId::get_scheduled_message_id(int32 server_message_id, int32 send_date) {
  if (send_date <= (1 << 30)) {
    LOG(ERROR) << "Scheduled message send date " << send_date << " is in the past";
    return MessageId();
  }
  if (server_message_id <= 0) {
    LOG(ERROR) << "Scheduled message ID " << server_message_id << " is non-positive";
    return MessageId();
  }
  if (server_message_id >= (1 << 18)) {
    LOG(ERROR) << "Scheduled message ID " << server_message_id << " is too big";
    return MessageId();
  }
  return MessageId((static_cast<int64>(send_date - (1 << 30)) << 21) | (server_message_id << 3) | SCHEDULED_MASK);
}

bool MessageId::is_valid() const {
  if (id <= 0 || id > max().get()) {
    return false;
  }
  if ((id & FULL_TYPE_MASK) == 0) {
    return true;
  }
  int32 type = (id & TYPE_MASK);
  return type == TYPE_YET_UNSENT || type == TYPE_LOCAL;
}

bool MessageId::is_valid_scheduled() const {
  if (id <= 0 || id > max().get()) {
    return false;
  }
  int32 type = (id & TYPE_MASK);
  return type == SCHEDULED_MASK || type == (SCHEDULED_MASK | TYPE_YET_UNSENT) || type == (SCHEDULED_MASK | TYPE_LOCAL);
}

MessageType MessageId::get_type() const {
  if (id <= 0 || id > max().get()) {
    return MessageType::None;
  }
  if ((id & FULL_TYPE_MASK) == 0) {
    return MessageType::Server;
  }
  switch (id & TYPE_MASK) {
    case TYPE_YET_UNSENT:
      return MessageType::YetUnsent;
    case TYPE_LOCAL:
      return MessageType::Local;
    default:
      return MessageType::None;
  }
}

MessageId MessageId::get_next_message_id(MessageType type) const {
  switch (type) {
    case MessageType::Server:
      return get_next_server_message_id();
    case MessageType::Local:
      return MessageId(((id + TYPE_MASK + 1 - TYPE_LOCAL) & ~TYPE_MASK) + TYPE_LOCAL);
    case MessageType::YetUnsent:
      return MessageId(((id + TYPE_MASK + 1 - TYPE_YET_UNSENT) & ~TYPE_MASK) + TYPE_YET_UNSENT);
    case MessageType::None:
    default:
      UNREACHABLE();
      return MessageId();
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, MessageId message_id) {
  if (!message_id.is_valid()) {
    return string_builder << "invalid message " << message_id.get();
  }
  if (message_id.is_server()) {
    return string_builder << "server message " << (message_id.get() >> MessageId::SERVER_ID_SHIFT);
  }
  if (message_id.is_local()) {
    return string_builder << "local message " << (message_id.get() >> MessageId::SERVER_ID_SHIFT) << '.'
                          << (message_id.get() & MessageId::FULL_TYPE_MASK);
  }
  if (message_id.is_yet_unsent()) {
    return string_builder << "yet unsent message " << (message_id.get() >> MessageId::SERVER_ID_SHIFT) << '.'
                          << (message_id.get() & MessageId::FULL_TYPE_MASK);
  }
  return string_builder << "bugged message " << message_id.get();
}

}  // namespace td
