//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <iostream>
#include <td/telegram/td_json_client.h>

// Basic example of TDLib JSON interface usage.
// Native interface should be preferred instead in C++, so here is only an example of
// the main event cycle, which should be essentially the same for all languages.

int main() {
  // disable TDLib logging
  td_json_client_execute(nullptr, "{\"@type\":\"setLogVerbosityLevel\", \"new_verbosity_level\":0}");

  void *client = td_json_client_create();
  // somehow share the client with other threads, which will be able to send requests via td_json_client_send

  const bool test_incorrect_queries = false;
  if (test_incorrect_queries) {
    td_json_client_execute(nullptr, "{\"@type\":\"setLogVerbosityLevel\", \"new_verbosity_level\":3}");
    td_json_client_execute(nullptr, "");
    td_json_client_execute(nullptr, "test");
    td_json_client_execute(nullptr, "\"test\"");
    td_json_client_execute(nullptr, "{\"@type\":\"test\", \"@extra\":1}");

    td_json_client_send(client, "{\"@type\":\"getFileMimeType\"}");
    td_json_client_send(client, "{\"@type\":\"getFileMimeType\", \"@extra\":1}");
    td_json_client_send(client, "{\"@type\":\"getFileMimeType\", \"@extra\":null}");
    td_json_client_send(client, "{\"@type\":\"test\"}");
    td_json_client_send(client, "[]");
    td_json_client_send(client, "{\"@type\":\"test\", \"@extra\":1}");
    td_json_client_send(client, "{\"@type\":\"sendMessage\", \"chat_id\":true, \"@extra\":1}");
    td_json_client_send(client, "test");
  }

  const double WAIT_TIMEOUT = 10.0;  // seconds
  while (true) {
    const char *result = td_json_client_receive(client, WAIT_TIMEOUT);
    if (result != nullptr) {
      // parse the result as a JSON object and process it as an incoming update or an answer to a previously sent request

      // if (result is UpdateAuthorizationState with authorizationStateClosed) {
      //   break;
      // }

      std::cout << result << std::endl;
    }
  }

  td_json_client_destroy(client);
}
