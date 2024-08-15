//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AlarmManager.h"

namespace td {

AlarmManager::AlarmManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void AlarmManager::tear_down() {
  parent_.reset();
}

}  // namespace td
