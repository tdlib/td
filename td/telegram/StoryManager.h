//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/StoryFullId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/StoryInteractionInfo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserPrivacySettingRule.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/WaitFreeHashMap.h"

#include <utility>

namespace td {

class StoryContent;
class Td;

class StoryManager final : public Actor {
  struct Story {
    int32 date_ = 0;
    int32 expire_date_ = 0;
    bool is_pinned_ = false;
    bool is_public_ = false;
    bool is_for_close_friends_ = false;
    StoryInteractionInfo interaction_info_;
    UserPrivacySettingRules privacy_rules_;
    unique_ptr<StoryContent> content_;
    FormattedText caption_;
  };

  struct PendingStory {
    DialogId dialog_id_;
    StoryId story_id_;
    uint64 log_event_id_ = 0;
    uint32 send_story_num_ = 0;
    int64 random_id_ = 0;
    bool was_reuploaded_ = false;
    unique_ptr<Story> story_;

    PendingStory(DialogId dialog_id, StoryId story_id, uint64 log_event_id, uint32 send_story_num, int64 random_id,
                 unique_ptr<Story> &&story);
  };

 public:
  StoryManager(Td *td, ActorShared<> parent);
  StoryManager(const StoryManager &) = delete;
  StoryManager &operator=(const StoryManager &) = delete;
  StoryManager(StoryManager &&) = delete;
  StoryManager &operator=(StoryManager &&) = delete;
  ~StoryManager() final;

  void send_story(td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                  td_api::object_ptr<td_api::formattedText> &&input_caption,
                  td_api::object_ptr<td_api::userPrivacySettingRules> &&rules, bool is_pinned,
                  Promise<td_api::object_ptr<td_api::story>> &&promise);

  void on_send_story_file_part_missing(unique_ptr<PendingStory> &&pending_story, int bad_part);

  void set_story_privacy_rules(StoryId story_id, td_api::object_ptr<td_api::userPrivacySettingRules> &&rules,
                               Promise<Unit> &&promise);

  void toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise);

  void get_dialog_pinned_stories(DialogId owner_dialog_id, StoryId from_story_id, int32 limit,
                                 Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void get_dialog_expiring_stories(DialogId owner_dialog_id, Promise<td_api::object_ptr<td_api::stories>> &&promise);

  StoryId on_get_story(DialogId owner_dialog_id, telegram_api::object_ptr<telegram_api::storyItem> &&story_item);

  std::pair<int32, vector<StoryId>> on_get_stories(DialogId owner_dialog_id,
                                                   telegram_api::object_ptr<telegram_api::stories_stories> &&stories);

  td_api::object_ptr<td_api::story> get_story_object(StoryFullId story_full_id) const;

  td_api::object_ptr<td_api::stories> get_stories_object(int32 total_count,
                                                         const vector<StoryFullId> &story_full_ids) const;

  FileSourceId get_story_file_source_id(StoryFullId story_full_id);

  void reload_story(StoryFullId story_full_id, Promise<Unit> &&promise);

 private:
  class UploadMediaCallback;

  class SendStoryQuery;

  void tear_down() final;

  bool is_story_owned(DialogId owner_dialog_id) const;

  const Story *get_story(StoryFullId story_full_id) const;

  Story *get_story_editable(StoryFullId story_full_id);

  void on_story_changed(StoryFullId story_full_id, const Story *story, bool is_changed, bool need_save_to_database);

  td_api::object_ptr<td_api::story> get_story_object(StoryFullId story_full_id, const Story *story) const;

  vector<StoryId> on_get_stories(DialogId owner_dialog_id,
                                 vector<telegram_api::object_ptr<telegram_api::StoryItem>> &&stories);

  void on_get_dialog_pinned_stories(DialogId owner_dialog_id,
                                    telegram_api::object_ptr<telegram_api::stories_stories> &&stories,
                                    Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void on_get_dialog_expiring_stories(DialogId owner_dialog_id,
                                      telegram_api::object_ptr<telegram_api::stories_userStories> &&stories,
                                      Promise<td_api::object_ptr<td_api::stories>> &&promise);

  vector<FileId> get_story_file_ids(const Story *story) const;

  void delete_story_files(const Story *story) const;

  void change_story_files(StoryFullId story_full_id, const Story *story, const vector<FileId> &old_file_ids);

  static bool is_local_story_id(StoryId story_id);

  void do_send_story(unique_ptr<PendingStory> &&pending_story, vector<int> bad_parts);

  void on_upload_story(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file);

  void on_upload_story_error(FileId file_id, Status status);

  void on_toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise);

  std::shared_ptr<UploadMediaCallback> upload_media_callback_;

  WaitFreeHashMap<StoryFullId, FileSourceId, StoryFullIdHash> story_full_id_to_file_source_id_;

  WaitFreeHashMap<StoryFullId, unique_ptr<Story>, StoryFullIdHash> stories_;

  uint32 send_story_count_ = 0;

  FlatHashMap<FileId, unique_ptr<PendingStory>, FileIdHash> being_uploaded_files_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
