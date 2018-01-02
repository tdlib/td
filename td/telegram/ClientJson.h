//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Client.h"

#include "td/utils/port/thread_local.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace td {

class ClientJson final {
 public:
  void send(Slice request);

  CSlice receive(double timeout);

  CSlice execute(Slice request);

 private:
  Client client_;
  std::mutex mutex_;  // for extra_
  std::unordered_map<std::int64_t, std::string> extra_;
  std::atomic<std::uint64_t> extra_id_{1};
  static TD_THREAD_LOCAL std::string *current_output_;

  CSlice store_string(std::string str);

  Result<Client::Request> to_request(Slice request);
  std::string from_response(Client::Response response);
};
}  // namespace td
