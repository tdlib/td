//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/SecretInputMedia.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {
class Td;
template <class T>
class Promise;
}  // namespace td

namespace td {

class AnimationsManager : public Actor {
 public:
  AnimationsManager(Td *td, ActorShared<> parent);

  int32 get_animation_duration(FileId file_id);

  tl_object_ptr<td_api::animation> get_animation_object(FileId file_id, const char *source);

  void create_animation(FileId file_id, PhotoSize thumbnail, string file_name, string mime_type, int32 duration,
                        Dimensions dimensions, bool replace);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file,
                                                          tl_object_ptr<telegram_api::InputFile> input_thumbnail) const;

  SecretInputMedia get_secret_input_media(FileId animation_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          const string &caption, BufferSlice thumbnail) const;

  FileId get_animation_thumbnail_file_id(FileId file_id) const;

  void delete_animation_thumbnail(FileId file_id);

  FileId dup_animation(FileId new_id, FileId old_id);

  bool merge_animations(FileId new_id, FileId old_id, bool can_delete_old);

  void on_update_saved_animations_limit(int32 saved_animations_limit);

  void reload_saved_animations(bool force);

  void on_get_saved_animations(tl_object_ptr<telegram_api::messages_SavedGifs> &&saved_animations_ptr);

  void on_get_saved_animations_failed(Status error);

  vector<FileId> get_saved_animations(Promise<Unit> &&promise);

  void add_saved_animation(const tl_object_ptr<td_api::InputFile> &input_file, Promise<Unit> &&promise);

  void add_saved_animation_by_id(FileId animation_id);

  void remove_saved_animation(const tl_object_ptr<td_api::InputFile> &input_file, Promise<Unit> &&promise);

  template <class T>
  void store_animation(FileId file_id, T &storer) const;

  template <class T>
  FileId parse_animation(T &parser);

  string get_animation_search_text(FileId file_id) const;

 private:
  class Animation {
   public:
    string file_name;
    string mime_type;
    int32 duration = 0;
    Dimensions dimensions;
    PhotoSize thumbnail;

    FileId file_id;

    bool is_changed = true;
  };

  const Animation *get_animation(FileId file_id) const;

  FileId on_get_animation(std::unique_ptr<Animation> new_animation, bool replace);

  int32 get_saved_animations_hash(const char *source) const;

  void add_saved_animation_inner(FileId animation_id, Promise<Unit> &&promise);

  bool add_saved_animation_impl(FileId animation_id, Promise<Unit> &promise);

  void load_saved_animations(Promise<Unit> &&promise);

  void on_load_saved_animations_from_database(const string &value);

  void on_load_saved_animations_finished(vector<FileId> &&saved_animation_ids, bool from_database = false);

  void send_update_saved_animations(bool from_database = false);

  void save_saved_animations_to_database();

  void tear_down() override;

  class AnimationListLogEvent;

  Td *td_;
  ActorShared<> parent_;

  std::unordered_map<FileId, unique_ptr<Animation>, FileIdHash> animations_;

  int32 saved_animations_limit_ = 200;
  vector<FileId> saved_animation_ids_;
  double next_saved_animations_load_time_ = 0;
  bool are_saved_animations_loaded_ = false;
  vector<Promise<Unit>> load_saved_animations_queries_;
};

}  // namespace td
