//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundId.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

class Td;

class BackgroundManager final : public Actor {
 public:
  BackgroundManager(Td *td, ActorShared<> parent);

  void get_backgrounds(bool for_dark_theme, Promise<td_api::object_ptr<td_api::backgrounds>> &&promise);

  static Result<string> get_background_url(const string &name,
                                           td_api::object_ptr<td_api::BackgroundType> background_type);

  void reload_background(BackgroundId background_id, int64 access_hash, Promise<Unit> &&promise);

  std::pair<BackgroundId, BackgroundType> search_background(const string &name, Promise<Unit> &&promise);

  BackgroundId set_background(const td_api::InputBackground *input_background,
                              const td_api::BackgroundType *background_type, bool for_dark_theme,
                              Promise<Unit> &&promise);

  void remove_background(BackgroundId background_id, Promise<Unit> &&promise);

  void reset_backgrounds(Promise<Unit> &&promise);

  td_api::object_ptr<td_api::background> get_background_object(BackgroundId background_id, bool for_dark_theme,
                                                               const BackgroundType *type) const;

  std::pair<BackgroundId, BackgroundType> on_get_background(
      BackgroundId expected_background_id, const string &expected_background_name,
      telegram_api::object_ptr<telegram_api::WallPaper> wallpaper_ptr, bool replace_type);

  FileSourceId get_background_file_source_id(BackgroundId background_id, int64 access_hash);

  void on_uploaded_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme,
                                   telegram_api::object_ptr<telegram_api::WallPaper> wallpaper,
                                   Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

  void store_background(BackgroundId background_id, LogEventStorerCalcLength &storer);

  void store_background(BackgroundId background_id, LogEventStorerUnsafe &storer);

  void parse_background(BackgroundId &background_id, LogEventParser &parser);

 private:
  struct Background {
    BackgroundId id;
    int64 access_hash = 0;
    string name;
    FileId file_id;
    bool is_creator = false;
    bool is_default = false;
    bool is_dark = false;
    bool has_new_local_id = true;
    BackgroundType type;
    FileSourceId file_source_id;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  class BackgroundLogEvent;
  class BackgroundsLogEvent;

  class UploadBackgroundFileCallback;

  void start_up() final;

  void tear_down() final;

  static string get_background_database_key(bool for_dark_theme);

  static string get_local_backgrounds_database_key(bool for_dark_theme);

  void save_background_id(bool for_dark_theme);

  void save_local_backgrounds(bool for_dark_theme);

  void reload_background_from_server(BackgroundId background_id, const string &background_name,
                                     telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper,
                                     Promise<Unit> &&promise) const;

  td_api::object_ptr<td_api::updateSelectedBackground> get_update_selected_background_object(bool for_dark_theme) const;

  td_api::object_ptr<td_api::backgrounds> get_backgrounds_object(bool for_dark_theme) const;

  void send_update_selected_background(bool for_dark_theme) const;

  void set_max_local_background_id(BackgroundId background_id);

  BackgroundId get_next_local_background_id();

  BackgroundId add_local_background(const BackgroundType &type);

  void add_background(const Background &background, bool replace_type);

  Background *get_background_ref(BackgroundId background_id);

  const Background *get_background(BackgroundId background_id) const;

  static string get_background_name_database_key(const string &name);

  void on_load_background_from_database(string name, string value);

  void on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result);

  Result<FileId> prepare_input_file(const tl_object_ptr<td_api::InputFile> &input_file);

  BackgroundId set_background(BackgroundId background_id, BackgroundType type, bool for_dark_theme,
                              Promise<Unit> &&promise);

  void on_installed_background(BackgroundId background_id, BackgroundType type, bool for_dark_theme,
                               Result<Unit> &&result, Promise<Unit> &&promise);

  void set_background_id(BackgroundId background_id, const BackgroundType &type, bool for_dark_theme);

  void on_removed_background(BackgroundId background_id, Result<Unit> &&result, Promise<Unit> &&promise);

  void on_reset_background(Result<Unit> &&result, Promise<Unit> &&promise);

  void upload_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme, Promise<Unit> &&promise);

  void on_upload_background_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);

  void on_upload_background_file_error(FileId file_id, Status status);

  void do_upload_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme,
                                 tl_object_ptr<telegram_api::InputFile> &&input_file, Promise<Unit> &&promise);

  std::unordered_map<BackgroundId, Background, BackgroundIdHash> backgrounds_;

  std::unordered_map<BackgroundId, std::pair<int64, FileSourceId>, BackgroundIdHash>
      background_id_to_file_source_id_;  // id -> [access_hash, file_source_id]

  std::unordered_map<string, BackgroundId> name_to_background_id_;

  std::unordered_map<FileId, BackgroundId, FileIdHash> file_id_to_background_id_;

  std::unordered_set<string> loaded_from_database_backgrounds_;
  std::unordered_map<string, vector<Promise<Unit>>> being_loaded_from_database_backgrounds_;

  BackgroundId set_background_id_[2];
  BackgroundType set_background_type_[2];

  vector<std::pair<BackgroundId, BackgroundType>> installed_backgrounds_;

  vector<std::pair<bool, Promise<td_api::object_ptr<td_api::backgrounds>>>> pending_get_backgrounds_queries_;

  std::shared_ptr<UploadBackgroundFileCallback> upload_background_file_callback_;

  struct UploadedFileInfo {
    BackgroundType type_;
    bool for_dark_theme_;
    Promise<Unit> promise_;

    UploadedFileInfo(BackgroundType type, bool for_dark_theme, Promise<Unit> &&promise)
        : type_(type), for_dark_theme_(for_dark_theme), promise_(std::move(promise)) {
    }
  };
  std::unordered_map<FileId, UploadedFileInfo, FileIdHash> being_uploaded_files_;

  BackgroundId max_local_background_id_;
  vector<BackgroundId> local_background_ids_[2];

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
