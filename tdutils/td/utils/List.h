//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/logging.h"

namespace td {

struct ListNode {
  ListNode *next;
  ListNode *prev;
  ListNode() {
    clear();
  }

  ~ListNode() {
    remove();
  }

  ListNode(const ListNode &) = delete;
  ListNode &operator=(const ListNode &) = delete;

  ListNode(ListNode &&other) {
    if (other.empty()) {
      clear();
    } else {
      ListNode *head = other.prev;
      other.remove();
      head->put(this);
    }
  }

  ListNode &operator=(ListNode &&other) {
    this->remove();

    if (!other.empty()) {
      ListNode *head = other.prev;
      other.remove();
      head->put(this);
    }

    return *this;
  }

  void connect(ListNode *to) {
    CHECK(to != nullptr);
    next = to;
    to->prev = this;
  }

  void remove() {
    prev->connect(next);
    clear();
  }

  void put(ListNode *other) {
    other->connect(next);
    this->connect(other);
  }

  void put_back(ListNode *other) {
    prev->connect(other);
    other->connect(this);
  }

  ListNode *get() {
    ListNode *result = prev;
    if (result == this) {
      return nullptr;
    }
    result->prev->connect(this);
    result->clear();
    // this->connect(result->next);
    return result;
  }

  bool empty() const {
    return next == this;
  }

 private:
  void clear() {
    next = this;
    prev = this;
  }
};

}  // namespace td
