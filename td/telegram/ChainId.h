//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/PollId.h"

#include "td/utils/common.h"

#include <functional>

namespace td {

class ChainId {
  uint64 id = 0;

 public:
  ChainId(DialogId dialog_id, MessageContentType message_content_type)
      : id((static_cast<uint64>(dialog_id.get()) << 10) + get_message_content_chain_id(message_content_type)) {
  }

  ChainId(DialogId dialog_id) : id((static_cast<uint64>(dialog_id.get()) << 10) + 10) {
  }

  ChainId(FullMessageId full_message_id) : ChainId(full_message_id.get_dialog_id()) {
    id += static_cast<uint64>(full_message_id.get_message_id().get()) << 10;
  }

  ChainId(FolderId folder_id) : id((static_cast<uint64>(folder_id.get() + (1 << 30)) << 10)) {
  }

  ChainId(PollId poll_id) : id(static_cast<uint64>(poll_id.get())) {
  }

  ChainId(const string &str) : id(std::hash<string>()(str)) {
  }

  uint64 get() const {
    return id;
  }
};

}  // namespace td
