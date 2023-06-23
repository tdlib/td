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
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageViewer.h"
#include "td/telegram/StoryFullId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/StoryInteractionInfo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserPrivacySettingRule.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"
#include "td/utils/WaitFreeHashSet.h"

#include <utility>

namespace td {

struct BinlogEvent;
class ReportReason;
class StoryContent;
class Td;

class StoryManager final : public Actor {
  struct Story {
    int32 date_ = 0;
    int32 expire_date_ = 0;
    int32 receive_date_ = 0;
    bool is_pinned_ = false;
    bool is_public_ = false;
    bool is_for_close_friends_ = false;
    mutable bool is_update_sent_ = false;  // whether the story is known to the app
    StoryInteractionInfo interaction_info_;
    UserPrivacySettingRules privacy_rules_;
    unique_ptr<StoryContent> content_;
    FormattedText caption_;
    mutable int64 edit_generation_ = 0;
    int64 global_id_ = 0;
  };

  struct BeingEditedStory {
    unique_ptr<StoryContent> content_;
    FormattedText caption_;
    bool edit_caption_ = false;
    vector<Promise<Unit>> promises_;
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

  struct PendingStoryViews {
    FlatHashSet<StoryId, StoryIdHash> story_ids_;
    bool has_query_ = false;
  };

  struct ActiveStories {
    StoryId max_read_story_id_;
    vector<StoryId> story_ids_;
  };

  struct CachedStoryViewers {
    int32 total_count_ = -1;
    MessageViewers viewers_;
  };

 public:
  StoryManager(Td *td, ActorShared<> parent);
  StoryManager(const StoryManager &) = delete;
  StoryManager &operator=(const StoryManager &) = delete;
  StoryManager(StoryManager &&) = delete;
  StoryManager &operator=(StoryManager &&) = delete;
  ~StoryManager() final;

  void get_story(DialogId owner_dialog_id, StoryId story_id, Promise<td_api::object_ptr<td_api::story>> &&promise);

  void send_story(td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                  td_api::object_ptr<td_api::formattedText> &&input_caption,
                  td_api::object_ptr<td_api::userPrivacySettingRules> &&rules, int32 active_period, bool is_pinned,
                  Promise<td_api::object_ptr<td_api::story>> &&promise);

  void on_send_story_file_part_missing(unique_ptr<PendingStory> &&pending_story, int bad_part);

  void edit_story(StoryId story_id, td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                  td_api::object_ptr<td_api::formattedText> &&input_caption, Promise<Unit> &&promise);

  void set_story_privacy_rules(StoryId story_id, td_api::object_ptr<td_api::userPrivacySettingRules> &&rules,
                               Promise<Unit> &&promise);

  void toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise);

  void delete_story(StoryId story_id, Promise<Unit> &&promise);

  void toggle_dialog_stories_hidden(DialogId dialog_id, bool are_hidden, Promise<Unit> &&promise);

  void get_dialog_pinned_stories(DialogId owner_dialog_id, StoryId from_story_id, int32 limit,
                                 Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void get_story_archive(StoryId from_story_id, int32 limit, Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void get_dialog_expiring_stories(DialogId owner_dialog_id,
                                   Promise<td_api::object_ptr<td_api::activeStories>> &&promise);

  void open_story(DialogId owner_dialog_id, StoryId story_id, Promise<Unit> &&promise);

  void close_story(DialogId owner_dialog_id, StoryId story_id, Promise<Unit> &&promise);

  void view_story_message(StoryFullId story_full_id);

  void get_story_viewers(StoryId story_id, const td_api::messageViewer *offset, int32 limit,
                         Promise<td_api::object_ptr<td_api::messageViewers>> &&promise);

  void report_story(StoryFullId story_full_id, ReportReason &&reason, Promise<Unit> &&promise);

  void remove_story_notifications_by_story_ids(DialogId dialog_id, const vector<StoryId> &story_ids);

  StoryId on_get_story(DialogId owner_dialog_id, telegram_api::object_ptr<telegram_api::StoryItem> &&story_item_ptr);

  std::pair<int32, vector<StoryId>> on_get_stories(DialogId owner_dialog_id, vector<StoryId> &&expected_story_ids,
                                                   telegram_api::object_ptr<telegram_api::stories_stories> &&stories);

  DialogId on_get_user_stories(DialogId owner_dialog_id,
                               telegram_api::object_ptr<telegram_api::userStories> &&user_stories);

  bool on_update_read_stories(DialogId owner_dialog_id, StoryId max_read_story_id);

  Status can_get_story_viewers(StoryFullId story_full_id, const Story *story) const;

  void on_get_story_views(const vector<StoryId> &story_ids,
                          telegram_api::object_ptr<telegram_api::stories_storyViews> &&story_views);

  bool have_story(StoryFullId story_full_id) const;

  bool have_story_force(StoryFullId story_full_id) const;

  bool is_inaccessible_story(StoryFullId story_full_id) const;

  int32 get_story_duration(StoryFullId story_full_id) const;

  void register_story(StoryFullId story_full_id, FullMessageId full_message_id, const char *source);

  void unregister_story(StoryFullId story_full_id, FullMessageId full_message_id, const char *source);

  td_api::object_ptr<td_api::story> get_story_object(StoryFullId story_full_id) const;

  td_api::object_ptr<td_api::stories> get_stories_object(int32 total_count,
                                                         const vector<StoryFullId> &story_full_ids) const;

  FileSourceId get_story_file_source_id(StoryFullId story_full_id);

  telegram_api::object_ptr<telegram_api::InputMedia> get_input_media(StoryFullId story_full_id) const;

  void reload_story(StoryFullId story_full_id, Promise<Unit> &&promise);

  void try_synchronize_archive_all_stories();

  void on_binlog_events(vector<BinlogEvent> &&events);

 private:
  class UploadMediaCallback;

  class SendStoryQuery;
  class EditStoryQuery;

  class DeleteStoryOnServerLogEvent;
  class ReadStoriesOnServerLogEvent;

  static constexpr int32 OPENED_STORY_POLL_PERIOD = 60;
  static constexpr int32 VIEWED_STORY_POLL_PERIOD = 300;

  void start_up() final;

  void hangup() final;

  void tear_down() final;

  static void on_story_reload_timeout_callback(void *story_manager_ptr, int64 story_global_id);

  void on_story_reload_timeout(int64 story_global_id);

  static void on_story_expire_timeout_callback(void *story_manager_ptr, int64 story_global_id);

  void on_story_expire_timeout(int64 story_global_id);

  static void on_story_can_get_viewers_timeout_callback(void *story_manager_ptr, int64 story_global_id);

  void on_story_can_get_viewers_timeout(int64 story_global_id);

  bool is_story_owned(DialogId owner_dialog_id) const;

  bool is_active_story(StoryFullId story_full_id) const;

  int32 get_story_viewers_expire_date(const Story *story) const;

  static bool is_active_story(const Story *story);

  const Story *get_story(StoryFullId story_full_id) const;

  Story *get_story_editable(StoryFullId story_full_id);

  const ActiveStories *get_active_stories(DialogId owner_dialog_id) const;

  void on_story_changed(StoryFullId story_full_id, const Story *story, bool is_changed, bool need_save_to_database);

  void register_story_global_id(StoryFullId story_full_id, Story *story);

  void unregister_story_global_id(const Story *story);

  td_api::object_ptr<td_api::storyInfo> get_story_info_object(StoryFullId story_full_id) const;

  td_api::object_ptr<td_api::storyInfo> get_story_info_object(StoryFullId story_full_id, const Story *story) const;

  td_api::object_ptr<td_api::story> get_story_object(StoryFullId story_full_id, const Story *story) const;

  td_api::object_ptr<td_api::activeStories> get_active_stories_object(DialogId owner_dialog_id) const;

  StoryId on_get_story(DialogId owner_dialog_id, telegram_api::object_ptr<telegram_api::storyItem> &&story_item);

  StoryId on_get_skipped_story(DialogId owner_dialog_id,
                               telegram_api::object_ptr<telegram_api::storyItemSkipped> &&story_item);

  StoryId on_get_deleted_story(DialogId owner_dialog_id,
                               telegram_api::object_ptr<telegram_api::storyItemDeleted> &&story_item);

  void on_delete_story(DialogId owner_dialog_id, StoryId story_id);

  void on_get_dialog_pinned_stories(DialogId owner_dialog_id,
                                    telegram_api::object_ptr<telegram_api::stories_stories> &&stories,
                                    Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void on_get_story_archive(telegram_api::object_ptr<telegram_api::stories_stories> &&stories,
                            Promise<td_api::object_ptr<td_api::stories>> &&promise);

  void on_get_dialog_expiring_stories(DialogId owner_dialog_id,
                                      telegram_api::object_ptr<telegram_api::stories_userStories> &&stories,
                                      Promise<td_api::object_ptr<td_api::activeStories>> &&promise);

  vector<FileId> get_story_file_ids(const Story *story) const;

  static uint64 save_delete_story_on_server_log_event(DialogId dialog_id, StoryId story_id);

  void delete_story_on_server(DialogId dialog_id, StoryId story_id, uint64 log_event_id, Promise<Unit> &&promise);

  void delete_story_files(const Story *story) const;

  void change_story_files(StoryFullId story_full_id, const Story *story, const vector<FileId> &old_file_ids);

  void do_get_story(StoryFullId story_full_id, Result<Unit> &&result,
                    Promise<td_api::object_ptr<td_api::story>> &&promise);

  void on_reload_story(StoryFullId story_full_id, Result<Unit> &&result);

  void do_send_story(unique_ptr<PendingStory> &&pending_story, vector<int> bad_parts);

  void on_upload_story(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file);

  void on_upload_story_error(FileId file_id, Status status);

  void do_edit_story(FileId file_id, unique_ptr<PendingStory> &&pending_story,
                     telegram_api::object_ptr<telegram_api::InputFile> input_file);

  void on_story_edited(FileId file_id, unique_ptr<PendingStory> pending_story, Result<Unit> result);

  void on_toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise);

  void on_update_active_stories(DialogId owner_dialog_id, StoryId max_read_story_id, vector<StoryId> &&story_ids);

  void send_update_active_stories(DialogId owner_dialog_id);

  void increment_story_views(DialogId owner_dialog_id, PendingStoryViews &story_views);

  void on_increment_story_views(DialogId owner_dialog_id);

  static uint64 save_read_stories_on_server_log_event(DialogId dialog_id, StoryId max_story_id);

  void read_stories_on_server(DialogId owner_dialog_id, StoryId story_id, uint64 log_event_id);

  void schedule_interaction_info_update();

  static void update_interaction_info_static(void *story_manager);

  void update_interaction_info();

  void on_synchronized_archive_all_stories(bool set_archive_all_stories, Result<Unit> result);

  void on_get_story_viewers(StoryId story_id, MessageViewer offset,
                            Result<telegram_api::object_ptr<telegram_api::stories_storyViewsList>> r_view_list,
                            Promise<td_api::object_ptr<td_api::messageViewers>> &&promise);

  std::shared_ptr<UploadMediaCallback> upload_media_callback_;

  WaitFreeHashMap<StoryFullId, FileSourceId, StoryFullIdHash> story_full_id_to_file_source_id_;

  WaitFreeHashMap<StoryFullId, unique_ptr<Story>, StoryFullIdHash> stories_;

  WaitFreeHashMap<int64, StoryFullId> stories_by_global_id_;

  WaitFreeHashSet<StoryFullId, StoryFullIdHash> inaccessible_story_full_ids_;

  WaitFreeHashSet<StoryFullId, StoryFullIdHash> deleted_story_full_ids_;

  WaitFreeHashMap<StoryFullId, WaitFreeHashSet<FullMessageId, FullMessageIdHash>, StoryFullIdHash> story_messages_;

  WaitFreeHashMap<DialogId, unique_ptr<ActiveStories>, DialogIdHash> active_stories_;

  WaitFreeHashMap<DialogId, StoryId, DialogIdHash> max_read_story_ids_;

  FlatHashMap<StoryFullId, unique_ptr<BeingEditedStory>, StoryFullIdHash> being_edited_stories_;

  FlatHashMap<DialogId, PendingStoryViews, DialogIdHash> pending_story_views_;

  FlatHashMap<StoryFullId, uint32, StoryFullIdHash> opened_owned_stories_;

  FlatHashMap<StoryFullId, uint32, StoryFullIdHash> opened_stories_;

  FlatHashMap<StoryFullId, unique_ptr<CachedStoryViewers>, StoryFullIdHash> cached_story_viewers_;

  FlatHashMap<StoryFullId, vector<Promise<Unit>>, StoryFullIdHash> reload_story_queries_;

  uint32 send_story_count_ = 0;

  int64 max_story_global_id_ = 0;

  bool has_active_synchronize_archive_all_stories_query_ = false;

  FlatHashMap<FileId, unique_ptr<PendingStory>, FileIdHash> being_uploaded_files_;

  Timeout interaction_info_update_timeout_;

  MultiTimeout story_reload_timeout_{"StoryReloadTimeout"};
  MultiTimeout story_expire_timeout_{"StoryExpireTimeout"};
  MultiTimeout story_can_get_viewers_timeout_{"StoryCanGetViewersTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td