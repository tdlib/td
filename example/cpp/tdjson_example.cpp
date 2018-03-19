//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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
  void *client = td_json_client_create();
  // somehow share the client with other threads, which will be able to send requests via td_json_client_send

  const double WAIT_TIMEOUT = 10.0;  // seconds
  while (true) {
    const char *result = td_json_client_receive(client, WAIT_TIMEOUT);
    if (result != nullptr) {
      // parse the result as JSON object and process it as an incoming update or an answer to a previously sent request

      // if (result is UpdateAuthorizationState with authorizationStateClosed) {
      //   break;
      // }

      std::cout << result << std::endl;
    }
  }

  td_json_client_destroy(client);
}
