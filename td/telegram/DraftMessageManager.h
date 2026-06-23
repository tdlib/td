//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/MessageTopic.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class DraftMessageManager final : public Actor {
 public:
  DraftMessageManager(Td *td, ActorShared<> parent);

  void save_draft_message(DialogId dialog_id, const MessageTopic &message_topic,
                          const unique_ptr<DraftMessage> &draft_message, Promise<Unit> &&promise);

  void load_all_draft_messages();

  void clear_all_draft_messages(Promise<Unit> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
