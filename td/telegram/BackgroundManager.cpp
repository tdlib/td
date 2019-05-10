//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

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

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/misc.h"

#include <algorithm>

namespace td {

class GetBackgroundQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  BackgroundId background_id_;

 public:
  explicit GetBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BackgroundId background_id, telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper) {
    background_id_ = background_id;
    LOG(INFO) << "Load " << background_id << " from server: " << to_string(input_wallpaper);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::account_getWallPaper(std::move(input_wallpaper)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->background_manager_->on_get_background(background_id_, result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(INFO) << "Receive error for getBackground: " << status;
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
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getWallPapers(0))));
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
  BackgroundId background_id_;
  BackgroundType type_;

 public:
  explicit InstallBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BackgroundId background_id, int64 access_hash, const BackgroundType &type) {
    background_id_ = background_id;
    type_ = type;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_installWallPaper(
        telegram_api::make_object<telegram_api::inputWallPaper>(background_id.get(), access_hash),
        get_input_wallpaper_settings(type)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_installWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->background_manager_->set_background_id(background_id_, type_);
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

 public:
  explicit UploadBackgroundQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, tl_object_ptr<telegram_api::InputFile> &&input_file, const BackgroundType &type) {
    CHECK(input_file != nullptr);
    file_id_ = file_id;
    type_ = type;
    string mime_type = type.type == BackgroundType::Type::Pattern ? "image/png" : "image/jpeg";
    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::account_uploadWallPaper(std::move(input_file), mime_type, get_input_wallpaper_settings(type)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_uploadWallPaper>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->background_manager_->on_uploaded_background_file(file_id_, type_, result_ptr.move_as_ok(), std::move(promise_));
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

class BackgroundManager::BackgroundLogEvent {
 public:
  BackgroundId background_id_;
  int64 access_hash_;
  string name_;
  FileId file_id_;
  bool is_creator_;
  bool is_default_;
  bool is_dark_;
  BackgroundType type_;
  BackgroundType set_type_;

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_file_id = file_id_.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_creator_);
    STORE_FLAG(is_default_);
    STORE_FLAG(is_dark_);
    STORE_FLAG(has_file_id);
    END_STORE_FLAGS();
    td::store(background_id_, storer);
    td::store(access_hash_, storer);
    td::store(name_, storer);
    if (has_file_id) {
      storer.context()->td().get_actor_unsafe()->documents_manager_->store_document(file_id_, storer);
    }
    td::store(type_, storer);
    td::store(set_type_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_file_id;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_creator_);
    PARSE_FLAG(is_default_);
    PARSE_FLAG(is_dark_);
    PARSE_FLAG(has_file_id);
    END_PARSE_FLAGS();
    td::parse(background_id_, parser);
    td::parse(access_hash_, parser);
    td::parse(name_, parser);
    if (has_file_id) {
      file_id_ = parser.context()->td().get_actor_unsafe()->documents_manager_->parse_document(parser);
    } else {
      file_id_ = FileId();
    }
    td::parse(type_, parser);
    td::parse(set_type_, parser);
  }
};

void BackgroundManager::start_up() {
  // G()->td_db()->get_binlog_pmc()->erase(get_background_database_key());
  auto logevent_string = G()->td_db()->get_binlog_pmc()->get(get_background_database_key());
  if (!logevent_string.empty()) {
    BackgroundLogEvent logevent;
    log_event_parse(logevent, logevent_string).ensure();

    CHECK(logevent.background_id_.is_valid());
    set_background_id_ = logevent.background_id_;
    set_background_type_ = logevent.set_type_;

    auto *background = add_background(set_background_id_);
    CHECK(!background->id.is_valid());
    background->id = set_background_id_;
    background->access_hash = logevent.access_hash_;
    background->is_creator = logevent.is_creator_;
    background->is_default = logevent.is_default_;
    background->is_dark = logevent.is_dark_;
    background->type = logevent.type_;
    background->name = std::move(logevent.name_);
    background->file_id = logevent.file_id_;

    name_to_background_id_.emplace(background->name, background->id);
    if (background->file_id.is_valid()) {
      background->file_source_id =
          td_->file_reference_manager_->create_background_file_source(background->id, background->access_hash);
      for (auto file_id : Document(Document::Type::General, background->file_id).get_file_ids(td_)) {
        td_->file_manager_->add_file_source(file_id, background->file_source_id);
      }
      file_id_to_background_id_.emplace(background->file_id, background->id);
    }
  }

  send_update_selected_background();
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

  vector<string> modes;
  if (type.is_blurred) {
    modes.emplace_back("blur");
  }
  if (type.is_moving) {
    modes.emplace_back("motion");
  }
  string mode = implode(modes, '+');

  string url = PSTRING() << G()->shared_config().get_option_string("t_me_url", "https://t.me/") << "bg/";
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
      url += name;
      if (!mode.empty()) {
        url += "?mode=";
        url += mode;
      }
      return url;
    case BackgroundType::Type::Pattern:
      url += name;
      url += "?intensity=";
      url += to_string(type.intensity);
      url += "&bg_color=";
      url += type.get_color_hex_string();
      if (!mode.empty()) {
        url += "&mode=";
        url += mode;
      }
      return url;
    case BackgroundType::Type::Solid:
      url += type.get_color_hex_string();
      return url;
    default:
      UNREACHABLE();
      return url;
  }
}

void BackgroundManager::reload_background_from_server(
    BackgroundId background_id, telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper,
    Promise<Unit> &&promise) const {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  td_->create_handler<GetBackgroundQuery>(std::move(promise))->send(background_id, std::move(input_wallpaper));
}

void BackgroundManager::reload_background(BackgroundId background_id, int64 access_hash, Promise<Unit> &&promise) {
  reload_background_from_server(
      background_id, telegram_api::make_object<telegram_api::inputWallPaper>(background_id.get(), access_hash),
      std::move(promise));
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

  if (name.size() <= 6) {
    for (auto c : name) {
      if (!is_hex_digit(c)) {
        promise.set_error(Status::Error(400, "WALLPAPER_INVALID"));
        return BackgroundId();
      }
    }
    int32 color = static_cast<int32>(hex_to_integer<uint32>(name));
    auto background_id = add_solid_background(color);
    promise.set_value(Unit());
    return background_id;
  }

  reload_background_from_server(BackgroundId(), telegram_api::make_object<telegram_api::inputWallPaperSlug>(name),
                                std::move(promise));
  return BackgroundId();
}

td_api::object_ptr<td_api::updateSelectedBackground> BackgroundManager::get_update_selected_background() const {
  return td_api::make_object<td_api::updateSelectedBackground>(get_background_object(set_background_id_));
}

void BackgroundManager::send_update_selected_background() const {
  send_closure(G()->td(), &Td::send_update, get_update_selected_background());
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

BackgroundId BackgroundManager::add_solid_background(int32 color) {
  CHECK(0 <= color && color < 0x1000000);
  BackgroundId background_id(static_cast<int64>(color) + 1);
  auto *background = add_background(background_id);
  if (background->id != background_id) {
    background->id = background_id;
    background->access_hash = 0;
    background->is_creator = true;
    background->is_default = false;
    background->is_dark = (color & 0x808080) == 0;
    background->type = BackgroundType(color);
    background->name = background->type.get_color_hex_string();
    background->file_id = FileId();
    background->file_source_id = FileSourceId();
  }
  return background_id;
}

BackgroundId BackgroundManager::set_background(const td_api::InputBackground *input_background,
                                               const td_api::BackgroundType *background_type, Promise<Unit> &&promise) {
  auto r_type = get_background_type(background_type);
  if (r_type.is_error()) {
    promise.set_error(r_type.move_as_error());
    return BackgroundId();
  }

  auto type = r_type.move_as_ok();
  if (type.type == BackgroundType::Type::Solid) {
    auto background_id = add_solid_background(type.color);
    if (set_background_id_ != background_id) {
      set_background_id(background_id, type);
    }
    promise.set_value(Unit());
    return background_id;
  }

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
        return set_background(it->second, type, std::move(promise));
      }

      upload_background_file(file_id, type, std::move(promise));
      break;
    }
    case td_api::inputBackgroundRemote::ID: {
      auto background_remote = static_cast<const td_api::inputBackgroundRemote *>(input_background);
      return set_background(BackgroundId(background_remote->background_id_), type, std::move(promise));
    }
    default:
      UNREACHABLE();
  }
  return BackgroundId();
}

BackgroundId BackgroundManager::set_background(BackgroundId background_id, const BackgroundType &type,
                                               Promise<Unit> &&promise) {
  auto *background = get_background(background_id);
  if (background == nullptr) {
    promise.set_error(Status::Error(400, "Background to set not found"));
    return BackgroundId();
  }
  if (background->type.type != type.type) {
    promise.set_error(Status::Error(400, "Background type mismatch"));
    return BackgroundId();
  }
  if (set_background_id_ == background_id) {
    promise.set_value(Unit());
    return background_id;
  }

  LOG(INFO) << "Install " << background_id << " with " << type;
  td_->create_handler<InstallBackgroundQuery>(std::move(promise))->send(background_id, background->access_hash, type);
  return BackgroundId();
}

string BackgroundManager::get_background_database_key() {
  return "bg";
}

void BackgroundManager::save_background_id() const {
  string key = get_background_database_key();
  if (set_background_id_.is_valid()) {
    const Background *background = get_background(set_background_id_);
    CHECK(background != nullptr);
    BackgroundLogEvent logevent{set_background_id_,  background->access_hash, background->name,
                                background->file_id, background->is_creator,  background->is_default,
                                background->is_dark, background->type,        set_background_type_};
    G()->td_db()->get_binlog_pmc()->set(key, log_event_store(logevent).as_slice().str());
  } else {
    G()->td_db()->get_binlog_pmc()->erase(key);
  }
}

void BackgroundManager::set_background_id(BackgroundId background_id, const BackgroundType &type) {
  if (background_id == set_background_id_ && set_background_type_ == type) {
    return;
  }

  set_background_id_ = background_id;
  set_background_type_ = type;

  save_background_id();
  send_update_selected_background();
}

void BackgroundManager::upload_background_file(FileId file_id, const BackgroundType &type, Promise<Unit> &&promise) {
  auto upload_file_id = td_->file_manager_->dup_file_id(file_id);

  being_uploaded_files_[upload_file_id] = {type, std::move(promise)};
  LOG(INFO) << "Ask to upload background file " << upload_file_id;
  td_->file_manager_->upload(upload_file_id, upload_background_file_callback_, 1, 0);
}

void BackgroundManager::on_upload_background_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  LOG(INFO) << "Background file " << file_id << " has been uploaded";

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());

  auto type = it->second.type;
  auto promise = std::move(it->second.promise);

  being_uploaded_files_.erase(it);

  do_upload_background_file(file_id, type, std::move(input_file), std::move(promise));
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

void BackgroundManager::do_upload_background_file(FileId file_id, const BackgroundType &type,
                                                  tl_object_ptr<telegram_api::InputFile> &&input_file,
                                                  Promise<Unit> &&promise) {
  if (input_file == nullptr) {
    FileView file_view = td_->file_manager_->get_file_view(file_id);
    file_id = file_view.file_id();
    auto it = file_id_to_background_id_.find(file_id);
    if (it != file_id_to_background_id_.end()) {
      set_background(it->second, type, std::move(promise));
      return;
    }
    return promise.set_error(Status::Error(500, "Failed to reupload background"));
  }

  td_->create_handler<UploadBackgroundQuery>(std::move(promise))->send(file_id, std::move(input_file), type);
}

void BackgroundManager::on_uploaded_background_file(FileId file_id, const BackgroundType &type,
                                                    telegram_api::object_ptr<telegram_api::wallPaper> wallpaper,
                                                    Promise<Unit> &&promise) {
  CHECK(wallpaper != nullptr);

  BackgroundId background_id = on_get_background(BackgroundId(), std::move(wallpaper));
  if (!background_id.is_valid()) {
    td_->file_manager_->cancel_upload(file_id);
    return promise.set_error(Status::Error(500, "Receive wrong uploaded background"));
  }

  auto background = get_background(background_id);
  CHECK(background != nullptr);
  LOG_STATUS(td_->file_manager_->merge(background->file_id, file_id));
  set_background_id(background_id, type);
  promise.set_value(Unit());
}

BackgroundManager::Background *BackgroundManager::add_background(BackgroundId background_id) {
  CHECK(background_id.is_valid());
  auto *result = &backgrounds_[background_id];
  if (!result->id.is_valid()) {
    auto it = background_id_to_file_source_id_.find(background_id);
    if (it != background_id_to_file_source_id_.end()) {
      result->file_source_id = it->second.second;
      background_id_to_file_source_id_.erase(it);
    }
  }
  return result;
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

BackgroundId BackgroundManager::on_get_background(BackgroundId expected_background_id,
                                                  telegram_api::object_ptr<telegram_api::wallPaper> wallpaper) {
  CHECK(wallpaper != nullptr);

  auto id = BackgroundId(wallpaper->id_);
  if (!id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(wallpaper);
    return BackgroundId();
  }
  if (expected_background_id.is_valid() && id != expected_background_id) {
    LOG(ERROR) << "Expected " << expected_background_id << ", but receive " << to_string(wallpaper);
  }
  if (wallpaper->slug_.size() <= 6 || (0 < wallpaper->id_ && wallpaper->id_ <= 0x1000000)) {
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
  CHECK(document.type == Document::Type::General);

  auto *background = add_background(id);
  background->id = id;
  background->access_hash = wallpaper->access_hash_;
  background->is_creator = (flags & telegram_api::wallPaper::CREATOR_MASK) != 0;
  background->is_default = (flags & telegram_api::wallPaper::DEFAULT_MASK) != 0;
  background->is_dark = (flags & telegram_api::wallPaper::DARK_MASK) != 0;
  background->type = get_background_type(is_pattern, std::move(wallpaper->settings_));
  if (background->name != wallpaper->slug_) {
    if (!background->name.empty()) {
      LOG(ERROR) << "Background name has changed from " << background->name << " to " << wallpaper->slug_;
      name_to_background_id_.erase(background->name);
    }

    background->name = std::move(wallpaper->slug_);
    name_to_background_id_.emplace(background->name, id);
  }
  if (background->file_id != document.file_id) {
    if (background->file_id.is_valid()) {
      LOG(ERROR) << "Background file has changed from " << background->file_id << " to " << document.file_id;
      file_id_to_background_id_.erase(background->file_id);
    }
    if (!background->file_source_id.is_valid()) {
      background->file_source_id =
          td_->file_reference_manager_->create_background_file_source(id, background->access_hash);
    }
    for (auto file_id : document.get_file_ids(td_)) {
      td_->file_manager_->add_file_source(file_id, background->file_source_id);
    }
    background->file_id = document.file_id;
    file_id_to_background_id_.emplace(background->file_id, id);
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
    auto background_id = on_get_background(BackgroundId(), std::move(wallpaper));
    if (background_id.is_valid()) {
      installed_background_ids_.push_back(background_id);
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

td_api::object_ptr<td_api::background> BackgroundManager::get_background_object(BackgroundId background_id) const {
  auto background = get_background(background_id);
  if (background == nullptr) {
    return nullptr;
  }
  auto type = &background->type;
  if (background_id == set_background_id_) {
    type = &set_background_type_;
  }
  return td_api::make_object<td_api::background>(
      background->id.get(), background->is_default, background->is_dark, background->name,
      td_->documents_manager_->get_document_object(background->file_id), get_background_type_object(*type));
}

td_api::object_ptr<td_api::backgrounds> BackgroundManager::get_backgrounds_object() const {
  auto background_ids = installed_background_ids_;
  if (set_background_id_.is_valid()) {
    auto it = std::find(background_ids.begin(), background_ids.end(), set_background_id_);
    if (it != background_ids.end()) {
      // move set background to the first place
      std::rotate(background_ids.begin(), it, it + 1);
    } else {
      background_ids.insert(background_ids.begin(), set_background_id_);
    }
  }
  return td_api::make_object<td_api::backgrounds>(
      transform(background_ids, [this](BackgroundId background_id) { return get_background_object(background_id); }));
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

}  // namespace td
