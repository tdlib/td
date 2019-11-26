//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ServerMessageId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <functional>
#include <limits>
#include <type_traits>

namespace td {

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

  static MessageId get_scheduled_message_id(int32 server_message_id, int32 send_date);

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

  bool is_valid() const;

  bool is_valid_scheduled() const;

  int64 get() const {
    return id;
  }

  MessageType get_type() const;

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

  MessageId get_next_message_id(MessageType type) const;

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
