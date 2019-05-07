//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {

class Td;

class BackgroundManager : public Actor {
 public:
  BackgroundManager(Td *td, ActorShared<> parent);

  void get_backgrounds(Promise<Unit> &&promise);

  td_api::object_ptr<td_api::background> get_background_object(BackgroundId background_id) const;

  td_api::object_ptr<td_api::backgrounds> get_backgrounds_object() const;

 private:
  struct BackgroundType {
    enum class Type : int32 { Wallpaper, Pattern, Solid };
    Type type = Type::Solid;
    bool is_blurred = false;
    bool is_moving = false;
    int32 color = 0;
    int32 intensity = 0;

    BackgroundType() = default;
    BackgroundType(bool is_blurred, bool is_moving)
        : type(Type::Wallpaper), is_blurred(is_blurred), is_moving(is_moving) {
    }
    BackgroundType(bool is_moving, int32 color, int32 intensity)
        : type(Type::Pattern), is_moving(is_moving), color(color), intensity(intensity) {
    }
    explicit BackgroundType(int32 color) : type(Type::Solid), color(color) {
    }
  };

  struct Background {
    BackgroundId id;
    int64 access_hash = 0;
    string name;
    FileId file_id;
    bool is_creator = false;
    bool is_default = false;
    bool is_dark = false;
    BackgroundType type;
  };

  void tear_down() override;

  Background *add_background(BackgroundId background_id);

  const Background *get_background(BackgroundId background_id) const;

  static BackgroundType get_background_type(bool is_pattern,
                                            telegram_api::object_ptr<telegram_api::wallPaperSettings> settings);

  BackgroundId on_get_background(telegram_api::object_ptr<telegram_api::wallPaper> wallpaper);

  void on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result);

  static td_api::object_ptr<td_api::BackgroundType> get_background_type_object(const BackgroundType &type);

  std::unordered_map<BackgroundId, Background, BackgroundIdHash> backgrounds_;  // id -> Background

  vector<BackgroundId> installed_backgrounds_;

  vector<Promise<Unit>> pending_get_backgrounds_queries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
