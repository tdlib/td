//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"

#include "td/utils/common.h"

#include <functional>

namespace td {

struct OrderedMessage {
  int32 random_y = 0;

  bool have_previous = false;
  bool have_next = false;

  MessageId message_id;

  unique_ptr<OrderedMessage> left;
  unique_ptr<OrderedMessage> right;
};

struct OrderedMessages {
  unique_ptr<OrderedMessage> messages_;

  class IteratorBase {
    vector<const OrderedMessage *> stack_;

   protected:
    IteratorBase() = default;

    // points iterator to message with greatest identifier which is less or equal than message_id
    IteratorBase(const OrderedMessage *root, MessageId message_id) {
      CHECK(!message_id.is_scheduled());

      size_t last_right_pos = 0;
      while (root != nullptr) {
        //        LOG(DEBUG) << "Have root->message_id = " << root->message_id;
        stack_.push_back(root);
        if (root->message_id <= message_id) {
          //          LOG(DEBUG) << "Go right";
          last_right_pos = stack_.size();
          root = root->right.get();
        } else {
          //          LOG(DEBUG) << "Go left";
          root = root->left.get();
        }
      }
      stack_.resize(last_right_pos);
    }

    const OrderedMessage *operator*() const {
      return stack_.empty() ? nullptr : stack_.back();
    }

    ~IteratorBase() = default;

   public:
    IteratorBase(const IteratorBase &) = default;
    IteratorBase &operator=(const IteratorBase &) = default;
    IteratorBase(IteratorBase &&other) = default;
    IteratorBase &operator=(IteratorBase &&other) = default;

    void operator++() {
      if (stack_.empty()) {
        return;
      }

      const OrderedMessage *cur = stack_.back();
      if (!cur->have_next) {
        stack_.clear();
        return;
      }
      if (cur->right == nullptr) {
        while (true) {
          stack_.pop_back();
          if (stack_.empty()) {
            return;
          }
          const OrderedMessage *new_cur = stack_.back();
          if (new_cur->left.get() == cur) {
            return;
          }
          cur = new_cur;
        }
      }

      cur = cur->right.get();
      while (cur != nullptr) {
        stack_.push_back(cur);
        cur = cur->left.get();
      }
    }

    void operator--() {
      if (stack_.empty()) {
        return;
      }

      const OrderedMessage *cur = stack_.back();
      if (!cur->have_previous) {
        stack_.clear();
        return;
      }
      if (cur->left == nullptr) {
        while (true) {
          stack_.pop_back();
          if (stack_.empty()) {
            return;
          }
          const OrderedMessage *new_cur = stack_.back();
          if (new_cur->right.get() == cur) {
            return;
          }
          cur = new_cur;
        }
      }

      cur = cur->left.get();
      while (cur != nullptr) {
        stack_.push_back(cur);
        cur = cur->right.get();
      }
    }
  };

  class Iterator final : public IteratorBase {
   public:
    Iterator() = default;

    Iterator(OrderedMessage *root, MessageId message_id) : IteratorBase(root, message_id) {
    }

    OrderedMessage *operator*() const {
      return const_cast<OrderedMessage *>(IteratorBase::operator*());
    }
  };

  class ConstIterator final : public IteratorBase {
   public:
    ConstIterator() = default;

    ConstIterator(const OrderedMessage *root, MessageId message_id) : IteratorBase(root, message_id) {
    }

    const OrderedMessage *operator*() const {
      return IteratorBase::operator*();
    }
  };

  Iterator get_iterator(MessageId message_id) {
    return Iterator(messages_.get(), message_id);
  }

  ConstIterator get_const_iterator(MessageId message_id) const {
    return ConstIterator(messages_.get(), message_id);
  }

  OrderedMessage *insert(MessageId message_id);

  void erase(MessageId message_id);

  vector<MessageId> find_older_messages(MessageId max_message_id) const;

  vector<MessageId> find_newer_messages(MessageId min_message_id) const;

  MessageId find_message_by_date(int32 date, const std::function<int32(MessageId)> &get_message_date) const;

  vector<MessageId> find_messages_by_date(int32 min_date, int32 max_date,
                                          const std::function<int32(MessageId)> &get_message_date) const;

  void traverse_messages(const std::function<bool(MessageId)> &need_scan_older,
                         const std::function<bool(MessageId)> &need_scan_newer) const;
};

}  // namespace td
