//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageId.h"
#include "td/telegram/OrderedMessage.h"
#include "td/telegram/ServerMessageId.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

static td::MessageId server_message_id(td::int32 id) {
  return td::MessageId(td::ServerMessageId(id));
}

static td::vector<td::MessageId> server_message_ids(td::int32 from, td::int32 to) {
  td::vector<td::MessageId> result;
  for (auto id = from; id >= to; id--) {
    result.push_back(server_message_id(id));
  }
  return result;
}

static void insert_messages(td::OrderedMessages &ordered_messages, td::int32 from, td::int32 to) {
  for (auto id = from; id <= to; id++) {
    ordered_messages.insert(server_message_id(id), false, td::MessageId(), "insert_messages");
  }
}

static td::vector<td::MessageId> get_newest_messages(const td::OrderedMessages &ordered_messages,
                                                     td::int32 last_message_id, td::int32 limit) {
  auto from_message_id = td::MessageId::max();
  td::int32 offset = 0;
  return ordered_messages.get_history(server_message_id(last_message_id), from_message_id, offset, limit, false);
}

TEST(OrderedMessages, attach_messages) {
  td::OrderedMessages ordered_messages;
  insert_messages(ordered_messages, 1, 10);
  ASSERT_EQ(server_message_ids(10, 10), get_newest_messages(ordered_messages, 10, 20));

  ordered_messages.attach_messages(server_message_ids(10, 1), false, "attach_messages");
  ASSERT_EQ(server_message_ids(10, 1), get_newest_messages(ordered_messages, 10, 20));
}

TEST(OrderedMessages, attach_messages_to_next) {
  td::OrderedMessages ordered_messages;
  insert_messages(ordered_messages, 1, 6);

  ordered_messages.attach_messages(server_message_ids(5, 1), false, "attach_messages_to_next");
  ASSERT_EQ(server_message_ids(6, 6), get_newest_messages(ordered_messages, 6, 20));

  ordered_messages.attach_messages(server_message_ids(5, 1), true, "attach_messages_to_next");
  ASSERT_EQ(server_message_ids(6, 1), get_newest_messages(ordered_messages, 6, 20));
}

TEST(OrderedMessages, need_preload_messages) {
  td::OrderedMessages ordered_messages;
  insert_messages(ordered_messages, 1, 40);
  ordered_messages.attach_messages(server_message_ids(40, 1), false, "need_preload_messages");

  auto min_message_id = server_message_id(40);
  ASSERT_TRUE(!ordered_messages.need_preload_older_messages(min_message_id, 31));
  ASSERT_EQ(server_message_id(10), min_message_id);

  min_message_id = server_message_id(20);
  ASSERT_TRUE(ordered_messages.need_preload_older_messages(min_message_id, 31));
  ASSERT_EQ(server_message_id(1), min_message_id);

  auto max_message_id = server_message_id(1);
  ASSERT_TRUE(!ordered_messages.need_preload_newer_messages(max_message_id, 30));
  ASSERT_EQ(server_message_id(31), max_message_id);

  max_message_id = server_message_id(35);
  ASSERT_TRUE(ordered_messages.need_preload_newer_messages(max_message_id, 30));
  ASSERT_EQ(server_message_id(40), max_message_id);
}

TEST(OrderedMessages, need_preload_messages_gap) {
  td::OrderedMessages ordered_messages;
  insert_messages(ordered_messages, 1, 10);
  insert_messages(ordered_messages, 20, 30);
  ordered_messages.attach_messages(server_message_ids(10, 1), false, "need_preload_messages_gap");
  ordered_messages.attach_messages(server_message_ids(30, 20), false, "need_preload_messages_gap");

  // the walk stops at the gap between the two parts
  auto min_message_id = server_message_id(30);
  ASSERT_TRUE(ordered_messages.need_preload_older_messages(min_message_id, 31));
  ASSERT_EQ(server_message_id(20), min_message_id);

  auto max_message_id = server_message_id(1);
  ASSERT_TRUE(ordered_messages.need_preload_newer_messages(max_message_id, 30));
  ASSERT_EQ(server_message_id(10), max_message_id);
}
