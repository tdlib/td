//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/MessageContentUploadId.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/SavedMessagesTopicId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

#include <memory>

namespace td {

class DraftMessage;
class Td;

class DraftMessageManager final : public Actor {
 public:
  DraftMessageManager(Td *td, ActorShared<> parent);

  void save_draft_message(DialogId dialog_id, const MessageTopic &message_topic,
                          const unique_ptr<DraftMessage> &draft_message, Promise<Unit> &&promise);

  void cancel_save_draft_message(MessageContentUploadId upload_id, Status status);

  void reload_draft_message(DialogId dialog_id, const MessageTopic &message_topic, Promise<Unit> &&promise);

  void load_all_draft_messages();

  void clear_all_draft_messages(Promise<Unit> &&promise);

  FileSourceId get_draft_message_file_source_id(DialogId dialog_id, const MessageTopic &message_topic);

  void change_draft_message_files(DialogId dialog_id, const MessageTopic &message_topic,
                                  const vector<FileId> &old_file_ids, const vector<FileId> &new_file_ids,
                                  bool need_delete_files);

  class UploadDraftMessageCallback;

 private:
  void tear_down() final;

  FileSourceId *get_file_source_id(DialogId dialog_id, const MessageTopic &message_topic);

  Td *td_;
  ActorShared<> parent_;

  FlatHashMap<DialogId, FileSourceId, DialogIdHash> dialog_draft_message_file_source_ids_;
  FlatHashMap<MessageTopic, FileSourceId, MessageTopicHash> topic_draft_message_file_source_ids_;

  struct SaveDraftMessageRequest {
    DialogId dialog_id_;
    MessageTopic message_topic_;
    unique_ptr<DraftMessage> draft_message_;
    Promise<Unit> promise_;
  };
  FlatHashMap<MessageContentUploadId, SaveDraftMessageRequest, MessageContentUploadIdHash> save_draft_message_queries_;

  FlatHashMap<DialogId, MessageContentUploadId, DialogIdHash> dialog_draft_message_upload_ids_;
  FlatHashMap<MessageTopic, MessageContentUploadId, MessageTopicHash> topic_draft_message_upload_ids_;

  std::shared_ptr<UploadDraftMessageCallback> upload_draft_message_callback_;
};

}  // namespace td
