//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/SavedMessagesTopicId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class DraftMessageManager final : public Actor {
 public:
  DraftMessageManager(Td *td, ActorShared<> parent);

  void save_draft_message(DialogId dialog_id, const MessageTopic &message_topic,
                          const unique_ptr<DraftMessage> &draft_message, Promise<Unit> &&promise);

  void reload_draft_message(DialogId dialog_id, const MessageTopic &message_topic, Promise<Unit> &&promise);

  void load_all_draft_messages();

  void clear_all_draft_messages(Promise<Unit> &&promise);

  FileSourceId get_draft_message_file_source_id(DialogId dialog_id, const MessageTopic &message_topic);

  void change_draft_message_files(DialogId dialog_id, const MessageTopic &message_topic,
                                  const vector<FileId> &old_file_ids, const vector<FileId> &new_file_ids,
                                  bool need_delete_files);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;

  FileSourceId *get_file_source_id(DialogId dialog_id, const MessageTopic &message_topic);

  FlatHashMap<DialogId, FileSourceId, DialogIdHash> dialog_draft_message_file_source_ids_;
  FlatHashMap<DialogId, FlatHashMap<ForumTopicId, FileSourceId, ForumTopicIdHash>, DialogIdHash>
      forum_topic_draft_message_file_source_ids_;
  FlatHashMap<DialogId, FlatHashMap<SavedMessagesTopicId, FileSourceId, SavedMessagesTopicIdHash>, DialogIdHash>
      monoforum_topic_draft_message_file_source_ids_;
};

}  // namespace td
