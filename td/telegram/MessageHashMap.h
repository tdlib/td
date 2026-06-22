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
      if (it != scheduled_messages_.end()) {
        auto message_it = it->second.find(message_id.get_scheduled_server_message_id());
        if (message_it != it->second.end()) {
          return message_it->second;
        }
      }
    } else {
      auto message_it = messages_.find({dialog_id, message_id});
      if (message_it != messages_.end()) {
        return message_it->second;
      }
    }
    return {};
  }

  ValueT get(MessageFullId message_full_id) const {
    return get(message_full_id.get_dialog_id(), message_full_id.get_message_id());
  }

  bool empty() const {
    return messages_.empty() && scheduled_messages_.empty();
  }

  size_t count(DialogId dialog_id, MessageId message_id) const {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      auto it = scheduled_messages_.find(dialog_id);
      if (it == scheduled_messages_.end()) {
        return 0;
      }
      return it->second.count(message_id.get_scheduled_server_message_id());
    } else {
      return messages_.count({dialog_id, message_id});
    }
  }

  size_t count(MessageFullId message_full_id) const {
    return count(message_full_id.get_dialog_id(), message_full_id.get_message_id());
  }

  // specialization for MessageHashMap<unique_ptr<T>>
  template <class T = ValueT>
  typename T::element_type *get_pointer(DialogId dialog_id, MessageId message_id) {
    return const_cast<typename T::element_type *>(
        static_cast<const MessageHashMap *>(this)->get_pointer(dialog_id, message_id));
  }

  template <class T = ValueT>
  typename T::element_type *get_pointer(MessageFullId message_full_id) {
    return get_pointer(message_full_id.get_dialog_id(), message_full_id.get_message_id());
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

  template <class T = ValueT>
  const typename T::element_type *get_pointer(MessageFullId message_full_id) const {
    return get_pointer(message_full_id.get_dialog_id(), message_full_id.get_message_id());
  }

  ValueT &operator[](MessageFullId message_full_id) {
    auto message_id = message_full_id.get_message_id();
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      return scheduled_messages_[message_full_id.get_dialog_id()][message_id.get_scheduled_server_message_id()];
    } else {
      return messages_[message_full_id];
    }
  }

  size_t erase(DialogId dialog_id, MessageId message_id) {
    if (message_id.is_scheduled() && message_id.is_scheduled_server()) {
      auto it = scheduled_messages_.find(dialog_id);
      if (it == scheduled_messages_.end()) {
        return 0;
      }
      auto erased_count = it->second.erase(message_id.get_scheduled_server_message_id());
      if (it->second.empty()) {
        scheduled_messages_.erase(it);
      }
      return erased_count;
    } else {
      return messages_.erase({dialog_id, message_id});
    }
  }

  size_t erase(MessageFullId message_full_id) {
    return erase(message_full_id.get_dialog_id(), message_full_id.get_message_id());
  }

  void foreach(
      const std::function<void(DialogId dialog_id, MessageId message_id, const ValueT &value)> &callback) const {
    for (auto &it : messages_) {
      callback(it.first.get_dialog_id(), it.first.get_message_id(), it.second);
    }
    for (auto &it : scheduled_messages_) {
      for (auto &message_it : it.second) {
        callback(it.first, MessageId(message_it.first, 2100000000), message_it.second);
      }
    }
  }
};

}  // namespace td
