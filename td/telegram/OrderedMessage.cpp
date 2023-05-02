//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/OrderedMessage.h"

namespace td {

OrderedMessage *OrderedMessages::insert(MessageId message_id) {
  auto random_y = static_cast<int32>(static_cast<uint32>(message_id.get() * 2101234567u));
  unique_ptr<OrderedMessage> *v = &messages_;
  while (*v != nullptr && (*v)->random_y >= random_y) {
    if ((*v)->message_id.get() < message_id.get()) {
      v = &(*v)->right;
    } else if ((*v)->message_id == message_id) {
      UNREACHABLE();
    } else {
      v = &(*v)->left;
    }
  }

  auto message = make_unique<OrderedMessage>();
  message->message_id = message_id;
  message->random_y = random_y;

  unique_ptr<OrderedMessage> *left = &message->left;
  unique_ptr<OrderedMessage> *right = &message->right;

  unique_ptr<OrderedMessage> cur = std::move(*v);
  while (cur != nullptr) {
    if (cur->message_id.get() < message_id.get()) {
      *left = std::move(cur);
      left = &((*left)->right);
      cur = std::move(*left);
    } else {
      *right = std::move(cur);
      right = &((*right)->left);
      cur = std::move(*right);
    }
  }
  CHECK(*left == nullptr);
  CHECK(*right == nullptr);
  *v = std::move(message);
  return v->get();
}

void OrderedMessages::erase(MessageId message_id) {
  unique_ptr<OrderedMessage> *v = &messages_;
  while (*v != nullptr) {
    if ((*v)->message_id.get() < message_id.get()) {
      v = &(*v)->right;
    } else if ((*v)->message_id.get() > message_id.get()) {
      v = &(*v)->left;
    } else {
      break;
    }
  }

  unique_ptr<OrderedMessage> result = std::move(*v);
  CHECK(result != nullptr);
  unique_ptr<OrderedMessage> left = std::move(result->left);
  unique_ptr<OrderedMessage> right = std::move(result->right);

  while (left != nullptr || right != nullptr) {
    if (left == nullptr || (right != nullptr && right->random_y > left->random_y)) {
      *v = std::move(right);
      v = &((*v)->left);
      right = std::move(*v);
    } else {
      *v = std::move(left);
      v = &((*v)->right);
      left = std::move(*v);
    }
  }
  CHECK(*v == nullptr);
}

static void do_find_older_messages(const OrderedMessage *ordered_message, MessageId max_message_id,
                                   vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  do_find_older_messages(ordered_message->left.get(), max_message_id, message_ids);

  if (ordered_message->message_id <= max_message_id) {
    message_ids.push_back(ordered_message->message_id);

    do_find_older_messages(ordered_message->right.get(), max_message_id, message_ids);
  }
}

vector<MessageId> OrderedMessages::find_older_messages(MessageId max_message_id) const {
  vector<MessageId> message_ids;
  do_find_older_messages(messages_.get(), max_message_id, message_ids);
  return message_ids;
}

static void do_find_newer_messages(const OrderedMessage *ordered_message, MessageId min_message_id,
                                   vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  if (ordered_message->message_id > min_message_id) {
    do_find_newer_messages(ordered_message->left.get(), min_message_id, message_ids);

    message_ids.push_back(ordered_message->message_id);
  }

  do_find_newer_messages(ordered_message->right.get(), min_message_id, message_ids);
}

vector<MessageId> OrderedMessages::find_newer_messages(MessageId min_message_id) const {
  vector<MessageId> message_ids;
  do_find_newer_messages(messages_.get(), min_message_id, message_ids);
  return message_ids;
}

static MessageId do_find_message_by_date(const OrderedMessage *ordered_message, int32 date,
                                         const std::function<int32(MessageId)> &get_message_date) {
  if (ordered_message == nullptr) {
    return MessageId();
  }

  auto message_date = get_message_date(ordered_message->message_id);
  if (message_date > date) {
    return do_find_message_by_date(ordered_message->left.get(), date, get_message_date);
  }

  auto message_id = do_find_message_by_date(ordered_message->right.get(), date, get_message_date);
  if (message_id.is_valid()) {
    return message_id;
  }

  return ordered_message->message_id;
}

MessageId OrderedMessages::find_message_by_date(int32 date,
                                                const std::function<int32(MessageId)> &get_message_date) const {
  return do_find_message_by_date(messages_.get(), date, get_message_date);
}

static void do_find_messages_by_date(const OrderedMessage *ordered_message, int32 min_date, int32 max_date,
                                     const std::function<int32(MessageId)> &get_message_date,
                                     vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  auto message_date = get_message_date(ordered_message->message_id);
  if (message_date >= min_date) {
    do_find_messages_by_date(ordered_message->left.get(), min_date, max_date, get_message_date, message_ids);
    if (message_date <= max_date) {
      message_ids.push_back(ordered_message->message_id);
    }
  }
  if (message_date <= max_date) {
    do_find_messages_by_date(ordered_message->right.get(), min_date, max_date, get_message_date, message_ids);
  }
}

vector<MessageId> OrderedMessages::find_messages_by_date(
    int32 min_date, int32 max_date, const std::function<int32(MessageId)> &get_message_date) const {
  vector<MessageId> message_ids;
  do_find_messages_by_date(messages_.get(), min_date, max_date, get_message_date, message_ids);
  return message_ids;
}

static void do_traverse_messages(const OrderedMessage *ordered_message,
                                 const std::function<bool(MessageId)> &need_scan_older,
                                 const std::function<bool(MessageId)> &need_scan_newer) {
  if (ordered_message == nullptr) {
    return;
  }

  if (need_scan_older(ordered_message->message_id)) {
    do_traverse_messages(ordered_message->left.get(), need_scan_older, need_scan_newer);
  }

  if (need_scan_newer(ordered_message->message_id)) {
    do_traverse_messages(ordered_message->right.get(), need_scan_older, need_scan_newer);
  }
}

void OrderedMessages::traverse_messages(const std::function<bool(MessageId)> &need_scan_older,
                                        const std::function<bool(MessageId)> &need_scan_newer) const {
  do_traverse_messages(messages_.get(), need_scan_older, need_scan_newer);
}

}  // namespace td
