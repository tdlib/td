//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <functional>
#include <limits>
#include <type_traits>

namespace td {

class ServerMessageId {
  int32 id = 0;

 public:
  ServerMessageId() = default;

  explicit ServerMessageId(int32 message_id) : id(message_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  ServerMessageId(T message_id) = delete;

  bool is_valid() const {
    return id > 0;
  }

  int32 get() const {
    return id;
  }

  bool operator==(const ServerMessageId &other) const {
    return id == other.id;
  }

  bool operator!=(const ServerMessageId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_int();
  }
};

enum class MessageType : int32 { None, Server, Local, YetUnsent };

class MessageId {
  int64 id = 0;

  static constexpr int32 SERVER_ID_SHIFT = 20;
  static constexpr int32 SHORT_TYPE_MASK = (1 << 2) - 1;
  static constexpr int32 TYPE_MASK = (1 << 3) - 1;
  static constexpr int32 FULL_TYPE_MASK = (1 << SERVER_ID_SHIFT) - 1;
  static constexpr int32 SCHEDULED_MASK = 4;
  static constexpr int32 TYPE_YET_UNSENT = 1;
  static constexpr int32 TYPE_LOCAL = 2;

  friend StringBuilder &operator<<(StringBuilder &string_builder, MessageId message_id);

  // ordinary message ID layout
  // |-------31--------|---17---|1|--2-|
  // |server_message_id|local_id|0|type|

  // scheduled message ID layout
  // |-------30-------|----18---|1|--2-|
  // |send_date-2**30 |server_id|1|type|

 public:
  MessageId() = default;

  explicit MessageId(ServerMessageId server_message_id)
      : id(static_cast<int64>(server_message_id.get()) << SERVER_ID_SHIFT) {
  }

  static MessageId get_scheduled_message_id(int32 server_message_id, int32 send_date) {
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

  explicit constexpr MessageId(int64 message_id) : id(message_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  MessageId(T message_id) = delete;

  static constexpr MessageId min() {
    return MessageId(static_cast<int64>(MessageId::TYPE_LOCAL));
  }
  static constexpr MessageId max() {
    return MessageId(static_cast<int64>(std::numeric_limits<int32>::max()) << SERVER_ID_SHIFT);
  }

  bool is_valid() const {
    if (id <= 0 || id > max().get()) {
      return false;
    }
    if ((id & FULL_TYPE_MASK) == 0) {
      return true;
    }
    int32 type = (id & TYPE_MASK);
    return type == TYPE_YET_UNSENT || type == TYPE_LOCAL;
  }

  bool is_valid_scheduled() const {
    if (id <= 0 || id > max().get()) {
      return false;
    }
    int32 type = (id & TYPE_MASK);
    return type == SCHEDULED_MASK || type == (SCHEDULED_MASK | TYPE_YET_UNSENT) ||
           type == (SCHEDULED_MASK | TYPE_LOCAL);
  }

  int64 get() const {
    return id;
  }

  MessageType get_type() const {
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

  bool is_scheduled() const {
    return (id & SCHEDULED_MASK) != 0;
  }

  bool is_yet_unsent() const {
    CHECK(is_valid() || is_scheduled());
    return (id & SHORT_TYPE_MASK) == TYPE_YET_UNSENT;
  }

  bool is_local() const {
    CHECK(is_valid() || is_scheduled());
    return (id & SHORT_TYPE_MASK) == TYPE_LOCAL;
  }

  bool is_server() const {
    CHECK(is_valid());
    return (id & FULL_TYPE_MASK) == 0;
  }

  bool is_scheduled_server() const {
    CHECK(is_valid_scheduled());
    return (id & SHORT_TYPE_MASK) == 0;
  }

  ServerMessageId get_server_message_id() const {
    CHECK(id == 0 || is_server());
    return ServerMessageId(narrow_cast<int32>(id >> SERVER_ID_SHIFT));
  }

  // returns greatest server message id not bigger than this message id
  MessageId get_prev_server_message_id() const {
    return MessageId(id & ~FULL_TYPE_MASK);
  }

  // returns smallest server message id not less than this message id
  MessageId get_next_server_message_id() const {
    return MessageId((id + FULL_TYPE_MASK) & ~FULL_TYPE_MASK);
  }

  MessageId get_next_message_id(MessageType type) const {
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

  int32 get_scheduled_server_message_id() const {
    CHECK(is_scheduled_server());
    return static_cast<int32>((id >> 3) & ((1 << 18) - 1));
  }

  bool operator==(const MessageId &other) const {
    return id == other.id;
  }

  bool operator!=(const MessageId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_long(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_long();
  }
};

struct MessageIdHash {
  std::size_t operator()(MessageId message_id) const {
    return std::hash<int64>()(message_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, MessageId message_id) {
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

struct FullMessageId {
 private:
  DialogId dialog_id;
  MessageId message_id;

 public:
  FullMessageId() : dialog_id(), message_id() {
  }

  FullMessageId(DialogId dialog_id, MessageId message_id) : dialog_id(dialog_id), message_id(message_id) {
  }

  bool operator==(const FullMessageId &other) const {
    return dialog_id == other.dialog_id && message_id == other.message_id;
  }

  bool operator!=(const FullMessageId &other) const {
    return !(*this == other);
  }

  DialogId get_dialog_id() const {
    return dialog_id;
  }
  MessageId get_message_id() const {
    return message_id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(dialog_id, storer);
    store(message_id, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(dialog_id, parser);
    parse(message_id, parser);
  }
};

struct FullMessageIdHash {
  std::size_t operator()(FullMessageId full_message_id) const {
    return DialogIdHash()(full_message_id.get_dialog_id()) * 2023654985u +
           MessageIdHash()(full_message_id.get_message_id());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, FullMessageId full_message_id) {
  return string_builder << full_message_id.get_message_id() << " in " << full_message_id.get_dialog_id();
}

}  // namespace td
