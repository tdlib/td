//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class WallpaperManager : public Actor {
 public:
  WallpaperManager(Td *td, ActorShared<> parent);

  void get_wallpapers(Promise<Unit> &&promise);

  void reload_wallpapers(Promise<Unit> &&promise);

  td_api::object_ptr<td_api::wallpapers> get_wallpapers_object() const;

  FileSourceId get_wallpapers_file_source_id();

 private:
  void tear_down() override;

  void on_get_wallpapers(Result<vector<telegram_api::object_ptr<telegram_api::WallPaper>>> result);

  struct Wallpaper {
    int32 id = 0;
    vector<PhotoSize> sizes;
    int32 color = 0;

    Wallpaper(int32 id, vector<PhotoSize> sizes, int32 color) : id(id), sizes(std::move(sizes)), color(color) {
    }
  };
  vector<Wallpaper> wallpapers_;
  vector<FileId> wallpaper_file_ids_;
  FileSourceId wallpaper_source_id_;

  vector<Promise<Unit>> pending_get_wallpapers_queries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
