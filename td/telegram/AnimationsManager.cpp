//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AnimationsManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>

namespace td {

class GetSavedGifsQuery : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    LOG(INFO) << "Send get saved animations request with hash = " << hash;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getSavedGifs(hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedGifs>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td->animations_manager_->on_get_saved_animations(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for get saved animations: " << status;
    td->animations_manager_->on_get_saved_animations_failed(std::move(status));
  }
};

class SaveGifQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveGifQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputDocument> &&input_document, bool unsave) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_saveGif(std::move(input_document), unsave))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_saveGif>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for save gif: " << result;
    if (!result) {
      td->animations_manager_->reload_saved_animations(true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for save gif: " << status;
    td->animations_manager_->reload_saved_animations(true);
    promise_.set_error(std::move(status));
  }
};

AnimationsManager::AnimationsManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  auto limit_string = G()->td_db()->get_binlog_pmc()->get("saved_animations_limit");
  if (!limit_string.empty()) {
    auto new_limit = to_integer<int32>(limit_string);
    if (new_limit > 0) {
      LOG(DEBUG) << "Load saved animations limit = " << new_limit;
      saved_animations_limit_ = new_limit;
    } else {
      LOG(ERROR) << "Wrong saved animations limit = \"" << limit_string << "\" stored in database";
    }
  }
}

void AnimationsManager::tear_down() {
  parent_.reset();
}

int32 AnimationsManager::get_animation_duration(FileId file_id) {
  auto &animation = animations_[file_id];
  CHECK(animation != nullptr);
  return animation->duration;
}

tl_object_ptr<td_api::animation> AnimationsManager::get_animation_object(FileId file_id, const char *source) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &animation = animations_[file_id];
  CHECK(animation != nullptr) << source << " " << file_id << " "
                              << static_cast<int32>(td_->file_manager_->get_file_view(file_id).get_type());
  // TODO can we make that function const?
  animation->is_changed = false;
  return make_tl_object<td_api::animation>(animation->duration, animation->dimensions.width,
                                           animation->dimensions.height, animation->file_name, animation->mime_type,
                                           get_photo_size_object(td_->file_manager_.get(), &animation->thumbnail),
                                           td_->file_manager_->get_file_object(file_id));
}

FileId AnimationsManager::on_get_animation(std::unique_ptr<Animation> new_animation, bool replace) {
  auto file_id = new_animation->file_id;
  LOG(INFO) << (replace ? "Replace" : "Add") << " animation " << file_id << " of size " << new_animation->dimensions;
  auto &a = animations_[file_id];
  if (a == nullptr) {
    a = std::move(new_animation);
  } else if (replace) {
    CHECK(a->file_id == file_id);
    if (a->mime_type != new_animation->mime_type) {
      LOG(DEBUG) << "Animation " << file_id << " info has changed";
      a->mime_type = new_animation->mime_type;
      a->is_changed = true;
    }
    if (a->file_name != new_animation->file_name) {
      LOG(DEBUG) << "Animation " << file_id << " file name has changed";
      a->file_name = std::move(new_animation->file_name);
      a->is_changed = true;
    }
    if (a->dimensions != new_animation->dimensions) {
      LOG(DEBUG) << "Animation " << file_id << " dimensions has changed";
      a->dimensions = new_animation->dimensions;
      a->is_changed = true;
    }
    if (a->duration != new_animation->duration) {
      LOG(DEBUG) << "Animation " << file_id << " duration has changed";
      a->duration = new_animation->duration;
      a->is_changed = true;
    }
    if (a->thumbnail != new_animation->thumbnail) {
      if (!a->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Animation " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Animation " << file_id << " thumbnail has changed from " << a->thumbnail << " to "
                  << new_animation->thumbnail;
      }
      a->thumbnail = new_animation->thumbnail;
      a->is_changed = true;
    }
  }

  return file_id;
}

const AnimationsManager::Animation *AnimationsManager::get_animation(FileId file_id) const {
  auto animation = animations_.find(file_id);
  if (animation == animations_.end()) {
    return nullptr;
  }

  CHECK(animation->second->file_id == file_id);
  return animation->second.get();
}

FileId AnimationsManager::get_animation_thumbnail_file_id(FileId file_id) const {
  auto animation = get_animation(file_id);
  CHECK(animation != nullptr);
  return animation->thumbnail.file_id;
}

void AnimationsManager::delete_animation_thumbnail(FileId file_id) {
  auto &animation = animations_[file_id];
  CHECK(animation != nullptr);
  animation->thumbnail = PhotoSize();
}

FileId AnimationsManager::dup_animation(FileId new_id, FileId old_id) {
  LOG(INFO) << "Dup animation " << old_id << " to " << new_id;
  const Animation *old_animation = get_animation(old_id);
  CHECK(old_animation != nullptr);
  auto &new_animation = animations_[new_id];
  CHECK(!new_animation);
  new_animation = std::make_unique<Animation>(*old_animation);
  new_animation->file_id = new_id;
  new_animation->thumbnail.file_id = td_->file_manager_->dup_file_id(new_animation->thumbnail.file_id);
  return new_id;
}

bool AnimationsManager::merge_animations(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge animations " << new_id << " and " << old_id;
  const Animation *old_ = get_animation(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = animations_.find(new_id);
  if (new_it == animations_.end()) {
    auto &old = animations_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_animation(new_id, old_id);
    } else {
      old->file_id = new_id;
      animations_.emplace(new_id, std::move(old));
    }
  } else {
    Animation *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    new_->is_changed = true;
    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    animations_.erase(old_id);
  }
  return true;
}

void AnimationsManager::create_animation(FileId file_id, PhotoSize thumbnail, string file_name, string mime_type,
                                         int32 duration, Dimensions dimensions, bool replace) {
  auto a = make_unique<Animation>();
  a->file_id = file_id;
  a->file_name = std::move(file_name);
  a->mime_type = std::move(mime_type);
  a->duration = max(duration, 0);
  a->dimensions = dimensions;
  a->thumbnail = std::move(thumbnail);
  on_get_animation(std::move(a), replace);
}

tl_object_ptr<telegram_api::InputMedia> AnimationsManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
    return make_tl_object<telegram_api::inputMediaDocument>(0, file_view.remote_location().as_input_document(), 0);
  }
  if (file_view.has_url()) {
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(0, file_view.url(), 0);
  }
  CHECK(!file_view.has_remote_location());

  if (input_file) {
    const Animation *animation = get_animation(file_id);
    CHECK(animation != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    if (!animation->file_name.empty()) {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(animation->file_name));
    }
    string mime_type = animation->mime_type;
    if (animation->mime_type == "video/mp4") {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeVideo>(
          0, false /*ignored*/, false /*ignored*/, animation->duration, animation->dimensions.width,
          animation->dimensions.height));
    } else if (animation->dimensions.width != 0 && animation->dimensions.height != 0) {
      if (!begins_with(mime_type, "image/")) {
        mime_type = "image/gif";
      }
      attributes.push_back(make_tl_object<telegram_api::documentAttributeImageSize>(animation->dimensions.width,
                                                                                    animation->dimensions.height));
    }
    int32 flags = 0;
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), mime_type, std::move(attributes),
        vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  }

  return nullptr;
}
SecretInputMedia AnimationsManager::get_secret_input_media(FileId animation_file_id,
                                                           tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                           const string &caption, BufferSlice thumbnail) const {
  auto *animation = get_animation(animation_file_id);
  CHECK(animation != nullptr);
  auto file_view = td_->file_manager_->get_file_view(animation_file_id);
  auto &encryption_key = file_view.encryption_key();
  if (!file_view.is_encrypted_secret() || encryption_key.empty()) {
    return SecretInputMedia{};
  }
  if (file_view.has_remote_location()) {
    input_file = file_view.remote_location().as_input_encrypted_file();
  }
  if (!input_file) {
    return SecretInputMedia{};
  }
  if (animation->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return SecretInputMedia{};
  }
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  if (!animation->file_name.empty()) {
    attributes.push_back(make_tl_object<secret_api::documentAttributeFilename>(animation->file_name));
  }
  if (animation->duration != 0 && animation->mime_type == "video/mp4") {
    attributes.push_back(make_tl_object<secret_api::documentAttributeVideo>(
        animation->duration, animation->dimensions.width, animation->dimensions.height));
  }
  if (animation->dimensions.width != 0 && animation->dimensions.height != 0) {
    attributes.push_back(make_tl_object<secret_api::documentAttributeImageSize>(animation->dimensions.width,
                                                                                animation->dimensions.height));
  }
  attributes.push_back(make_tl_object<secret_api::documentAttributeAnimated>());

  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(thumbnail), animation->thumbnail.dimensions.width, animation->thumbnail.dimensions.height,
          animation->mime_type, narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
          BufferSlice(encryption_key.iv_slice()), std::move(attributes), caption)};
}

void AnimationsManager::on_update_saved_animations_limit(int32 saved_animations_limit) {
  if (saved_animations_limit != saved_animations_limit_) {
    if (saved_animations_limit > 0) {
      LOG(INFO) << "Update saved animations limit to " << saved_animations_limit;
      G()->td_db()->get_binlog_pmc()->set("saved_animations_limit", to_string(saved_animations_limit));
      saved_animations_limit_ = saved_animations_limit;
      if (static_cast<int32>(saved_animation_ids_.size()) > saved_animations_limit_) {
        saved_animation_ids_.resize(saved_animations_limit_);
        send_update_saved_animations();
      }
    } else {
      LOG(ERROR) << "Receive wrong saved animations limit = " << saved_animations_limit;
    }
  }
}

class AnimationsManager::AnimationListLogEvent {
 public:
  vector<FileId> animation_ids;

  AnimationListLogEvent() = default;

  explicit AnimationListLogEvent(vector<FileId> animation_ids) : animation_ids(std::move(animation_ids)) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    AnimationsManager *animations_manager = storer.context()->td().get_actor_unsafe()->animations_manager_.get();
    td::store(narrow_cast<int32>(animation_ids.size()), storer);
    for (auto animation_id : animation_ids) {
      animations_manager->store_animation(animation_id, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    AnimationsManager *animations_manager = parser.context()->td().get_actor_unsafe()->animations_manager_.get();
    int32 size = parser.fetch_int();
    animation_ids.resize(size);
    for (auto &animation_id : animation_ids) {
      animation_id = animations_manager->parse_animation(parser);
    }
  }
};

void AnimationsManager::reload_saved_animations(bool force) {
  if (!td_->auth_manager_->is_bot() && next_saved_animations_load_time_ >= 0 &&
      (next_saved_animations_load_time_ < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload saved animations";
    next_saved_animations_load_time_ = -1;
    td_->create_handler<GetSavedGifsQuery>()->send(get_saved_animations_hash("reload_saved_animations"));
  }
}

vector<FileId> AnimationsManager::get_saved_animations(Promise<Unit> &&promise) {
  if (!are_saved_animations_loaded_) {
    load_saved_animations(std::move(promise));
    return {};
  }
  reload_saved_animations(false);

  promise.set_value(Unit());
  return saved_animation_ids_;
}

void AnimationsManager::load_saved_animations(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_saved_animations_loaded_ = true;
  }
  if (are_saved_animations_loaded_) {
    promise.set_value(Unit());
    return;
  }
  load_saved_animations_queries_.push_back(std::move(promise));
  if (load_saved_animations_queries_.size() == 1u) {
    if (G()->parameters().use_file_db) {  // otherwise there is no sqlite_pmc, TODO
      LOG(INFO) << "Trying to load saved animations from database";
      G()->td_db()->get_sqlite_pmc()->get("ans", PromiseCreator::lambda([](string value) {
                                            send_closure(G()->animations_manager(),
                                                         &AnimationsManager::on_load_saved_animations_from_database,
                                                         std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load saved animations from server";
      reload_saved_animations(true);
    }
  }
}

void AnimationsManager::on_load_saved_animations_from_database(const string &value) {
  if (value.empty()) {
    LOG(INFO) << "Saved animations aren't found in database";
    reload_saved_animations(true);
    return;
  }

  LOG(INFO) << "Successfully loaded saved animations list of size " << value.size() << " from database";

  AnimationListLogEvent log_event;
  log_event_parse(log_event, value).ensure();

  on_load_saved_animations_finished(std::move(log_event.animation_ids), true);
}

void AnimationsManager::on_load_saved_animations_finished(vector<FileId> &&saved_animation_ids, bool from_database) {
  if (static_cast<int32>(saved_animation_ids.size()) > saved_animations_limit_) {
    saved_animation_ids.resize(saved_animations_limit_);
  }
  saved_animation_ids_ = std::move(saved_animation_ids);
  are_saved_animations_loaded_ = true;
  send_update_saved_animations(from_database);
  auto promises = std::move(load_saved_animations_queries_);
  load_saved_animations_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void AnimationsManager::on_get_saved_animations(
    tl_object_ptr<telegram_api::messages_SavedGifs> &&saved_animations_ptr) {
  CHECK(!td_->auth_manager_->is_bot());
  next_saved_animations_load_time_ = Time::now_cached() + Random::fast(30 * 60, 50 * 60);

  CHECK(saved_animations_ptr != nullptr);
  int32 constructor_id = saved_animations_ptr->get_id();
  if (constructor_id == telegram_api::messages_savedGifsNotModified::ID) {
    LOG(INFO) << "Saved animations are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_savedGifs::ID);
  auto saved_animations = move_tl_object_as<telegram_api::messages_savedGifs>(saved_animations_ptr);
  LOG(INFO) << "Receive " << saved_animations->gifs_.size() << " saved animations from server";

  vector<FileId> saved_animation_ids;
  saved_animation_ids.reserve(saved_animations->gifs_.size());
  for (auto &document_ptr : saved_animations->gifs_) {
    int32 document_constructor_id = document_ptr->get_id();
    if (document_constructor_id == telegram_api::documentEmpty::ID) {
      LOG(ERROR) << "Empty gif document received";
      continue;
    }
    CHECK(document_constructor_id == telegram_api::document::ID);
    auto document =
        td_->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(document_ptr), DialogId());
    if (document.first != DocumentsManager::DocumentType::Animation) {
      LOG(ERROR) << "Receive " << static_cast<int>(document.first) << " instead of animation as saved animation";
      continue;
    }
    saved_animation_ids.push_back(document.second);
  }

  on_load_saved_animations_finished(std::move(saved_animation_ids));

  LOG_IF(ERROR, get_saved_animations_hash("on_get_saved_animations") != saved_animations->hash_)
      << "Saved animations hash mismatch: " << saved_animations->hash_ << " vs "
      << get_saved_animations_hash("on_get_saved_animations 2");
}

void AnimationsManager::on_get_saved_animations_failed(Status error) {
  CHECK(error.is_error());
  next_saved_animations_load_time_ = Time::now_cached() + Random::fast(5, 10);
  auto promises = std::move(load_saved_animations_queries_);
  load_saved_animations_queries_.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

int32 AnimationsManager::get_saved_animations_hash(const char *source) const {
  vector<uint32> numbers;
  numbers.reserve(saved_animation_ids_.size() * 2);
  for (auto animation_id : saved_animation_ids_) {
    auto animation = get_animation(animation_id);
    CHECK(animation != nullptr);
    auto file_view = td_->file_manager_->get_file_view(animation_id);
    CHECK(file_view.has_remote_location());
    CHECK(file_view.remote_location().is_document()) << source << " " << file_view.remote_location();
    auto id = static_cast<uint64>(file_view.remote_location().get_id());
    numbers.push_back(static_cast<uint32>(id >> 32));
    numbers.push_back(static_cast<uint32>(id & 0xFFFFFFFF));
  }
  return get_vector_hash(numbers);
}

void AnimationsManager::add_saved_animation(const tl_object_ptr<td_api::InputFile> &input_file,
                                            Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_saved_animations_loaded_) {
    load_saved_animations(std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Animation, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  add_saved_animation_inner(r_file_id.ok(), std::move(promise));
}

void AnimationsManager::add_saved_animation_inner(FileId animation_id, Promise<Unit> &&promise) {
  if (add_saved_animation_impl(animation_id, promise)) {
    // TODO invokeAfter and log event
    auto file_view = td_->file_manager_->get_file_view(animation_id);
    td_->create_handler<SaveGifQuery>(std::move(promise))->send(file_view.remote_location().as_input_document(), false);
  }
}

void AnimationsManager::add_saved_animation_by_id(FileId animation_id) {
  // TODO log event
  Promise<Unit> promise;
  add_saved_animation_impl(animation_id, promise);
}

bool AnimationsManager::add_saved_animation_impl(FileId animation_id, Promise<Unit> &promise) {
  CHECK(!td_->auth_manager_->is_bot());

  if (!are_saved_animations_loaded_) {
    load_saved_animations(PromiseCreator::lambda([animation_id, promise = std::move(promise)](Result<> result) mutable {
      if (result.is_ok()) {
        send_closure(G()->animations_manager(), &AnimationsManager::add_saved_animation_inner, animation_id,
                     std::move(promise));
      } else {
        promise.set_error(result.move_as_error());
      }
    }));
    return false;
  }

  if (!saved_animation_ids_.empty() && saved_animation_ids_[0] == animation_id) {
    if (saved_animation_ids_[0].get_remote() == 0 && animation_id.get_remote() != 0) {
      saved_animation_ids_[0] = animation_id;
      save_saved_animations_to_database();
    }

    promise.set_value(Unit());
    return false;
  }

  auto animation = get_animation(animation_id);
  if (animation == nullptr) {
    promise.set_error(Status::Error(7, "Animation not found"));
    return false;
  }
  if (animation->mime_type != "video/mp4") {
    promise.set_error(Status::Error(7, "Only MPEG4 animations can be saved"));
    return false;
  }

  auto file_view = td_->file_manager_->get_file_view(animation_id);
  if (!file_view.has_remote_location()) {
    promise.set_error(Status::Error(7, "Can save only sent animations"));
    return false;
  }
  if (file_view.remote_location().is_web()) {
    promise.set_error(Status::Error(7, "Can't save web animations"));
    return false;
  }
  if (!file_view.remote_location().is_document()) {
    promise.set_error(Status::Error(7, "Can't save encrypted animations"));
    return false;
  }

  auto it = std::find(saved_animation_ids_.begin(), saved_animation_ids_.end(), animation_id);
  if (it == saved_animation_ids_.end()) {
    if (static_cast<int32>(saved_animation_ids_.size()) == saved_animations_limit_) {
      saved_animation_ids_.back() = animation_id;
    } else {
      saved_animation_ids_.push_back(animation_id);
    }
    it = saved_animation_ids_.end() - 1;
  }
  std::rotate(saved_animation_ids_.begin(), it, it + 1);
  if (saved_animation_ids_[0].get_remote() == 0 && animation_id.get_remote() != 0) {
    saved_animation_ids_[0] = animation_id;
  }

  send_update_saved_animations();
  return true;
}

void AnimationsManager::remove_saved_animation(const tl_object_ptr<td_api::InputFile> &input_file,
                                               Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_saved_animations_loaded_) {
    load_saved_animations(std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Animation, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  FileId file_id = r_file_id.ok();
  auto it = std::find(saved_animation_ids_.begin(), saved_animation_ids_.end(), file_id);
  if (it == saved_animation_ids_.end()) {
    return promise.set_value(Unit());
  }

  auto animation = get_animation(file_id);
  if (animation == nullptr) {
    return promise.set_error(Status::Error(7, "Animation not found"));
  }

  // TODO invokeAfter
  auto file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(file_view.has_remote_location());
  CHECK(file_view.remote_location().is_document()) << file_view.remote_location();
  CHECK(!file_view.remote_location().is_web());
  td_->create_handler<SaveGifQuery>(std::move(promise))->send(file_view.remote_location().as_input_document(), true);

  saved_animation_ids_.erase(it);

  send_update_saved_animations();
}

void AnimationsManager::send_update_saved_animations(bool from_database) {
  if (are_saved_animations_loaded_) {
    vector<int32> animations;
    animations.reserve(saved_animation_ids_.size());
    for (auto animation_id : saved_animation_ids_) {
      animations.push_back(animation_id.get());
    }
    send_closure(G()->td(), &Td::send_update, make_tl_object<td_api::updateSavedAnimations>(std::move(animations)));

    if (!from_database) {
      save_saved_animations_to_database();
    }
  }
}

void AnimationsManager::save_saved_animations_to_database() {
  if (G()->parameters().use_file_db) {
    LOG(INFO) << "Save saved animations to database";
    AnimationListLogEvent log_event(saved_animation_ids_);
    G()->td_db()->get_sqlite_pmc()->set("ans", log_event_store(log_event).as_slice().str(), Auto());
  }
}

string AnimationsManager::get_animation_search_text(FileId file_id) const {
  auto animation = get_animation(file_id);
  CHECK(animation != nullptr);
  return animation->file_name;
}

}  // namespace td
