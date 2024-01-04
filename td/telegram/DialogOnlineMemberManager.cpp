//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogOnlineMemberManager.h"

namespace td {

DialogOnlineMemberManager::DialogOnlineMemberManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
}

void DialogOnlineMemberManager::tear_down() {
  parent_.reset();
}

}  // namespace td
