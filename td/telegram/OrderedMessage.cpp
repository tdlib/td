//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/OrderedMessage.h"

#include "td/utils/logging.h"

namespace td {

OrderedMessage *OrderedMessages::insert(MessageId message_id) {
  auto random_y = static_cast<int32>(static_cast<uint32>(message_id.get() * 2101234567u));
  unique_ptr<OrderedMessage> *v = &messages_;
  while (*v != nullptr && (*v)->random_y_ >= random_y) {
    if ((*v)->message_id_.get() < message_id.get()) {
      v = &(*v)->right_;
    } else if ((*v)->message_id_ == message_id) {
      UNREACHABLE();
    } else {
      v = &(*v)->left_;
    }
  }

  auto message = make_unique<OrderedMessage>();
  message->message_id_ = message_id;
  message->random_y_ = random_y;

  unique_ptr<OrderedMessage> *left = &message->left_;
  unique_ptr<OrderedMessage> *right = &message->right_;

  unique_ptr<OrderedMessage> cur = std::move(*v);
  while (cur != nullptr) {
    if (cur->message_id_.get() < message_id.get()) {
      *left = std::move(cur);
      left = &((*left)->right_);
      cur = std::move(*left);
    } else {
      *right = std::move(cur);
      right = &((*right)->left_);
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
    if ((*v)->message_id_.get() < message_id.get()) {
      v = &(*v)->right_;
    } else if ((*v)->message_id_.get() > message_id.get()) {
      v = &(*v)->left_;
    } else {
      break;
    }
  }

  unique_ptr<OrderedMessage> result = std::move(*v);
  CHECK(result != nullptr);
  unique_ptr<OrderedMessage> left = std::move(result->left_);
  unique_ptr<OrderedMessage> right = std::move(result->right_);

  while (left != nullptr || right != nullptr) {
    if (left == nullptr || (right != nullptr && right->random_y_ > left->random_y_)) {
      *v = std::move(right);
      v = &((*v)->left_);
      right = std::move(*v);
    } else {
      *v = std::move(left);
      v = &((*v)->right_);
      left = std::move(*v);
    }
  }
  CHECK(*v == nullptr);
}

void OrderedMessages::attach_message_to_previous(MessageId message_id, const char *source) {
  CHECK(message_id.is_valid());
  auto it = get_iterator(message_id);
  OrderedMessage *ordered_message = *it;
  CHECK(ordered_message != nullptr);
  CHECK(ordered_message->message_id_ == message_id);
  if (ordered_message->have_previous_) {
    return;
  }
  ordered_message->have_previous_ = true;
  --it;
  LOG_CHECK(*it != nullptr) << message_id << ' ' << source;
  LOG(INFO) << "Attach " << message_id << " to the previous " << (*it)->message_id_ << " from " << source;
  if ((*it)->have_next_) {
    ordered_message->have_next_ = true;
  } else {
    (*it)->have_next_ = true;
  }
}

void OrderedMessages::attach_message_to_next(MessageId message_id, const char *source) {
  CHECK(message_id.is_valid());
  auto it = get_iterator(message_id);
  OrderedMessage *ordered_message = *it;
  CHECK(ordered_message != nullptr);
  CHECK(ordered_message->message_id_ == message_id);
  if (ordered_message->have_next_) {
    return;
  }
  ordered_message->have_next_ = true;
  ++it;
  LOG_CHECK(*it != nullptr) << message_id << ' ' << source;
  LOG(INFO) << "Attach " << message_id << " to the next " << (*it)->message_id_ << " from " << source;
  if ((*it)->have_previous_) {
    ordered_message->have_previous_ = true;
  } else {
    (*it)->have_previous_ = true;
  }
}

OrderedMessages::AttachInfo OrderedMessages::auto_attach_message(MessageId message_id, MessageId last_message_id,
                                                                 const char *source) {
  auto it = get_iterator(message_id);
  OrderedMessage *previous_message = *it;
  if (previous_message != nullptr) {
    auto previous_message_id = previous_message->message_id_;
    CHECK(previous_message_id < message_id);
    if (previous_message->have_next_ || (last_message_id.is_valid() && previous_message_id >= last_message_id)) {
      if (message_id.is_server() && previous_message_id.is_server() && previous_message->have_next_) {
        ++it;
        auto next_message = *it;
        if (next_message != nullptr) {
          if (next_message->message_id_.is_server()) {
            LOG(ERROR) << "Attach " << message_id << " from " << source << " before " << next_message->message_id_
                       << " and after " << previous_message_id;
          }
        } else {
          LOG(ERROR) << "Supposed to have next message, but there is no next message after " << previous_message_id
                     << " from " << source;
        }
      }

      LOG(INFO) << "Attach " << message_id << " to the previous " << previous_message_id;
      auto have_next = previous_message->have_next_;
      previous_message->have_next_ = true;
      return {true, have_next};
    }
  }
  if (!message_id.is_yet_unsent()) {
    // message may be attached to the next message if there is no previous message
    OrderedMessage *cur = messages_.get();
    OrderedMessage *next_message = nullptr;
    while (cur != nullptr) {
      if (cur->message_id_ < message_id) {
        cur = cur->right_.get();
      } else {
        next_message = cur;
        cur = cur->left_.get();
      }
    }
    if (next_message != nullptr) {
      CHECK(!next_message->have_previous_);
      LOG(INFO) << "Attach " << message_id << " to the next " << next_message->message_id_;
      auto have_previous = next_message->have_previous_;
      return {have_previous, true};
    }
  }

  LOG(INFO) << "Can't auto-attach " << message_id;
  return {false, false};
}

void OrderedMessages::do_find_older_messages(const OrderedMessage *ordered_message, MessageId max_message_id,
                                             vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  do_find_older_messages(ordered_message->left_.get(), max_message_id, message_ids);

  if (ordered_message->message_id_ <= max_message_id) {
    message_ids.push_back(ordered_message->message_id_);

    do_find_older_messages(ordered_message->right_.get(), max_message_id, message_ids);
  }
}

vector<MessageId> OrderedMessages::find_older_messages(MessageId max_message_id) const {
  vector<MessageId> message_ids;
  do_find_older_messages(messages_.get(), max_message_id, message_ids);
  return message_ids;
}

void OrderedMessages::do_find_newer_messages(const OrderedMessage *ordered_message, MessageId min_message_id,
                                             vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  if (ordered_message->message_id_ > min_message_id) {
    do_find_newer_messages(ordered_message->left_.get(), min_message_id, message_ids);

    message_ids.push_back(ordered_message->message_id_);
  }

  do_find_newer_messages(ordered_message->right_.get(), min_message_id, message_ids);
}

vector<MessageId> OrderedMessages::find_newer_messages(MessageId min_message_id) const {
  vector<MessageId> message_ids;
  do_find_newer_messages(messages_.get(), min_message_id, message_ids);
  return message_ids;
}

MessageId OrderedMessages::do_find_message_by_date(const OrderedMessage *ordered_message, int32 date,
                                                   const std::function<int32(MessageId)> &get_message_date) {
  if (ordered_message == nullptr) {
    return MessageId();
  }

  auto message_date = get_message_date(ordered_message->message_id_);
  if (message_date > date) {
    return do_find_message_by_date(ordered_message->left_.get(), date, get_message_date);
  }

  auto message_id = do_find_message_by_date(ordered_message->right_.get(), date, get_message_date);
  if (message_id.is_valid()) {
    return message_id;
  }

  return ordered_message->message_id_;
}

MessageId OrderedMessages::find_message_by_date(int32 date,
                                                const std::function<int32(MessageId)> &get_message_date) const {
  return do_find_message_by_date(messages_.get(), date, get_message_date);
}

void OrderedMessages::do_find_messages_by_date(const OrderedMessage *ordered_message, int32 min_date, int32 max_date,
                                               const std::function<int32(MessageId)> &get_message_date,
                                               vector<MessageId> &message_ids) {
  if (ordered_message == nullptr) {
    return;
  }

  auto message_date = get_message_date(ordered_message->message_id_);
  if (message_date >= min_date) {
    do_find_messages_by_date(ordered_message->left_.get(), min_date, max_date, get_message_date, message_ids);
    if (message_date <= max_date) {
      message_ids.push_back(ordered_message->message_id_);
    }
  }
  if (message_date <= max_date) {
    do_find_messages_by_date(ordered_message->right_.get(), min_date, max_date, get_message_date, message_ids);
  }
}

vector<MessageId> OrderedMessages::find_messages_by_date(
    int32 min_date, int32 max_date, const std::function<int32(MessageId)> &get_message_date) const {
  vector<MessageId> message_ids;
  do_find_messages_by_date(messages_.get(), min_date, max_date, get_message_date, message_ids);
  return message_ids;
}

void OrderedMessages::do_traverse_messages(const OrderedMessage *ordered_message,
                                           const std::function<bool(MessageId)> &need_scan_older,
                                           const std::function<bool(MessageId)> &need_scan_newer) {
  if (ordered_message == nullptr) {
    return;
  }

  if (need_scan_older(ordered_message->message_id_)) {
    do_traverse_messages(ordered_message->left_.get(), need_scan_older, need_scan_newer);
  }

  if (need_scan_newer(ordered_message->message_id_)) {
    do_traverse_messages(ordered_message->right_.get(), need_scan_older, need_scan_newer);
  }
}

void OrderedMessages::traverse_messages(const std::function<bool(MessageId)> &need_scan_older,
                                        const std::function<bool(MessageId)> &need_scan_newer) const {
  do_traverse_messages(messages_.get(), need_scan_older, need_scan_newer);
}

}  // namespace td
