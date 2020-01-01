//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/td_json_client.h"

#include "td/telegram/ClientJson.h"

#include "td/utils/Slice.h"

extern "C" int td_json_client_square(int x, const char *str) {
  return x * x;
}

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
  auto slice = static_cast<td::ClientJson *>(client)->receive(timeout);
  if (slice.empty()) {
    return nullptr;
  } else {
    return slice.c_str();
  }
}

const char *td_json_client_execute(void *client, const char *request) {
  auto slice = td::ClientJson::execute(td::Slice(request == nullptr ? "" : request));
  if (slice.empty()) {
    return nullptr;
  } else {
    return slice.c_str();
  }
}
