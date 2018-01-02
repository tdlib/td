//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <iostream>
#include <string>
#include <td/telegram/td_json_client.h>

int main(void) {
  void *client = td_json_client_create();
  while (true) {
    std::string str = td_json_client_receive(client, 10);
    if (!str.empty()) {
      std::cout << str << std::endl;
      break;
    }
  }
  return 0;
}
