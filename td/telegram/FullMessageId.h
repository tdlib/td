//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

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
    dialog_id.store(storer);
    message_id.store(storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    dialog_id.parse(parser);
    message_id.parse(parser);
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
