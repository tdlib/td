//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/BackgroundType.hpp"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/tl_helpers.h"

namespace td {

class GetBackgroundQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  BackgroundId background_id_;
  string background_name_;

 public:
  explicit GetBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BackgroundId background_id, const string &background_name,
            telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper) {
    background_id_ = background_id;
    background_name_ = background_name;
    LOG(INFO) << "Load " << background_id_ << "/" << background_name_ << " from server: " << to_string(input_wallpaper);
    send_query(G()->net_query_creator().create(telegram_api::account_getWallPaper(std::move(input_wallpaper))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->background_manager_->on_get_background(background_id_, background_name_, result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(INFO) << "Receive error for GetBackgroundQuery for " << background_id_ << "/" << background_name_ << ": "
              << status;
    promise_.set_error(std::move(status));
  }
};

class GetBackgroundsQuery : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_WallPapers>> promise_;

 public:
  explicit GetBackgroundsQuery(Promise<telegram_api::object_ptr<telegram_api::account_WallPapers>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getWallPapers(0)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getWallPapers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class InstallBackgroundQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit InstallBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BackgroundId background_id, int64 access_hash, const BackgroundType &type) {
    send_query(G()->net_query_creator().create(telegram_api::account_installWallPaper(
        telegram_api::make_object<telegram_api::inputWallPaper>(background_id.get(), access_hash),
        get_input_wallpaper_settings(type))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_installWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    LOG_IF(INFO, !result_ptr.ok()) << "Receive false from account.installWallPaper";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class UploadBackgroundQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;
  BackgroundType type_;
  bool for_dark_theme_;

 public:
  explicit UploadBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, tl_object_ptr<telegram_api::InputFile> &&input_file, const BackgroundType &type,
            bool for_dark_theme) {
    CHECK(input_file != nullptr);
    file_id_ = file_id;
    type_ = type;
    for_dark_theme_ = for_dark_theme;
    string mime_type = type.type == BackgroundType::Type::Pattern ? "image/png" : "image/jpeg";
    send_query(G()->net_query_creator().create(
        telegram_api::account_uploadWallPaper(std::move(input_file), mime_type, get_input_wallpaper_settings(type))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_uploadWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->background_manager_->on_uploaded_background_file(file_id_, type_, for_dark_theme_, result_ptr.move_as_ok(),
                                                         std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    CHECK(file_id_.is_valid());
    if (begins_with(status.message(), "FILE_PART_") && ends_with(status.message(), "_MISSING")) {
      // TODO td->background_manager_->on_upload_background_file_part_missing(file_id_, to_integer<int32>(status.message().substr(10)));
      // return;
    } else {
      if (status.code() != 429 && status.code() < 500 && !G()->close_flag()) {
        td->file_manager_->delete_partial_remote_location(file_id_);
      }
    }
    td->file_manager_->cancel_upload(file_id_);
    promise_.set_error(std::move(status));
  }
};

class SaveBackgroundQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BackgroundId background_id, int64 access_hash, const BackgroundType &type, bool unsave) {
    send_query(G()->net_query_creator().create(telegram_api::account_saveWallPaper(
        telegram_api::make_object<telegram_api::inputWallPaper>(background_id.get(), access_hash), unsave,
        get_input_wallpaper_settings(type))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_saveWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for save background: " << result;
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for save background: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class ResetBackgroundsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetBackgroundsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_resetWallPapers()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_resetWallPapers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for reset backgrounds: " << result;
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for reset backgrounds: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class BackgroundManager::UploadBackgroundFileCallback : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override {
    send_closure_later(G()->background_manager(), &BackgroundManager::on_upload_background_file, file_id,
                       std::move(input_file));
  }

  void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override {
    UNREACHABLE();
  }

  void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override {
    UNREACHABLE();
  }

  void on_upload_error(FileId file_id, Status error) override {
    send_closure_later(G()->background_manager(), &BackgroundManager::on_upload_background_file_error, file_id,
                       std::move(error));
  }
};

BackgroundManager::BackgroundManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_background_file_callback_ = std::make_shared<UploadBackgroundFileCallback>();
}

template <class StorerT>
void BackgroundManager::Background::store(StorerT &storer) const {
  bool has_file_id = file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_creator);
  STORE_FLAG(is_default);
  STORE_FLAG(is_dark);
  STORE_FLAG(has_file_id);
  END_STORE_FLAGS();
  td::store(id, storer);
  td::store(access_hash, storer);
  td::store(name, storer);
  if (has_file_id) {
    storer.context()->td().get_actor_unsafe()->documents_manager_->store_document(file_id, storer);
  }
  td::store(type, storer);
}

template <class ParserT>
void BackgroundManager::Background::parse(ParserT &parser) {
  bool has_file_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_creator);
  PARSE_FLAG(is_default);
  PARSE_FLAG(is_dark);
  PARSE_FLAG(has_file_id);
  END_PARSE_FLAGS();
  td::parse(id, parser);
  td::parse(access_hash, parser);
  td::parse(name, parser);
  if (has_file_id) {
    file_id = parser.context()->td().get_actor_unsafe()->documents_manager_->parse_document(parser);
  } else {
    file_id = FileId();
  }
  td::parse(type, parser);
}

class BackgroundManager::BackgroundLogEvent {
 public:
  Background background_;
  BackgroundType set_type_;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(background_, storer);
    td::store(set_type_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(background_, parser);
    td::parse(set_type_, parser);
  }
};

void BackgroundManager::start_up() {
  for (int i = 0; i < 2; i++) {
    bool for_dark_theme = i != 0;
    auto log_event_string = G()->td_db()->get_binlog_pmc()->get(get_background_database_key(for_dark_theme));
    if (!log_event_string.empty()) {
      BackgroundLogEvent log_event;
      log_event_parse(log_event, log_event_string).ensure();

      CHECK(log_event.background_.id.is_valid());
      bool needs_file_id = log_event.background_.type.type != BackgroundType::Type::Fill;
      if (log_event.background_.file_id.is_valid() != needs_file_id) {
        LOG(ERROR) << "Failed to load " << log_event.background_.id << " of " << log_event.background_.type;
        G()->td_db()->get_binlog_pmc()->erase(get_background_database_key(for_dark_theme));
        continue;
      }
      set_background_id_[for_dark_theme] = log_event.background_.id;
      set_background_type_[for_dark_theme] = log_event.set_type_;

      add_background(log_event.background_);
    }

    send_update_selected_background(for_dark_theme);
  }
}

void BackgroundManager::tear_down() {
  parent_.reset();
}

void BackgroundManager::get_backgrounds(Promise<Unit> &&promise) {
  pending_get_backgrounds_queries_.push_back(std::move(promise));
  if (pending_get_backgrounds_queries_.size() == 1) {
    auto request_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result) {
          send_closure(actor_id, &BackgroundManager::on_get_backgrounds, std::move(result));
        });

    td_->create_handler<GetBackgroundsQuery>(std::move(request_promise))->send();
  }
}

Result<string> BackgroundManager::get_background_url(const string &name,
                                                     td_api::object_ptr<td_api::BackgroundType> background_type) const {
  TRY_RESULT(type, get_background_type(background_type.get()));
  auto url = PSTRING() << G()->shared_config().get_option_string("t_me_url", "https://t.me/") << "bg/";
  auto link = type.get_link();
  if (type.is_server()) {
    url += name;
    if (!link.empty()) {
      url += '?';
      url += link;
    }
  } else {
    url += link;
  }
  return url;
}

void BackgroundManager::reload_background_from_server(
    BackgroundId background_id, const string &background_name,
    telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper, Promise<Unit> &&promise) const {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  td_->create_handler<GetBackgroundQuery>(std::move(promise))
      ->send(background_id, background_name, std::move(input_wallpaper));
}

void BackgroundManager::reload_background(BackgroundId background_id, int64 access_hash, Promise<Unit> &&promise) {
  reload_background_from_server(
      background_id, string(),
      telegram_api::make_object<telegram_api::inputWallPaper>(background_id.get(), access_hash), std::move(promise));
}

static bool is_background_name_local(Slice name) {
  return name.size() <= 6 || name.find('?') <= 13u;
}

BackgroundId BackgroundManager::search_background(const string &name, Promise<Unit> &&promise) {
  auto it = name_to_background_id_.find(name);
  if (it != name_to_background_id_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  if (name.empty()) {
    promise.set_error(Status::Error(400, "Background name must be non-empty"));
    return BackgroundId();
  }

  if (is_background_name_local(name)) {
    Slice fill_colors = name;
    Slice parameters;
    auto parameters_pos = fill_colors.find('?');
    if (parameters_pos != Slice::npos) {
      parameters = fill_colors.substr(parameters_pos + 1);
      fill_colors = fill_colors.substr(0, parameters_pos);
    }
    CHECK(fill_colors.size() <= 13u);

    bool have_hyphen = false;
    size_t hyphen_pos = 0;
    for (size_t i = 0; i < fill_colors.size(); i++) {
      auto c = fill_colors[i];
      if (!is_hex_digit(c)) {
        if (c != '-' || have_hyphen || i > 6 || i + 7 < fill_colors.size()) {
          promise.set_error(Status::Error(400, "WALLPAPER_INVALID"));
          return BackgroundId();
        }
        have_hyphen = true;
        hyphen_pos = i;
      }
    }

    BackgroundFill fill;
    if (have_hyphen) {
      int32 top_color = static_cast<int32>(hex_to_integer<uint32>(fill_colors.substr(0, hyphen_pos)));
      int32 bottom_color = static_cast<int32>(hex_to_integer<uint32>(fill_colors.substr(hyphen_pos + 1)));
      int32 rotation_angle = 0;

      Slice prefix("rotation=");
      if (begins_with(parameters, prefix)) {
        rotation_angle = to_integer<int32>(parameters.substr(prefix.size()));
        if (!BackgroundFill::is_valid_rotation_angle(rotation_angle)) {
          rotation_angle = 0;
        }
      }

      fill = BackgroundFill(top_color, bottom_color, rotation_angle);
    } else {
      int32 color = static_cast<int32>(hex_to_integer<uint32>(fill_colors));
      fill = BackgroundFill(color);
    }
    auto background_id = add_fill_background(fill);
    promise.set_value(Unit());
    return background_id;
  }

  if (G()->parameters().use_file_db && loaded_from_database_backgrounds_.count(name) == 0) {
    auto &queries = being_loaded_from_database_backgrounds_[name];
    queries.push_back(std::move(promise));
    if (queries.size() == 1) {
      LOG(INFO) << "Trying to load background " << name << " from database";
      G()->td_db()->get_sqlite_pmc()->get(
          get_background_name_database_key(name), PromiseCreator::lambda([name](string value) {
            send_closure(G()->background_manager(), &BackgroundManager::on_load_background_from_database,
                         std::move(name), std::move(value));
          }));
    }
    return BackgroundId();
  }

  reload_background_from_server(BackgroundId(), name, telegram_api::make_object<telegram_api::inputWallPaperSlug>(name),
                                std::move(promise));
  return BackgroundId();
}

void BackgroundManager::on_load_background_from_database(string name, string value) {
  if (G()->close_flag()) {
    return;
  }

  auto promises_it = being_loaded_from_database_backgrounds_.find(name);
  CHECK(promises_it != being_loaded_from_database_backgrounds_.end());
  auto promises = std::move(promises_it->second);
  CHECK(!promises.empty());
  being_loaded_from_database_backgrounds_.erase(promises_it);

  loaded_from_database_backgrounds_.insert(name);

  CHECK(!is_background_name_local(name));
  if (name_to_background_id_.count(name) == 0 && !value.empty()) {
    LOG(INFO) << "Successfully loaded background " << name << " of size " << value.size() << " from database";
    Background background;
    auto status = log_event_parse(background, value);
    if (status.is_error() || background.type.type == BackgroundType::Type::Fill || !background.file_id.is_valid() ||
        !background.id.is_valid()) {
      LOG(ERROR) << "Can't load background " << name << ": " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    } else {
      if (background.name != name) {
        LOG(ERROR) << "Expected background " << name << ", but received " << background.name;
        name_to_background_id_.emplace(name, background.id);
      }
      add_background(background);
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

td_api::object_ptr<td_api::updateSelectedBackground> BackgroundManager::get_update_selected_background_object(
    bool for_dark_theme) const {
  return td_api::make_object<td_api::updateSelectedBackground>(
      for_dark_theme, get_background_object(set_background_id_[for_dark_theme], for_dark_theme));
}

void BackgroundManager::send_update_selected_background(bool for_dark_theme) const {
  send_closure(G()->td(), &Td::send_update, get_update_selected_background_object(for_dark_theme));
}

Result<FileId> BackgroundManager::prepare_input_file(const tl_object_ptr<td_api::InputFile> &input_file) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Background, input_file, {}, false, false);
  if (r_file_id.is_error()) {
    return Status::Error(400, r_file_id.error().message());
  }
  auto file_id = r_file_id.move_as_ok();

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return Status::Error(400, "Can't use encrypted file");
  }
  if (!file_view.has_local_location() && !file_view.has_generate_location()) {
    return Status::Error(400, "Need local or generate location to upload background");
  }
  return std::move(file_id);
}

BackgroundId BackgroundManager::add_fill_background(const BackgroundFill &fill) {
  return add_fill_background(fill, false, (fill.top_color & 0x808080) == 0 && (fill.bottom_color & 0x808080) == 0);
}

BackgroundId BackgroundManager::add_fill_background(const BackgroundFill &fill, bool is_default, bool is_dark) {
  BackgroundId background_id(fill.get_id());

  Background background;
  background.id = background_id;
  background.is_creator = true;
  background.is_default = is_default;
  background.is_dark = is_dark;
  background.type = BackgroundType(fill);
  background.name = background.type.get_link();
  add_background(background);

  return background_id;
}

BackgroundId BackgroundManager::set_background(const td_api::InputBackground *input_background,
                                               const td_api::BackgroundType *background_type, bool for_dark_theme,
                                               Promise<Unit> &&promise) {
  if (background_type == nullptr) {
    set_background_id(BackgroundId(), BackgroundType(), for_dark_theme);
    promise.set_value(Unit());
    return BackgroundId();
  }

  auto r_type = get_background_type(background_type);
  if (r_type.is_error()) {
    promise.set_error(r_type.move_as_error());
    return BackgroundId();
  }

  auto type = r_type.move_as_ok();
  if (type.type == BackgroundType::Type::Fill) {
    auto background_id = add_fill_background(type.fill);
    set_background_id(background_id, type, for_dark_theme);
    promise.set_value(Unit());
    return background_id;
  }
  CHECK(type.is_server());

  if (input_background == nullptr) {
    promise.set_error(Status::Error(400, "Input background must be non-empty"));
    return BackgroundId();
  }

  switch (input_background->get_id()) {
    case td_api::inputBackgroundLocal::ID: {
      auto background_local = static_cast<const td_api::inputBackgroundLocal *>(input_background);
      auto r_file_id = prepare_input_file(background_local->background_);
      if (r_file_id.is_error()) {
        promise.set_error(r_file_id.move_as_error());
        return BackgroundId();
      }
      auto file_id = r_file_id.move_as_ok();
      LOG(INFO) << "Receive file " << file_id << " for input background";

      auto it = file_id_to_background_id_.find(file_id);
      if (it != file_id_to_background_id_.end()) {
        return set_background(it->second, type, for_dark_theme, std::move(promise));
      }

      upload_background_file(file_id, type, for_dark_theme, std::move(promise));
      break;
    }
    case td_api::inputBackgroundRemote::ID: {
      auto background_remote = static_cast<const td_api::inputBackgroundRemote *>(input_background);
      return set_background(BackgroundId(background_remote->background_id_), type, for_dark_theme, std::move(promise));
    }
    default:
      UNREACHABLE();
  }
  return BackgroundId();
}

BackgroundId BackgroundManager::set_background(BackgroundId background_id, const BackgroundType &type,
                                               bool for_dark_theme, Promise<Unit> &&promise) {
  LOG(INFO) << "Set " << background_id << " with " << type;
  auto *background = get_background(background_id);
  if (background == nullptr) {
    promise.set_error(Status::Error(400, "Background to set not found"));
    return BackgroundId();
  }
  if (background->type.type != type.type) {
    promise.set_error(Status::Error(400, "Background type mismatch"));
    return BackgroundId();
  }
  if (set_background_id_[for_dark_theme] == background_id && set_background_type_[for_dark_theme] == type) {
    promise.set_value(Unit());
    return background_id;
  }

  LOG(INFO) << "Install " << background_id << " with " << type;
  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), background_id, type, for_dark_theme,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    send_closure(actor_id, &BackgroundManager::on_installed_background, background_id, type, for_dark_theme,
                 std::move(result), std::move(promise));
  });
  td_->create_handler<InstallBackgroundQuery>(std::move(query_promise))
      ->send(background_id, background->access_hash, type);
  return BackgroundId();
}

void BackgroundManager::on_installed_background(BackgroundId background_id, BackgroundType type, bool for_dark_theme,
                                                Result<Unit> &&result, Promise<Unit> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }

  if (!td::contains(installed_background_ids_, background_id)) {
    installed_background_ids_.insert(installed_background_ids_.begin(), background_id);
  }
  set_background_id(background_id, type, for_dark_theme);
  promise.set_value(Unit());
}

string BackgroundManager::get_background_database_key(bool for_dark_theme) {
  return for_dark_theme ? "bgd" : "bg";
}

void BackgroundManager::save_background_id(bool for_dark_theme) const {
  string key = get_background_database_key(for_dark_theme);
  auto background_id = set_background_id_[for_dark_theme];
  if (background_id.is_valid()) {
    const Background *background = get_background(background_id);
    CHECK(background != nullptr);
    BackgroundLogEvent log_event{*background, set_background_type_[for_dark_theme]};
    G()->td_db()->get_binlog_pmc()->set(key, log_event_store(log_event).as_slice().str());
  } else {
    G()->td_db()->get_binlog_pmc()->erase(key);
  }
}

void BackgroundManager::set_background_id(BackgroundId background_id, const BackgroundType &type, bool for_dark_theme) {
  if (background_id == set_background_id_[for_dark_theme] && set_background_type_[for_dark_theme] == type) {
    return;
  }

  set_background_id_[for_dark_theme] = background_id;
  set_background_type_[for_dark_theme] = type;

  save_background_id(for_dark_theme);
  send_update_selected_background(for_dark_theme);
}

void BackgroundManager::upload_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme,
                                               Promise<Unit> &&promise) {
  auto upload_file_id = td_->file_manager_->dup_file_id(file_id);

  being_uploaded_files_[upload_file_id] = {type, for_dark_theme, std::move(promise)};
  LOG(INFO) << "Ask to upload background file " << upload_file_id;
  td_->file_manager_->upload(upload_file_id, upload_background_file_callback_, 1, 0);
}

void BackgroundManager::on_upload_background_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  LOG(INFO) << "Background file " << file_id << " has been uploaded";

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());

  auto type = it->second.type;
  auto for_dark_theme = it->second.for_dark_theme;
  auto promise = std::move(it->second.promise);

  being_uploaded_files_.erase(it);

  do_upload_background_file(file_id, type, for_dark_theme, std::move(input_file), std::move(promise));
}

void BackgroundManager::on_upload_background_file_error(FileId file_id, Status status) {
  if (G()->close_flag()) {
    // do not fail upload if closing
    return;
  }

  LOG(WARNING) << "Background file " << file_id << " has upload error " << status;
  CHECK(status.is_error());

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());

  auto promise = std::move(it->second.promise);

  being_uploaded_files_.erase(it);

  promise.set_error(Status::Error(status.code() > 0 ? status.code() : 500,
                                  status.message()));  // TODO CHECK that status has always a code
}

void BackgroundManager::do_upload_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme,
                                                  tl_object_ptr<telegram_api::InputFile> &&input_file,
                                                  Promise<Unit> &&promise) {
  if (input_file == nullptr) {
    FileView file_view = td_->file_manager_->get_file_view(file_id);
    file_id = file_view.file_id();
    auto it = file_id_to_background_id_.find(file_id);
    if (it != file_id_to_background_id_.end()) {
      set_background(it->second, type, for_dark_theme, std::move(promise));
      return;
    }
    return promise.set_error(Status::Error(500, "Failed to reupload background"));
  }

  td_->create_handler<UploadBackgroundQuery>(std::move(promise))
      ->send(file_id, std::move(input_file), type, for_dark_theme);
}

void BackgroundManager::on_uploaded_background_file(FileId file_id, const BackgroundType &type, bool for_dark_theme,
                                                    telegram_api::object_ptr<telegram_api::WallPaper> wallpaper,
                                                    Promise<Unit> &&promise) {
  CHECK(wallpaper != nullptr);

  BackgroundId background_id = on_get_background(BackgroundId(), string(), std::move(wallpaper));
  if (!background_id.is_valid()) {
    td_->file_manager_->cancel_upload(file_id);
    return promise.set_error(Status::Error(500, "Receive wrong uploaded background"));
  }

  auto background = get_background(background_id);
  CHECK(background != nullptr);
  if (!background->file_id.is_valid()) {
    td_->file_manager_->cancel_upload(file_id);
    return promise.set_error(Status::Error(500, "Receive wrong uploaded background without file"));
  }
  LOG_STATUS(td_->file_manager_->merge(background->file_id, file_id));
  set_background_id(background_id, type, for_dark_theme);
  promise.set_value(Unit());
}

void BackgroundManager::remove_background(BackgroundId background_id, Promise<Unit> &&promise) {
  auto background = get_background(background_id);
  if (background == nullptr) {
    return promise.set_error(Status::Error(400, "Background not found"));
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), background_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
        send_closure(actor_id, &BackgroundManager::on_removed_background, background_id, std::move(result),
                     std::move(promise));
      });

  if (!background->type.is_server()) {
    return query_promise.set_value(Unit());
  }

  td_->create_handler<SaveBackgroundQuery>(std::move(query_promise))
      ->send(background_id, background->access_hash, background->type, true);
}

void BackgroundManager::on_removed_background(BackgroundId background_id, Result<Unit> &&result,
                                              Promise<Unit> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }
  td::remove(installed_background_ids_, background_id);
  if (background_id == set_background_id_[0]) {
    set_background_id(BackgroundId(), BackgroundType(), false);
  }
  if (background_id == set_background_id_[1]) {
    set_background_id(BackgroundId(), BackgroundType(), true);
  }
  promise.set_value(Unit());
}

void BackgroundManager::reset_backgrounds(Promise<Unit> &&promise) {
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](Result<Unit> &&result) mutable {
        send_closure(actor_id, &BackgroundManager::on_reset_background, std::move(result), std::move(promise));
      });

  td_->create_handler<ResetBackgroundsQuery>(std::move(query_promise))->send();
}

void BackgroundManager::on_reset_background(Result<Unit> &&result, Promise<Unit> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }
  installed_background_ids_.clear();
  set_background_id(BackgroundId(), BackgroundType(), false);
  set_background_id(BackgroundId(), BackgroundType(), true);
  promise.set_value(Unit());
}

void BackgroundManager::add_background(const Background &background) {
  LOG(INFO) << "Add " << background.id << " of " << background.type;

  CHECK(background.id.is_valid());
  auto *result = &backgrounds_[background.id];

  FileSourceId file_source_id;
  auto it = background_id_to_file_source_id_.find(background.id);
  if (it != background_id_to_file_source_id_.end()) {
    CHECK(!result->id.is_valid());
    file_source_id = it->second.second;
    background_id_to_file_source_id_.erase(it);
  }

  if (!result->id.is_valid()) {
    result->id = background.id;
  } else {
    CHECK(result->id == background.id);
  }
  result->access_hash = background.access_hash;
  result->is_creator = background.is_creator;
  result->is_default = background.is_default;
  result->is_dark = background.is_dark;
  result->type = background.type;

  if (result->name != background.name) {
    if (!result->name.empty()) {
      LOG(ERROR) << "Background name has changed from " << result->name << " to " << background.name;
      // keep correspondence from previous name to background ID
      // it will not harm, because background names can't be reassigned
      // name_to_background_id_.erase(result->name);
    }

    result->name = background.name;

    if (!is_background_name_local(result->name)) {
      name_to_background_id_.emplace(result->name, result->id);
      loaded_from_database_backgrounds_.erase(result->name);  // don't needed anymore
    }
  }

  if (result->file_id != background.file_id) {
    if (result->file_id.is_valid()) {
      if (td_->file_manager_->get_file_view(result->file_id).file_id() !=
          td_->file_manager_->get_file_view(background.file_id).file_id()) {
        LOG(ERROR) << "Background file has changed from " << result->file_id << " to " << background.file_id;
        file_id_to_background_id_.erase(result->file_id);
        result->file_source_id = FileSourceId();
      }
      CHECK(!file_source_id.is_valid());
    }
    if (file_source_id.is_valid()) {
      result->file_source_id = file_source_id;
    }

    result->file_id = background.file_id;

    if (result->file_id.is_valid()) {
      if (!result->file_source_id.is_valid()) {
        result->file_source_id =
            td_->file_reference_manager_->create_background_file_source(result->id, result->access_hash);
      }
      for (auto file_id : Document(Document::Type::General, result->file_id).get_file_ids(td_)) {
        td_->file_manager_->add_file_source(file_id, result->file_source_id);
      }
    }

    file_id_to_background_id_.emplace(result->file_id, result->id);
  } else {
    // if file_source_id is valid, then this is a new background with result->file_id == FileId()
    // then background.file_id == FileId(), then this is a fill background, which can't have file_source_id
    CHECK(!file_source_id.is_valid());
  }
}

BackgroundManager::Background *BackgroundManager::get_background_ref(BackgroundId background_id) {
  auto p = backgrounds_.find(background_id);
  if (p == backgrounds_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

const BackgroundManager::Background *BackgroundManager::get_background(BackgroundId background_id) const {
  auto p = backgrounds_.find(background_id);
  if (p == backgrounds_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

string BackgroundManager::get_background_name_database_key(const string &name) {
  return PSTRING() << "bgn" << name;
}

BackgroundId BackgroundManager::on_get_background(BackgroundId expected_background_id,
                                                  const string &expected_background_name,
                                                  telegram_api::object_ptr<telegram_api::WallPaper> wallpaper_ptr) {
  CHECK(wallpaper_ptr != nullptr);

  if (wallpaper_ptr->get_id() == telegram_api::wallPaperNoFile::ID) {
    auto wallpaper = move_tl_object_as<telegram_api::wallPaperNoFile>(wallpaper_ptr);

    auto settings = std::move(wallpaper->settings_);
    if (settings == nullptr) {
      LOG(ERROR) << "Receive wallPaperNoFile without settings: " << to_string(wallpaper);
      return BackgroundId();
    }

    bool has_color = (settings->flags_ & telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK) != 0;
    auto color = has_color ? settings->background_color_ : 0;
    auto is_default = (wallpaper->flags_ & telegram_api::wallPaperNoFile::DEFAULT_MASK) != 0;
    auto is_dark = (wallpaper->flags_ & telegram_api::wallPaperNoFile::DARK_MASK) != 0;

    BackgroundFill fill = BackgroundFill(color);
    if ((settings->flags_ & telegram_api::wallPaperSettings::SECOND_BACKGROUND_COLOR_MASK) != 0) {
      fill = BackgroundFill(color, settings->second_background_color_, settings->rotation_);
    }
    return add_fill_background(fill, is_default, is_dark);
  }

  auto wallpaper = move_tl_object_as<telegram_api::wallPaper>(wallpaper_ptr);
  auto id = BackgroundId(wallpaper->id_);
  if (!id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(wallpaper);
    return BackgroundId();
  }
  if (expected_background_id.is_valid() && id != expected_background_id) {
    LOG(ERROR) << "Expected " << expected_background_id << ", but receive " << to_string(wallpaper);
  }
  if (is_background_name_local(wallpaper->slug_) || BackgroundFill::is_valid_id(wallpaper->id_)) {
    LOG(ERROR) << "Receive " << to_string(wallpaper);
    return BackgroundId();
  }

  int32 document_id = wallpaper->document_->get_id();
  if (document_id == telegram_api::documentEmpty::ID) {
    LOG(ERROR) << "Receive " << to_string(wallpaper);
    return BackgroundId();
  }
  CHECK(document_id == telegram_api::document::ID);

  int32 flags = wallpaper->flags_;
  bool is_pattern = (flags & telegram_api::wallPaper::PATTERN_MASK) != 0;

  Document document = td_->documents_manager_->on_get_document(
      telegram_api::move_object_as<telegram_api::document>(wallpaper->document_), DialogId(), nullptr,
      Document::Type::General, true, is_pattern);
  if (!document.file_id.is_valid()) {
    LOG(ERROR) << "Receive wrong document in " << to_string(wallpaper);
    return BackgroundId();
  }
  CHECK(document.type == Document::Type::General);  // guaranteed by is_background parameter to on_get_document

  Background background;
  background.id = id;
  background.access_hash = wallpaper->access_hash_;
  background.is_creator = (flags & telegram_api::wallPaper::CREATOR_MASK) != 0;
  background.is_default = (flags & telegram_api::wallPaper::DEFAULT_MASK) != 0;
  background.is_dark = (flags & telegram_api::wallPaper::DARK_MASK) != 0;
  background.type = get_background_type(is_pattern, std::move(wallpaper->settings_));
  background.name = std::move(wallpaper->slug_);
  background.file_id = document.file_id;
  add_background(background);

  if (!expected_background_name.empty() && background.name != expected_background_name) {
    LOG(ERROR) << "Expected background " << expected_background_name << ", but receive " << background.name;
    name_to_background_id_.emplace(expected_background_name, id);
  }

  if (G()->parameters().use_file_db && !is_background_name_local(background.name)) {
    LOG(INFO) << "Save " << id << " to database with name " << background.name;
    G()->td_db()->get_sqlite_pmc()->set(get_background_name_database_key(background.name),
                                        log_event_store(background).as_slice().str(), Auto());
  }

  return id;
}

void BackgroundManager::on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result) {
  auto promises = std::move(pending_get_backgrounds_queries_);
  CHECK(!promises.empty());
  reset_to_empty(pending_get_backgrounds_queries_);

  if (result.is_error()) {
    // do not clear installed_background_ids_

    auto error = result.move_as_error();
    for (auto &promise : promises) {
      promise.set_error(error.clone());
    }
    return;
  }

  auto wallpapers_ptr = result.move_as_ok();
  LOG(INFO) << "Receive " << to_string(wallpapers_ptr);
  if (wallpapers_ptr->get_id() == telegram_api::account_wallPapersNotModified::ID) {
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
    return;
  }

  installed_background_ids_.clear();
  auto wallpapers = telegram_api::move_object_as<telegram_api::account_wallPapers>(wallpapers_ptr);
  for (auto &wallpaper : wallpapers->wallpapers_) {
    auto background_id = on_get_background(BackgroundId(), string(), std::move(wallpaper));
    if (background_id.is_valid()) {
      installed_background_ids_.push_back(background_id);
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

td_api::object_ptr<td_api::background> BackgroundManager::get_background_object(BackgroundId background_id,
                                                                                bool for_dark_theme) const {
  auto background = get_background(background_id);
  if (background == nullptr) {
    return nullptr;
  }
  auto type = &background->type;
  // first check another set_background_id to get correct type if both backgrounds are the same
  if (background_id == set_background_id_[1 - static_cast<int>(for_dark_theme)]) {
    type = &set_background_type_[1 - static_cast<int>(for_dark_theme)];
  }
  if (background_id == set_background_id_[for_dark_theme]) {
    type = &set_background_type_[for_dark_theme];
  }
  return td_api::make_object<td_api::background>(
      background->id.get(), background->is_default, background->is_dark, background->name,
      td_->documents_manager_->get_document_object(background->file_id, PhotoFormat::Png),
      get_background_type_object(*type));
}

td_api::object_ptr<td_api::backgrounds> BackgroundManager::get_backgrounds_object(bool for_dark_theme) const {
  auto backgrounds = transform(installed_background_ids_, [this, for_dark_theme](BackgroundId background_id) {
    return get_background_object(background_id, for_dark_theme);
  });
  auto background_id = set_background_id_[for_dark_theme];
  if (background_id.is_valid() && !td::contains(installed_background_ids_, background_id)) {
    backgrounds.push_back(get_background_object(background_id, for_dark_theme));
  }
  std::stable_sort(backgrounds.begin(), backgrounds.end(),
                   [background_id, for_dark_theme](const td_api::object_ptr<td_api::background> &lhs,
                                                   const td_api::object_ptr<td_api::background> &rhs) {
                     auto get_order = [background_id,
                                       for_dark_theme](const td_api::object_ptr<td_api::background> &background) {
                       if (background->id_ == background_id.get()) {
                         return 0;
                       }
                       if (background->is_dark_ == for_dark_theme) {
                         return 1;
                       }
                       return 2;
                     };
                     return get_order(lhs) < get_order(rhs);
                   });
  return td_api::make_object<td_api::backgrounds>(std::move(backgrounds));
}

FileSourceId BackgroundManager::get_background_file_source_id(BackgroundId background_id, int64 access_hash) {
  Background *background = get_background_ref(background_id);
  if (background != nullptr) {
    if (!background->file_source_id.is_valid()) {
      background->file_source_id =
          td_->file_reference_manager_->create_background_file_source(background_id, background->access_hash);
    }
    return background->file_source_id;
  }

  auto &result = background_id_to_file_source_id_[background_id];
  if (result.first == 0) {
    result.first = access_hash;
  }
  if (!result.second.is_valid()) {
    result.second = td_->file_reference_manager_->create_background_file_source(background_id, result.first);
  }
  return result.second;
}

void BackgroundManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(get_update_selected_background_object(false));
  updates.push_back(get_update_selected_background_object(true));
}

}  // namespace td
