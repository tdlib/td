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

  td_api::object_ptr<td_api::background> get_background_object(BackgroundId background_id) const;

  td_api::object_ptr<td_api::backgrounds> get_backgrounds_object() const;

  BackgroundId on_get_background(BackgroundId expected_background_id,
                                 telegram_api::object_ptr<telegram_api::wallPaper> wallpaper);

  FileSourceId get_background_file_source_id(BackgroundId background_id, int64 access_hash);

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

  void tear_down() override;

  void reload_background_from_server(BackgroundId background_id,
                                     telegram_api::object_ptr<telegram_api::InputWallPaper> &&input_wallpaper,
                                     Promise<Unit> &&promise) const;

  Background *add_background(BackgroundId background_id);

  Background *get_background_ref(BackgroundId background_id);

  const Background *get_background(BackgroundId background_id) const;

  void on_get_backgrounds(Result<telegram_api::object_ptr<telegram_api::account_WallPapers>> result);

  std::unordered_map<BackgroundId, Background, BackgroundIdHash> backgrounds_;

  std::unordered_map<BackgroundId, std::pair<int64, FileSourceId>, BackgroundIdHash>
      background_id_to_file_source_id_;  // id -> [access_hash, file_source_id]

  std::unordered_map<string, BackgroundId> name_to_background_id_;

  vector<BackgroundId> installed_backgrounds_;

  vector<Promise<Unit>> pending_get_backgrounds_queries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
