//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Client.h"

#include "td/utils/Slice.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace td {

class ClientJson final {
 public:
  void send(Slice request);

  CSlice receive(double timeout);

  static CSlice execute(Slice request);

 private:
  Client client_;
  std::mutex mutex_;  // for extra_
  std::unordered_map<std::int64_t, std::string> extra_;
  std::atomic<std::uint64_t> extra_id_{1};
};

}  // namespace td
