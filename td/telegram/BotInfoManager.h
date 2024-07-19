//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class StoryContent;

class Td;

class BotInfoManager final : public Actor {
 public:
  BotInfoManager(Td *td, ActorShared<> parent);
  BotInfoManager(const BotInfoManager &) = delete;
  BotInfoManager &operator=(const BotInfoManager &) = delete;
  BotInfoManager(BotInfoManager &&) = delete;
  BotInfoManager &operator=(BotInfoManager &&) = delete;
  ~BotInfoManager() final;

  void set_default_group_administrator_rights(AdministratorRights administrator_rights, Promise<Unit> &&promise);

  void set_default_channel_administrator_rights(AdministratorRights administrator_rights, Promise<Unit> &&promise);

  void can_bot_send_messages(UserId bot_user_id, Promise<Unit> &&promise);

  void allow_bot_to_send_messages(UserId bot_user_id, Promise<Unit> &&promise);

  FileSourceId get_bot_media_preview_file_source_id(UserId bot_user_id);

  void get_bot_media_previews(UserId bot_user_id, Promise<td_api::object_ptr<td_api::botMediaPreviews>> &&promise);

  void reload_bot_media_previews(UserId bot_user_id, Promise<Unit> &&promise);

  void add_bot_media_preview(UserId bot_user_id, const string &language_code,
                             td_api::object_ptr<td_api::InputStoryContent> &&input_content,
                             Promise<td_api::object_ptr<td_api::StoryContent>> &&promise);

  void edit_bot_media_preview(UserId bot_user_id, const string &language_code, FileId file_id,
                              td_api::object_ptr<td_api::InputStoryContent> &&input_content,
                              Promise<td_api::object_ptr<td_api::StoryContent>> &&promise);

  void set_bot_name(UserId bot_user_id, const string &language_code, const string &name, Promise<Unit> &&promise);

  void get_bot_name(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_description(UserId bot_user_id, const string &language_code, const string &description,
                                Promise<Unit> &&promise);

  void get_bot_info_description(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_about(UserId bot_user_id, const string &language_code, const string &about,
                          Promise<Unit> &&promise);

  void get_bot_info_about(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

 private:
  static constexpr double MAX_QUERY_DELAY = 0.01;

  class UploadMediaCallback;

  class AddPreviewMediaQuery;

  struct PendingBotMediaPreview {
    FileId edited_file_id_;
    UserId bot_user_id_;
    string language_code_;
    unique_ptr<StoryContent> content_;
    uint32 upload_order_ = 0;
    bool was_reuploaded_ = false;
    Promise<td_api::object_ptr<td_api::StoryContent>> promise_;
  };

  struct PendingSetBotInfoQuery {
    UserId bot_user_id_;
    string language_code_;
    int type_ = 0;
    string value_;
    Promise<Unit> promise_;

    PendingSetBotInfoQuery(UserId bot_user_id, const string &language_code, int type, const string &value,
                           Promise<Unit> &&promise)
        : bot_user_id_(bot_user_id)
        , language_code_(language_code)
        , type_(type)
        , value_(value)
        , promise_(std::move(promise)) {
    }
  };

  struct PendingGetBotInfoQuery {
    UserId bot_user_id_;
    string language_code_;
    int type_ = 0;
    Promise<string> promise_;

    PendingGetBotInfoQuery(UserId bot_user_id, const string &language_code, int type, Promise<string> &&promise)
        : bot_user_id_(bot_user_id), language_code_(language_code), type_(type), promise_(std::move(promise)) {
    }
  };

  void tear_down() final;

  void hangup() final;

  void timeout_expired() final;

  Result<telegram_api::object_ptr<telegram_api::InputUser>> get_media_preview_bot_input_user(
      UserId user_id, bool can_be_edited = false);

  void do_add_bot_media_preview(unique_ptr<PendingBotMediaPreview> &&pending_preview, vector<int> bad_parts);

  void on_add_bot_media_preview_file_parts_missing(unique_ptr<PendingBotMediaPreview> &&pending_preview,
                                                   vector<int> &&bad_parts);

  void on_upload_bot_media_preview(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file);

  void on_upload_bot_media_preview_error(FileId file_id, Status status);

  telegram_api::object_ptr<telegram_api::InputMedia> get_fake_input_media(FileId file_id) const;

  void add_pending_set_query(UserId bot_user_id, const string &language_code, int type, const string &value,
                             Promise<Unit> &&promise);

  void add_pending_get_query(UserId bot_user_id, const string &language_code, int type, Promise<string> &&promise);

  vector<PendingSetBotInfoQuery> pending_set_bot_info_queries_;

  vector<PendingGetBotInfoQuery> pending_get_bot_info_queries_;

  FlatHashMap<UserId, FileSourceId, UserIdHash> bot_media_preview_file_source_ids_;

  FlatHashMap<FileId, unique_ptr<PendingBotMediaPreview>, FileIdHash> being_uploaded_files_;

  std::shared_ptr<UploadMediaCallback> upload_media_callback_;

  uint32 bot_media_preview_upload_order_ = 0;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
