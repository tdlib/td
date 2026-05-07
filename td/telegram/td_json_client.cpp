//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/td_json_client.h"

#include "td/telegram/Client.h"
#include "td/telegram/ClientJson.h"

#include "td/utils/logging.h"

#include <memory>

namespace {

td::Slice get_request_slice(const char *request) {
  return td::Slice(request == nullptr ? "" : request);
}

td::ClientJson *try_get_json_client(void *client, const char *callsite) {
  if (client == nullptr) {
    LOG(WARNING) << callsite << ": null client pointer";
    return nullptr;
  }
  return static_cast<td::ClientJson *>(client);  // NOLINT
}

}  // namespace

void *td_json_client_create() {
  LOG(DEBUG) << "td_json_client_create: create client instance";
  auto client = std::make_unique<td::ClientJson>();
  return client.release();
}

void td_json_client_destroy(void *client) {
  if (client == nullptr) {
    LOG(DEBUG) << "td_json_client_destroy: null client pointer, nothing to destroy";
    return;
  }
  LOG(DEBUG) << "td_json_client_destroy: destroy client instance";
  auto owned_client = std::unique_ptr<td::ClientJson>(static_cast<td::ClientJson *>(client));
}

void td_json_client_send(void *client, const char *request) {
  LOG(DEBUG) << "td_json_client_send: begin request_is_null=" << (request == nullptr);
  auto *json_client = try_get_json_client(client, "td_json_client_send");
  if (json_client == nullptr) {
    LOG(WARNING) << "td_json_client_send: dropping request because client pointer is null";
    return;
  }
  json_client->send(get_request_slice(request));
}

const char *td_json_client_receive(void *client, double timeout) {
  LOG(DEBUG) << "td_json_client_receive: begin timeout=" << timeout;
  auto *json_client = try_get_json_client(client, "td_json_client_receive");
  if (json_client == nullptr) {
    LOG(WARNING) << "td_json_client_receive: returning nullptr because client pointer is null";
    return nullptr;
  }
  return json_client->receive(timeout);
}

const char *td_json_client_execute(void *client, const char *request) {
  LOG(DEBUG) << "td_json_client_execute: begin client_ignored=true request_is_null=" << (request == nullptr);
  (void)client;
  return td::ClientJson::execute(get_request_slice(request));
}

int td_create_client_id() {
  auto client_id = td::json_create_client_id();
  LOG(DEBUG) << "td_create_client_id: created client_id=" << client_id;
  return client_id;
}

void td_send(int client_id, const char *request) {
  LOG(DEBUG) << "td_send: begin client_id=" << client_id << " request_is_null=" << (request == nullptr);
  td::json_send(client_id, get_request_slice(request));
}

const char *td_receive(double timeout) {
  LOG(DEBUG) << "td_receive: begin timeout=" << timeout;
  return td::json_receive(timeout);
}

const char *td_execute(const char *request) {
  LOG(DEBUG) << "td_execute: begin request_is_null=" << (request == nullptr);
  return td::json_execute(get_request_slice(request));
}

// Keep C callback pointer signature for exported tdjson ABI compatibility.
// NOLINTBEGIN
void td_set_log_message_callback(int max_verbosity_level, td_log_message_callback_ptr callback) {  // NOLINT
  td::ClientManager::set_log_message_callback(max_verbosity_level, callback);
}
// NOLINTEND
