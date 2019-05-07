//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/misc.h"

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

BackgroundManager::BackgroundManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
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

Result<BackgroundManager::BackgroundType> BackgroundManager::get_background_type(
    td_api::object_ptr<td_api::BackgroundType> type) {
  if (type == nullptr) {
    return Status::Error(400, "Type must not be empty");
  }

  BackgroundType result;
  switch (type->get_id()) {
    case td_api::backgroundTypeWallpaper::ID: {
      auto wallpaper = td_api::move_object_as<td_api::backgroundTypeWallpaper>(type);
      result = BackgroundType(wallpaper->is_blurred_, wallpaper->is_moving_);
      break;
    }
    case td_api::backgroundTypePattern::ID: {
      auto pattern = td_api::move_object_as<td_api::backgroundTypePattern>(type);
      result = BackgroundType(pattern->is_moving_, pattern->color_, pattern->intensity_);
      break;
    }
    case td_api::backgroundTypeSolid::ID: {
      auto solid = td_api::move_object_as<td_api::backgroundTypeSolid>(type);
      result = BackgroundType(solid->color_);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (result.intensity < 0 || result.intensity > 100) {
    return Status::Error(400, "Wrong intensity value");
  }
  if (result.color < 0 || result.color > 0xFFFFFF) {
    return Status::Error(400, "Wrong color value");
  }
  return result;
}

Result<string> BackgroundManager::get_background_url(const string &name,
                                                     td_api::object_ptr<td_api::BackgroundType> background_type) const {
  TRY_RESULT(type, get_background_type(std::move(background_type)));

  vector<string> modes;
  if (type.is_blurred) {
    modes.emplace_back("blur");
  }
  if (type.is_moving) {
    modes.emplace_back("motion");
  }
  string mode = implode(modes, '+');

  auto get_color_string = [](int32 color) {
    string result;
    for (int i = 20; i >= 0; i -= 4) {
      result += "0123456789abcdef"[(color >> i) & 0xf];
    }
    return result;
  };

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
      url += get_color_string(type.color);
      if (!mode.empty()) {
        url += "&mode=";
        url += mode;
      }
      return url;
    case BackgroundType::Type::Solid:
      url += get_color_string(type.color);
      return url;
    default:
      UNREACHABLE();
      return url;
  }
}

void BackgroundManager::reload_background(BackgroundId background_id,
                                          telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper,
                                          Promise<Unit> &&promise) const {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  td_->create_handler<GetBackgroundQuery>(std::move(promise))->send(background_id, std::move(input_wallpaper));
}

BackgroundId BackgroundManager::search_background(const string &name, Promise<Unit> &&promise) {
  auto it = name_to_background_id_.find(name);
  if (it != name_to_background_id_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  reload_background(BackgroundId(), telegram_api::make_object<telegram_api::inputWallPaperSlug>(name),
                    std::move(promise));
  return BackgroundId();
}

BackgroundManager::Background *BackgroundManager::add_background(BackgroundId background_id) {
  CHECK(background_id.is_valid());
  return &backgrounds_[background_id];
}

const BackgroundManager::Background *BackgroundManager::get_background(BackgroundId background_id) const {
  auto p = backgrounds_.find(background_id);
  if (p == backgrounds_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

BackgroundManager::BackgroundType BackgroundManager::get_background_type(
    bool is_pattern, telegram_api::object_ptr<telegram_api::wallPaperSettings> settings) {
  bool is_blurred = false;
  bool is_moving = false;
  int32 color = 0;
  int32 intensity = 0;
  if (settings) {
    auto flags = settings->flags_;
    is_blurred = (flags & telegram_api::wallPaperSettings::BLUR_MASK) != 0;
    is_moving = (flags & telegram_api::wallPaperSettings::MOTION_MASK) != 0;
    if ((flags & telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK) != 0) {
      color = settings->background_color_;
      if (color < 0 || color > 0xFFFFFF) {
        LOG(ERROR) << "Receive " << to_string(settings);
        color = 0;
      }
    }
    if ((flags & telegram_api::wallPaperSettings::INTENSITY_MASK) != 0) {
      intensity = settings->intensity_;
      if (intensity < 0 || intensity > 100) {
        LOG(ERROR) << "Receive " << to_string(settings);
        intensity = 0;
      }
    }
  }
  if (is_pattern) {
    return BackgroundType(is_moving, color, intensity);
  } else {
    return BackgroundType(is_blurred, is_moving);
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
  background->file_id = document.file_id;
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

  return id;
}

void BackgroundManager::on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result) {
  auto promises = std::move(pending_get_backgrounds_queries_);
  CHECK(!promises.empty());
  reset_to_empty(pending_get_backgrounds_queries_);

  if (result.is_error()) {
    // do not clear installed_backgrounds_

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

  installed_backgrounds_.clear();
  auto wallpapers = telegram_api::move_object_as<telegram_api::account_wallPapers>(wallpapers_ptr);
  for (auto &wallpaper : wallpapers->wallpapers_) {
    auto background_id = on_get_background(BackgroundId(), std::move(wallpaper));
    if (background_id.is_valid()) {
      installed_backgrounds_.push_back(background_id);
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

td_api::object_ptr<td_api::BackgroundType> BackgroundManager::get_background_type_object(const BackgroundType &type) {
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
      return td_api::make_object<td_api::backgroundTypeWallpaper>(type.is_blurred, type.is_moving);
    case BackgroundType::Type::Pattern:
      return td_api::make_object<td_api::backgroundTypePattern>(type.is_moving, type.color, type.intensity);
    case BackgroundType::Type::Solid:
      return td_api::make_object<td_api::backgroundTypeSolid>(type.color);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::background> BackgroundManager::get_background_object(BackgroundId background_id) const {
  auto background = get_background(background_id);
  if (background == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::background>(
      background->id.get(), background->is_default, background->is_dark, background->name,
      td_->documents_manager_->get_document_object(background->file_id), get_background_type_object(background->type));
}

td_api::object_ptr<td_api::backgrounds> BackgroundManager::get_backgrounds_object() const {
  return td_api::make_object<td_api::backgrounds>(transform(
      installed_backgrounds_, [this](BackgroundId background_id) { return get_background_object(background_id); }));
}

}  // namespace td
