//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/td_api_json.h"
#include "td/telegram/td_json_client.h"

#include "td/telegram/Client.h"
#include "td/telegram/ClientJson.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/SliceBuilder.h"

#include "td/utils/Slice.h"

void *td_json_client_create() {
  return new td::ClientJson();
}

void td_json_client_destroy(void *client) {
  delete static_cast<td::ClientJson *>(client);
}

void td_json_client_send(void *client, const char *request) {
  static_cast<td::ClientJson *>(client)->send(td::Slice(request == nullptr ? "" : request));
}

const char *td_json_client_receive(void *client, double timeout) {
  return static_cast<td::ClientJson *>(client)->receive(timeout);
}

const char *td_json_client_execute(void *client, const char *request) {
  return td::ClientJson::execute(td::Slice(request == nullptr ? "" : request));
}

int td_create_client_id() {
  return td::json_create_client_id();
}

void td_send(int client_id, const char *request) {
  td::json_send(client_id, td::Slice(request == nullptr ? "" : request));
}

const char *td_receive(double timeout) {
  return td::json_receive(timeout);
}

const char *td_execute(const char *request) {
  return td::json_execute(td::Slice(request == nullptr ? "" : request));
}

void td_set_log_message_callback(int max_verbosity_level, td_log_message_callback_ptr callback) {
  td::ClientManager::set_log_message_callback(max_verbosity_level, callback);
}

static TD_THREAD_LOCAL std::string *current_output;

static const char *store_string(std::string str) {
  td::init_thread_local<std::string>(current_output);
  *current_output = std::move(str);
  return current_output->c_str();
}

const char *td_object_to_json(const void *obj) {
  const td::td_api::Object &object = *static_cast<const td::td_api::Object *>(obj);
  auto buf = td::StackAllocator::alloc(1 << 18);
  td::JsonBuilder jb(td::StringBuilder(buf.as_slice(), true), -1);
  jb.enter_value() << ToJson(object);
  auto &sb = jb.string_builder();
  auto slice = sb.as_cslice();
  CHECK(!slice.empty() && slice.back() == '}');
  sb.pop_back();
  sb << '}';
  return store_string(sb.as_cslice().str());
}

