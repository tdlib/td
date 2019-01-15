//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WallpaperManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/Global.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"

namespace td {

class GetWallpapersQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::wallpapers>> promise_;

 public:
  explicit GetWallpapersQuery(Promise<td_api::object_ptr<td_api::wallpapers>> &&promise)
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

    auto wallpapers = result_ptr.move_as_ok();

    auto results = td_api::make_object<td_api::wallpapers>();
    results->wallpapers_.reserve(wallpapers.size());
    for (auto &wallpaper_ptr : wallpapers) {
      CHECK(wallpaper_ptr != nullptr);
      switch (wallpaper_ptr->get_id()) {
        case telegram_api::wallPaper::ID: {
          auto wallpaper = move_tl_object_as<telegram_api::wallPaper>(wallpaper_ptr);
          vector<td_api::object_ptr<td_api::photoSize>> sizes;
          sizes.reserve(wallpaper->sizes_.size());
          for (auto &size_ptr : wallpaper->sizes_) {
            auto photo_size = get_photo_size(td->file_manager_.get(), FileType::Wallpaper, 0, 0, DialogId(),
                                             std::move(size_ptr), false);
            sizes.push_back(get_photo_size_object(td->file_manager_.get(), &photo_size));
          }
          sort_photo_sizes(sizes);
          results->wallpapers_.push_back(
              td_api::make_object<td_api::wallpaper>(wallpaper->id_, std::move(sizes), wallpaper->color_));
          break;
        }
        case telegram_api::wallPaperSolid::ID: {
          auto wallpaper = move_tl_object_as<telegram_api::wallPaperSolid>(wallpaper_ptr);
          results->wallpapers_.push_back(td_api::make_object<td_api::wallpaper>(
              wallpaper->id_, vector<td_api::object_ptr<td_api::photoSize>>(), wallpaper->bg_color_));
          break;
        }
        default:
          UNREACHABLE();
      }
    }
    promise_.set_value(std::move(results));
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

void WallpaperManager::get_wallpapers(Promise<td_api::object_ptr<td_api::wallpapers>> &&promise) {
  td_->create_handler<GetWallpapersQuery>(std::move(promise))->send();
}

}  // namespace td
