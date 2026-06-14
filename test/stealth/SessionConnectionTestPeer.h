// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/SessionConnection.h"
#include "td/mtproto/mtproto_api.h"

namespace td {
namespace mtproto {
namespace test {

struct SessionConnectionTestPeer final {
  SessionConnectionTestPeer() = delete;

  static size_t service_query_count(const SessionConnection &connection) {
    return connection.service_queries_.size();
  }

  static size_t container_service_mapping_count(const SessionConnection &connection) {
    return connection.container_to_service_message_id_.size();
  }

  static MessageId first_service_query_id(const SessionConnection &connection) {
    CHECK(!connection.service_queries_.empty());
    return connection.service_queries_.begin()->first;
  }

  static MessageId first_container_message_id(const SessionConnection &connection) {
    CHECK(!connection.container_to_service_message_id_.empty());
    return connection.container_to_service_message_id_.begin()->first;
  }

  static Status deliver_msgs_state_info(SessionConnection &connection, MessageId request_message_id, Slice info) {
    SessionConnection::MsgInfo msg_info{MessageId(static_cast<uint64>(request_message_id.get() + 4)), 1, info.size()};
    auto stable_info = info.str();
    mtproto_api::msgs_state_info object(static_cast<int64>(request_message_id.get()), stable_info);
    return connection.on_packet(msg_info, object);
  }

  static Status deliver_new_session_created(SessionConnection &connection, uint64 unique_id, MessageId first_message_id) {
    SessionConnection::MsgInfo msg_info{MessageId(static_cast<uint64>(first_message_id.get() + 4)), 1, 0};
    mtproto_api::new_session_created object(static_cast<int64>(first_message_id.get()), static_cast<int64>(unique_id), 1);
    return connection.on_packet(msg_info, object);
  }

  static void fail_message(SessionConnection &connection, MessageId message_id, Status status) {
    connection.on_message_failed(message_id, std::move(status));
  }
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
