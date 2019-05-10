//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundId.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>
#include <unordered_map>

namespace td {

class Td;

class BackgroundManager : public Actor {
 public:
  BackgroundManager(Td *td, ActorShared<> parent);

  void get_backgrounds(Promise<Unit> &&promise);

  Result<string> get_background_url(const string &name,
                                    td_api::object_ptr<td_api::BackgroundType> background_type) const;

  void reload_background(BackgroundId background_id, int64 access_hash, Promise<Unit> &&promise);

  BackgroundId search_background(const string &name, Promise<Unit> &&promise);

  BackgroundId set_background(const td_api::InputBackground *input_background,
                              const td_api::BackgroundType *background_type, Promise<Unit> &&promise);

  void set_background_id(BackgroundId background_id, const BackgroundType &type);

  td_api::object_ptr<td_api::background> get_background_object(BackgroundId background_id) const;

  td_api::object_ptr<td_api::backgrounds> get_backgrounds_object() const;

  BackgroundId on_get_background(BackgroundId expected_background_id,
                                 telegram_api::object_ptr<telegram_api::wallPaper> wallpaper);

  FileSourceId get_background_file_source_id(BackgroundId background_id, int64 access_hash);

  void on_uploaded_background_file(FileId file_id, const BackgroundType &type,
                                   telegram_api::object_ptr<telegram_api::wallPaper> wallpaper,
                                   Promise<Unit> &&promise);

 private:
  struct Background {
    BackgroundId id;
    int64 access_hash = 0;
    string name;
    FileId file_id;
    bool is_creator = false;
    bool is_default = false;
    bool is_dark = false;
    BackgroundType type;
    FileSourceId file_source_id;
  };

  class BackgroundLogEvent;

  class UploadBackgroundFileCallback;

  void start_up() override;

  void tear_down() override;

  static string get_background_database_key();

  void save_background_id() const;

  void reload_background_from_server(BackgroundId background_id,
                                     telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper,
                                     Promise<Unit> &&promise) const;

  td_api::object_ptr<td_api::updateSelectedBackground> get_update_selected_background() const;

  void send_update_selected_background() const;

  BackgroundId add_solid_background(int32 color);

  Background *add_background(BackgroundId background_id);

  Background *get_background_ref(BackgroundId background_id);

  const Background *get_background(BackgroundId background_id) const;

  void on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result);

  Result<FileId> prepare_input_file(const tl_object_ptr<td_api::InputFile> &input_file);

  BackgroundId set_background(BackgroundId background_id, const BackgroundType &type, Promise<Unit> &&promise);

  void upload_background_file(FileId file_id, const BackgroundType &type, Promise<Unit> &&promise);

  void on_upload_background_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);

  void on_upload_background_file_error(FileId file_id, Status status);

  void do_upload_background_file(FileId file_id, const BackgroundType &type,
                                 tl_object_ptr<telegram_api::InputFile> &&input_file, Promise<Unit> &&promise);

  std::unordered_map<BackgroundId, Background, BackgroundIdHash> backgrounds_;

  std::unordered_map<BackgroundId, std::pair<int64, FileSourceId>, BackgroundIdHash>
      background_id_to_file_source_id_;  // id -> [access_hash, file_source_id]

  std::unordered_map<string, BackgroundId> name_to_background_id_;

  std::unordered_map<FileId, BackgroundId, FileIdHash> file_id_to_background_id_;

  BackgroundId set_background_id_;
  BackgroundType set_background_type_;

  vector<BackgroundId> installed_background_ids_;

  vector<Promise<Unit>> pending_get_backgrounds_queries_;

  std::shared_ptr<UploadBackgroundFileCallback> upload_background_file_callback_;

  struct UploadedFileInfo {
    BackgroundType type;
    Promise<Unit> promise;
  };
  std::unordered_map<FileId, UploadedFileInfo, FileIdHash> being_uploaded_files_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
