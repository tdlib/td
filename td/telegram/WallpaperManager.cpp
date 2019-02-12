//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WallpaperManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
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

class GetWallpapersQuery : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::WallPaper>>> promise_;

 public:
  explicit GetWallpapersQuery(Promise<vector<telegram_api::object_ptr<telegram_api::WallPaper>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getWallPapers())));
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

WallpaperManager::WallpaperManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void WallpaperManager::tear_down() {
  parent_.reset();
}

void WallpaperManager::get_wallpapers(Promise<Unit> &&promise) {
  if (!wallpapers_.empty()) {
    return promise.set_value(Unit());
  }

  reload_wallpapers(std::move(promise));
}

void WallpaperManager::reload_wallpapers(Promise<Unit> &&promise) {
  pending_get_wallpapers_queries_.push_back(std::move(promise));
  if (pending_get_wallpapers_queries_.size() == 1) {
    auto request_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<vector<telegram_api::object_ptr<telegram_api::WallPaper>>> result) {
          send_closure(actor_id, &WallpaperManager::on_get_wallpapers, std::move(result));
        });

    td_->create_handler<GetWallpapersQuery>(std::move(request_promise))->send();
  }
}

void WallpaperManager::on_get_wallpapers(Result<vector<telegram_api::object_ptr<telegram_api::WallPaper>>> result) {
  auto promises = std::move(pending_get_wallpapers_queries_);
  CHECK(!promises.empty());
  reset_to_empty(pending_get_wallpapers_queries_);

  if (result.is_error()) {
    // do not clear wallpapers_

    auto error = result.move_as_error();
    for (auto &promise : promises) {
      promise.set_error(error.clone());
    }
    return;
  }

  wallpapers_ = transform(result.move_as_ok(), [file_manager = td_->file_manager_.get()](
                                                   tl_object_ptr<telegram_api::WallPaper> &&wallpaper_ptr) {
    CHECK(wallpaper_ptr != nullptr);
    switch (wallpaper_ptr->get_id()) {
      case telegram_api::wallPaper::ID: {
        auto wallpaper = move_tl_object_as<telegram_api::wallPaper>(wallpaper_ptr);
        vector<PhotoSize> sizes = transform(std::move(wallpaper->sizes_),
                                            [file_manager](tl_object_ptr<telegram_api::PhotoSize> &&photo_size) {
                                              return get_photo_size(file_manager, FileType::Wallpaper, 0, 0, "",
                                                                    DialogId(), std::move(photo_size), false);
                                            });
        return Wallpaper{wallpaper->id_, std::move(sizes), wallpaper->color_};
      }
      case telegram_api::wallPaperSolid::ID: {
        auto wallpaper = move_tl_object_as<telegram_api::wallPaperSolid>(wallpaper_ptr);
        return Wallpaper{wallpaper->id_, {}, wallpaper->bg_color_};
      }
      default:
        UNREACHABLE();
        return Wallpaper{0, {}, 0};
    }
  });
  vector<FileId> new_file_ids;
  for (auto &wallpaper : wallpapers_) {
    append(new_file_ids, transform(wallpaper.sizes, [](auto &size) { return size.file_id; }));
  };
  td_->file_manager_->change_files_source(get_wallpapers_file_source_id(), wallpaper_file_ids_, new_file_ids);
  wallpaper_file_ids_ = std::move(new_file_ids);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

FileSourceId WallpaperManager::get_wallpapers_file_source_id() {
  if (!wallpaper_source_id_.is_valid()) {
    wallpaper_source_id_ = td_->file_reference_manager_->create_wallpapers_file_source();
  }
  return wallpaper_source_id_;
}

td_api::object_ptr<td_api::wallpapers> WallpaperManager::get_wallpapers_object() const {
  return td_api::make_object<td_api::wallpapers>(
      transform(wallpapers_, [file_manager = td_->file_manager_.get()](const Wallpaper &wallpaper) {
        return td_api::make_object<td_api::wallpaper>(
            wallpaper.id, get_photo_sizes_object(file_manager, wallpaper.sizes), wallpaper.color);
      }));
}

}  // namespace td
