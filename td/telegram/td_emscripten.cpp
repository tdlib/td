//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/telegram/td_json_client.h"
#include "td/telegram/td_log.h"

#include <emscripten.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE void *td_create() {
  return td_json_client_create();
}
EMSCRIPTEN_KEEPALIVE void td_send(void *client, const char *query) {
  td_json_client_send(client, query);
}
EMSCRIPTEN_KEEPALIVE const char *td_receive(void *client) {
  return td_json_client_receive(client, 0);
}
EMSCRIPTEN_KEEPALIVE const char *td_execute(void *client, const char *query) {
  return td_json_client_execute(client, query);
}
EMSCRIPTEN_KEEPALIVE void td_destroy(void *client) {
  td_json_client_destroy(client);
}
EMSCRIPTEN_KEEPALIVE double td_get_timeout() {
  return td::ConcurrentScheduler::emscripten_get_main_timeout();
}
}

int main(void) {
  emscripten_exit_with_live_runtime();
  return 0;
}
