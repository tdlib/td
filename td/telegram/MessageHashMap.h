//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/ScheduledServerMessageId.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"

#include <functional>

namespace td {

template <class ValueT>
class MessageHashMap {
  FlatHashMap<MessageFullId, ValueT, MessageFullIdHash> messages_;
  FlatHashMap<DialogId, FlatHashMap<ScheduledServerMessageId, ValueT, ScheduledServerMessageIdHash>, DialogIdHash>
      scheduled_messages_;

 public:
  void set(DialogId dialog_id, MessageId message_id, ValueT value) {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      scheduled_messages_[dialog_id][message_id.get_scheduled_server_message_id()] = std::move(value);
    } else {
      messages_[{dialog_id, message_id}] = std::move(value);
    }
  }

  ValueT get(DialogId dialog_id, MessageId message_id) const {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      auto it = scheduled_messages_.find(dialog_id);
      if (it == scheduled_messages_.end()) {
        return {};
      }
      auto message_it = it->second.find(message_id.get_scheduled_server_message_id());
      if (message_it == it->second.end()) {
        return {};
      }
      return message_it->second;
    } else {
      auto message_it = messages_.find({dialog_id, message_id});
      if (message_it == messages_.end()) {
        return {};
      }
      return message_it->second;
    }
  }

  // specialization for MessageHashMap<unique_ptr<T>>
  template <class T = ValueT>
  typename T::element_type *get_pointer(DialogId dialog_id, MessageId message_id) {
    return const_cast<typename T::element_type *>(
        static_cast<const MessageHashMap *>(this)->get_pointer(dialog_id, message_id));
  }

  template <class T = ValueT>
  const typename T::element_type *get_pointer(DialogId dialog_id, MessageId message_id) const {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      auto it = scheduled_messages_.find(dialog_id);
      if (it == scheduled_messages_.end()) {
        return nullptr;
      }
      auto message_it = it->second.find(message_id.get_scheduled_server_message_id());
      if (message_it == it->second.end()) {
        return nullptr;
      }
      return message_it->second.get();
    } else {
      auto message_it = messages_.find({dialog_id, message_id});
      if (message_it == messages_.end()) {
        return nullptr;
      }
      return message_it->second.get();
    }
  }

  size_t erase(DialogId dialog_id, MessageId message_id) {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      auto it = scheduled_messages_.find(dialog_id);
      if (it == scheduled_messages_.end()) {
        return 0;
      }
      return it->second.erase(message_id.get_scheduled_server_message_id());
    } else {
      return messages_.erase({dialog_id, message_id});
    }
  }

  void foreach(
      const std::function<void(DialogId dialog_id, MessageId message_id, const ValueT &value)> &callback) const {
    for (auto &it : messages_) {
      callback(it.first.get_dialog_id(), it.first.get_message_id(), it.second);
    }
    for (auto &it : scheduled_messages_) {
      for (auto &message_it : it.second) {
        callback(it.first, message_it.first, message_it.second);
      }
    }
  }
};

}  // namespace td
