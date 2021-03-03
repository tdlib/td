//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StickersManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/StickerSetId.hpp"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SleepActor.h"

#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/algorithm.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace td {

class GetAllStickersQuery : public Td::ResultHandler {
  bool is_masks_;

 public:
  void send(bool is_masks, int32 hash) {
    is_masks_ = is_masks;
    if (is_masks) {
      send_query(G()->net_query_creator().create(telegram_api::messages_getMaskStickers(hash)));
    } else {
      send_query(G()->net_query_creator().create(telegram_api::messages_getAllStickers(hash)));
    }
  }

  void on_result(uint64 id, BufferSlice packet) override {
    static_assert(std::is_same<telegram_api::messages_getMaskStickers::ReturnType,
                               telegram_api::messages_getAllStickers::ReturnType>::value,
                  "");
    auto result_ptr = fetch_result<telegram_api::messages_getAllStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for get all " << (is_masks_ ? "masks" : "stickers") << ": " << to_string(ptr);
    td->stickers_manager_->on_get_installed_sticker_sets(is_masks_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for get all stickers: " << status;
    }
    td->stickers_manager_->on_get_installed_sticker_sets_failed(is_masks_, std::move(status));
  }
};

class SearchStickersQuery : public Td::ResultHandler {
  string emoji_;

 public:
  void send(string emoji, int32 hash) {
    emoji_ = std::move(emoji);
    send_query(G()->net_query_creator().create(telegram_api::messages_getStickers(emoji_, hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for search stickers: " << to_string(ptr);
    td->stickers_manager_->on_find_stickers_success(emoji_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for search stickers: " << status;
    }
    td->stickers_manager_->on_find_stickers_fail(emoji_, std::move(status));
  }
};

class GetEmojiKeywordsLanguageQuery : public Td::ResultHandler {
  Promise<vector<string>> promise_;

 public:
  explicit GetEmojiKeywordsLanguageQuery(Promise<vector<string>> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<string> &&language_codes) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_getEmojiKeywordsLanguages(std::move(language_codes))));
  }
  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getEmojiKeywordsLanguages>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result =
        transform(result_ptr.move_as_ok(), [](auto &&emoji_language) { return std::move(emoji_language->lang_code_); });
    promise_.set_value(std::move(result));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetEmojiKeywordsQuery : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> promise_;

 public:
  explicit GetEmojiKeywordsQuery(Promise<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &language_code) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getEmojiKeywords(language_code)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getEmojiKeywords>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetEmojiKeywordsDifferenceQuery : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> promise_;

 public:
  explicit GetEmojiKeywordsDifferenceQuery(
      Promise<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &language_code, int32 version) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_getEmojiKeywordsDifference(language_code, version)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getEmojiKeywordsDifference>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetEmojiUrlQuery : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::emojiURL>> promise_;

 public:
  explicit GetEmojiUrlQuery(Promise<telegram_api::object_ptr<telegram_api::emojiURL>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &language_code) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getEmojiURL(language_code)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getEmojiURL>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetArchivedStickerSetsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  StickerSetId offset_sticker_set_id_;
  bool is_masks_;

 public:
  explicit GetArchivedStickerSetsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_masks, StickerSetId offset_sticker_set_id, int32 limit) {
    offset_sticker_set_id_ = offset_sticker_set_id;
    is_masks_ = is_masks;
    LOG(INFO) << "Get archived " << (is_masks ? "mask" : "sticker") << " sets from " << offset_sticker_set_id
              << " with limit " << limit;

    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::messages_getArchivedStickers::MASKS_MASK;
    }
    is_masks_ = is_masks;

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getArchivedStickers(flags, is_masks /*ignored*/, offset_sticker_set_id.get(), limit)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getArchivedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetArchivedStickerSetsQuery: " << to_string(ptr);
    td->stickers_manager_->on_get_archived_sticker_sets(is_masks_, offset_sticker_set_id_, std::move(ptr->sets_),
                                                        ptr->count_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetFeaturedStickerSetsQuery : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    LOG(INFO) << "Get trending sticker sets with hash " << hash;
    send_query(G()->net_query_creator().create(telegram_api::messages_getFeaturedStickers(hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getFeaturedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetFeaturedStickerSetsQuery: " << to_string(ptr);
    td->stickers_manager_->on_get_featured_sticker_sets(-1, -1, 0, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    td->stickers_manager_->on_get_featured_sticker_sets_failed(-1, -1, 0, std::move(status));
  }
};

class GetOldFeaturedStickerSetsQuery : public Td::ResultHandler {
  int32 offset_;
  int32 limit_;
  uint32 generation_;

 public:
  void send(int32 offset, int32 limit, uint32 generation) {
    offset_ = offset;
    limit_ = limit;
    generation_ = generation;
    LOG(INFO) << "Get old trending sticker sets with offset = " << offset << " and limit = " << limit;
    send_query(G()->net_query_creator().create(telegram_api::messages_getOldFeaturedStickers(offset, limit, 0)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getOldFeaturedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetOldFeaturedStickerSetsQuery: " << to_string(ptr);
    td->stickers_manager_->on_get_featured_sticker_sets(offset_, limit_, generation_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    td->stickers_manager_->on_get_featured_sticker_sets_failed(offset_, limit_, generation_, std::move(status));
  }
};

class GetAttachedStickerSetsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;
  string file_reference_;

 public:
  explicit GetAttachedStickerSetsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, string &&file_reference,
            tl_object_ptr<telegram_api::InputStickeredMedia> &&input_stickered_media) {
    file_id_ = file_id;
    file_reference_ = std::move(file_reference);
    send_query(
        G()->net_query_creator().create(telegram_api::messages_getAttachedStickers(std::move(input_stickered_media))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getAttachedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_attached_sticker_sets(file_id_, result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!td->auth_manager_->is_bot() && FileReferenceManager::is_file_reference_error(status)) {
      VLOG(file_references) << "Receive " << status << " for " << file_id_;
      td->file_manager_->delete_file_reference(file_id_, file_reference_);
      td->file_reference_manager_->repair_file_reference(
          file_id_,
          PromiseCreator::lambda([file_id = file_id_, promise = std::move(promise_)](Result<Unit> result) mutable {
            if (result.is_error()) {
              return promise.set_error(Status::Error(400, "Failed to find the file"));
            }

            send_closure(G()->stickers_manager(), &StickersManager::send_get_attached_stickers_query, file_id,
                         std::move(promise));
          }));
      return;
    }

    promise_.set_error(std::move(status));
  }
};

class GetRecentStickersQuery : public Td::ResultHandler {
  bool is_repair_ = false;
  bool is_attached_ = false;

 public:
  void send(bool is_repair, bool is_attached, int32 hash) {
    is_repair_ = is_repair;
    is_attached_ = is_attached;
    int32 flags = 0;
    if (is_attached) {
      flags |= telegram_api::messages_getRecentStickers::ATTACHED_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getRecentStickers(flags, is_attached /*ignored*/, hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getRecentStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for get recent " << (is_attached_ ? "attached " : "")
               << "stickers: " << to_string(ptr);
    td->stickers_manager_->on_get_recent_stickers(is_repair_, is_attached_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for get recent " << (is_attached_ ? "attached " : "") << "stickers: " << status;
    }
    td->stickers_manager_->on_get_recent_stickers_failed(is_repair_, is_attached_, std::move(status));
  }
};

class SaveRecentStickerQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;
  string file_reference_;
  bool unsave_ = false;
  bool is_attached_;

 public:
  explicit SaveRecentStickerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_attached, FileId file_id, tl_object_ptr<telegram_api::inputDocument> &&input_document,
            bool unsave) {
    CHECK(input_document != nullptr);
    CHECK(file_id.is_valid());
    file_id_ = file_id;
    file_reference_ = input_document->file_reference_.as_slice().str();
    unsave_ = unsave;
    is_attached_ = is_attached;

    int32 flags = 0;
    if (is_attached) {
      flags |= telegram_api::messages_saveRecentSticker::ATTACHED_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_saveRecentSticker(flags, is_attached /*ignored*/, std::move(input_document), unsave)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_saveRecentSticker>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for save recent " << (is_attached_ ? "attached " : "") << "sticker: " << result;
    if (!result) {
      td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!td->auth_manager_->is_bot() && FileReferenceManager::is_file_reference_error(status)) {
      VLOG(file_references) << "Receive " << status << " for " << file_id_;
      td->file_manager_->delete_file_reference(file_id_, file_reference_);
      td->file_reference_manager_->repair_file_reference(
          file_id_, PromiseCreator::lambda([sticker_id = file_id_, is_attached = is_attached_, unsave = unsave_,
                                            promise = std::move(promise_)](Result<Unit> result) mutable {
            if (result.is_error()) {
              return promise.set_error(Status::Error(400, "Failed to find the sticker"));
            }

            send_closure(G()->stickers_manager(), &StickersManager::send_save_recent_sticker_query, is_attached,
                         sticker_id, unsave, std::move(promise));
          }));
      return;
    }

    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for save recent " << (is_attached_ ? "attached " : "") << "sticker: " << status;
    }
    td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    promise_.set_error(std::move(status));
  }
};

class ClearRecentStickersQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  bool is_attached_;

 public:
  explicit ClearRecentStickersQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_attached) {
    is_attached_ = is_attached;

    int32 flags = 0;
    if (is_attached) {
      flags |= telegram_api::messages_clearRecentStickers::ATTACHED_MASK;
    }

    send_query(
        G()->net_query_creator().create(telegram_api::messages_clearRecentStickers(flags, is_attached /*ignored*/)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_clearRecentStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for clear recent " << (is_attached_ ? "attached " : "") << "stickers: " << result;
    if (!result) {
      td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for clear recent " << (is_attached_ ? "attached " : "") << "stickers: " << status;
    }
    td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    promise_.set_error(std::move(status));
  }
};

class GetFavedStickersQuery : public Td::ResultHandler {
  bool is_repair_ = false;

 public:
  void send(bool is_repair, int32 hash) {
    is_repair_ = is_repair;
    LOG(INFO) << "Send get favorite stickers request with hash = " << hash;
    send_query(G()->net_query_creator().create(telegram_api::messages_getFavedStickers(hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getFavedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td->stickers_manager_->on_get_favorite_stickers(is_repair_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for get favorite stickers: " << status;
    }
    td->stickers_manager_->on_get_favorite_stickers_failed(is_repair_, std::move(status));
  }
};

class FaveStickerQuery : public Td::ResultHandler {
  FileId file_id_;
  string file_reference_;
  bool unsave_ = false;

  Promise<Unit> promise_;

 public:
  explicit FaveStickerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, tl_object_ptr<telegram_api::inputDocument> &&input_document, bool unsave) {
    CHECK(input_document != nullptr);
    CHECK(file_id.is_valid());
    file_id_ = file_id;
    file_reference_ = input_document->file_reference_.as_slice().str();
    unsave_ = unsave;

    send_query(G()->net_query_creator().create(telegram_api::messages_faveSticker(std::move(input_document), unsave)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_faveSticker>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for fave sticker: " << result;
    if (!result) {
      td->stickers_manager_->reload_favorite_stickers(true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!td->auth_manager_->is_bot() && FileReferenceManager::is_file_reference_error(status)) {
      VLOG(file_references) << "Receive " << status << " for " << file_id_;
      td->file_manager_->delete_file_reference(file_id_, file_reference_);
      td->file_reference_manager_->repair_file_reference(
          file_id_, PromiseCreator::lambda([sticker_id = file_id_, unsave = unsave_,
                                            promise = std::move(promise_)](Result<Unit> result) mutable {
            if (result.is_error()) {
              return promise.set_error(Status::Error(400, "Failed to find the sticker"));
            }

            send_closure(G()->stickers_manager(), &StickersManager::send_fave_sticker_query, sticker_id, unsave,
                         std::move(promise));
          }));
      return;
    }

    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for fave sticker: " << status;
    }
    td->stickers_manager_->reload_favorite_stickers(true);
    promise_.set_error(std::move(status));
  }
};

class ReorderStickerSetsQuery : public Td::ResultHandler {
  bool is_masks_;

 public:
  void send(bool is_masks, vector<StickerSetId> sticker_set_ids) {
    is_masks_ = is_masks;
    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::messages_reorderStickerSets::MASKS_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_reorderStickerSets(
        flags, is_masks /*ignored*/, StickersManager::convert_sticker_set_ids(sticker_set_ids))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_reorderStickerSets>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      return on_error(id, Status::Error(400, "Result is false"));
    }
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for ReorderStickerSetsQuery: " << status;
    }
    td->stickers_manager_->reload_installed_sticker_sets(is_masks_, true);
  }
};

class GetStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  StickerSetId sticker_set_id_;
  string sticker_set_name_;

 public:
  explicit GetStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StickerSetId sticker_set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set) {
    sticker_set_id_ = sticker_set_id;
    if (input_sticker_set->get_id() == telegram_api::inputStickerSetShortName::ID) {
      sticker_set_name_ =
          static_cast<const telegram_api::inputStickerSetShortName *>(input_sticker_set.get())->short_name_;
    }
    LOG(INFO) << "Load " << sticker_set_id << " from server: " << to_string(input_sticker_set);
    send_query(G()->net_query_creator().create(telegram_api::messages_getStickerSet(std::move(input_sticker_set))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto set = result_ptr.move_as_ok();

    constexpr int64 GREAT_MINDS_COLOR_SET_ID = 151353307481243663;
    if (set->set_->id_ == GREAT_MINDS_COLOR_SET_ID) {
      string great_minds_name = "TelegramGreatMinds";
      if (sticker_set_id_.get() == StickersManager::GREAT_MINDS_SET_ID ||
          trim(to_lower(sticker_set_name_)) == to_lower(great_minds_name)) {
        set->set_->id_ = StickersManager::GREAT_MINDS_SET_ID;
        set->set_->short_name_ = std::move(great_minds_name);
      }
    }

    td->stickers_manager_->on_get_messages_sticker_set(sticker_set_id_, std::move(set), true, "GetStickerSetQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(INFO) << "Receive error for GetStickerSetQuery: " << status;
    td->stickers_manager_->on_load_sticker_set_fail(sticker_set_id_, status);
    promise_.set_error(std::move(status));
  }
};

class ReloadSpecialStickerSetQuery : public Td::ResultHandler {
  SpecialStickerSetType type_;

 public:
  void send(SpecialStickerSetType type) {
    type_ = std::move(type);
    send_query(G()->net_query_creator().create(telegram_api::messages_getStickerSet(type_.get_input_sticker_set())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto sticker_set_id = td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(),
                                                                             true, "ReloadSpecialStickerSetQuery");
    if (sticker_set_id.is_valid()) {
      td->stickers_manager_->on_get_special_sticker_set(type_, sticker_set_id);
    } else {
      on_error(id, Status::Error(500, "Failed to add special sticker set"));
    }
  }

  void on_error(uint64 id, Status status) override {
    LOG(WARNING) << "Receive error for ReloadSpecialStickerSetQuery: " << status;
    td->stickers_manager_->on_load_special_sticker_set(type_, std::move(status));
  }
};

class SearchStickerSetsQuery : public Td::ResultHandler {
  string query_;

 public:
  void send(string query) {
    query_ = std::move(query);
    send_query(
        G()->net_query_creator().create(telegram_api::messages_searchStickerSets(0, false /*ignored*/, query_, 0)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_searchStickerSets>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for search sticker sets: " << to_string(ptr);
    td->stickers_manager_->on_find_sticker_sets_success(query_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for search sticker sets: " << status;
    }
    td->stickers_manager_->on_find_sticker_sets_fail(query_, std::move(status));
  }
};

class InstallStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  StickerSetId set_id_;
  bool is_archived_;

 public:
  explicit InstallStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StickerSetId set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_set, bool is_archived) {
    set_id_ = set_id;
    is_archived_ = is_archived;
    send_query(
        G()->net_query_creator().create(telegram_api::messages_installStickerSet(std::move(input_set), is_archived)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_installStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_install_sticker_set(set_id_, is_archived_, result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class UninstallStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  StickerSetId set_id_;

 public:
  explicit UninstallStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StickerSetId set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_set) {
    set_id_ = set_id;
    send_query(G()->net_query_creator().create(telegram_api::messages_uninstallStickerSet(std::move(input_set))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_uninstallStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      LOG(WARNING) << "Receive false in result to uninstallStickerSet";
    } else {
      td->stickers_manager_->on_uninstall_sticker_set(set_id_);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class ReadFeaturedStickerSetsQuery : public Td::ResultHandler {
 public:
  void send(vector<StickerSetId> sticker_set_ids) {
    LOG(INFO) << "Read trending sticker sets " << format::as_array(sticker_set_ids);
    send_query(G()->net_query_creator().create(
        telegram_api::messages_readFeaturedStickers(StickersManager::convert_sticker_set_ids(sticker_set_ids))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_readFeaturedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    (void)result;
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for ReadFeaturedStickerSetsQuery: " << status;
    }
    td->stickers_manager_->reload_featured_sticker_sets(true);
  }
};

class UploadStickerFileQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;
  bool was_uploaded_ = false;

 public:
  explicit UploadStickerFileQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputPeer> &&input_peer, FileId file_id,
            tl_object_ptr<telegram_api::InputMedia> &&input_media) {
    CHECK(input_peer != nullptr);
    CHECK(input_media != nullptr);
    file_id_ = file_id;
    was_uploaded_ = FileManager::extract_was_uploaded(input_media);
    send_query(G()->net_query_creator().create(
        telegram_api::messages_uploadMedia(std::move(input_peer), std::move(input_media))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_uploadMedia>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_uploaded_sticker_file(file_id_, result_ptr.move_as_ok(), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    if (was_uploaded_) {
      CHECK(file_id_.is_valid());
      if (begins_with(status.message(), "FILE_PART_") && ends_with(status.message(), "_MISSING")) {
        // TODO td->stickers_manager_->on_upload_sticker_file_part_missing(file_id_, to_integer<int32>(status.message().substr(10)));
        // return;
      } else {
        if (status.code() != 429 && status.code() < 500 && !G()->close_flag()) {
          td->file_manager_->delete_partial_remote_location(file_id_);
        }
      }
    } else if (FileReferenceManager::is_file_reference_error(status)) {
      LOG(ERROR) << "Receive file reference error for UploadStickerFileQuery";
    }
    td->file_manager_->cancel_upload(file_id_);
    promise_.set_error(std::move(status));
  }
};

class CreateNewStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CreateNewStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> &&input_user, const string &title, const string &short_name,
            bool is_masks, bool is_animated,
            vector<tl_object_ptr<telegram_api::inputStickerSetItem>> &&input_stickers) {
    CHECK(input_user != nullptr);

    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::stickers_createStickerSet::MASKS_MASK;
    }
    if (is_animated) {
      flags |= telegram_api::stickers_createStickerSet::ANIMATED_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::stickers_createStickerSet(flags, false /*ignored*/, false /*ignored*/, std::move(input_user),
                                                title, short_name, nullptr, std::move(input_stickers))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_createStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(), true,
                                                       "CreateNewStickerSetQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class AddStickerToSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AddStickerToSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &short_name, tl_object_ptr<telegram_api::inputStickerSetItem> &&input_sticker) {
    send_query(G()->net_query_creator().create(telegram_api::stickers_addStickerToSet(
        make_tl_object<telegram_api::inputStickerSetShortName>(short_name), std::move(input_sticker))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_addStickerToSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(), true,
                                                       "AddStickerToSetQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class SetStickerSetThumbnailQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetStickerSetThumbnailQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &short_name, tl_object_ptr<telegram_api::InputDocument> &&input_document) {
    send_query(G()->net_query_creator().create(telegram_api::stickers_setStickerSetThumb(
        make_tl_object<telegram_api::inputStickerSetShortName>(short_name), std::move(input_document))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_setStickerSetThumb>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(), true,
                                                       "SetStickerSetThumbnailQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class SetStickerPositionQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetStickerPositionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::inputDocument> &&input_document, int32 position) {
    send_query(G()->net_query_creator().create(
        telegram_api::stickers_changeStickerPosition(std::move(input_document), position)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_changeStickerPosition>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(), true,
                                                       "SetStickerPositionQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class DeleteStickerFromSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteStickerFromSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::inputDocument> &&input_document) {
    send_query(G()->net_query_creator().create(telegram_api::stickers_removeStickerFromSet(std::move(input_document))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_removeStickerFromSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(StickerSetId(), result_ptr.move_as_ok(), true,
                                                       "DeleteStickerFromSetQuery");

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    CHECK(status.is_error());
    promise_.set_error(std::move(status));
  }
};

class StickersManager::StickerListLogEvent {
 public:
  vector<FileId> sticker_ids;

  StickerListLogEvent() = default;

  explicit StickerListLogEvent(vector<FileId> sticker_ids) : sticker_ids(std::move(sticker_ids)) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    StickersManager *stickers_manager = storer.context()->td().get_actor_unsafe()->stickers_manager_.get();
    td::store(narrow_cast<int32>(sticker_ids.size()), storer);
    for (auto sticker_id : sticker_ids) {
      stickers_manager->store_sticker(sticker_id, false, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    StickersManager *stickers_manager = parser.context()->td().get_actor_unsafe()->stickers_manager_.get();
    int32 size = parser.fetch_int();
    sticker_ids.resize(size);
    for (auto &sticker_id : sticker_ids) {
      sticker_id = stickers_manager->parse_sticker(false, parser);
    }
  }
};

class StickersManager::StickerSetListLogEvent {
 public:
  vector<StickerSetId> sticker_set_ids;

  StickerSetListLogEvent() = default;

  explicit StickerSetListLogEvent(vector<StickerSetId> sticker_set_ids) : sticker_set_ids(std::move(sticker_set_ids)) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(sticker_set_ids, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(sticker_set_ids, parser);
  }
};

class StickersManager::UploadStickerFileCallback : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override {
    send_closure_later(G()->stickers_manager(), &StickersManager::on_upload_sticker_file, file_id,
                       std::move(input_file));
  }

  void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override {
    UNREACHABLE();
  }

  void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override {
    UNREACHABLE();
  }

  void on_upload_error(FileId file_id, Status error) override {
    send_closure_later(G()->stickers_manager(), &StickersManager::on_upload_sticker_file_error, file_id,
                       std::move(error));
  }
};

StickersManager::StickersManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_sticker_file_callback_ = std::make_shared<UploadStickerFileCallback>();

  on_update_recent_stickers_limit(
      narrow_cast<int32>(G()->shared_config().get_option_integer("recent_stickers_limit", 200)));
  on_update_favorite_stickers_limit(
      narrow_cast<int32>(G()->shared_config().get_option_integer("favorite_stickers_limit", 5)));
  on_update_dice_emojis();
}

void StickersManager::start_up() {
  init();
}

void StickersManager::init() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot() || G()->close_flag()) {
    return;
  }
  LOG(INFO) << "Init StickersManager";
  is_inited_ = true;

  {
    // add animated emoji sticker set
    auto &animated_emoji_sticker_set = add_special_sticker_set(SpecialStickerSetType::animated_emoji());
    if (G()->is_test_dc()) {
      init_special_sticker_set(animated_emoji_sticker_set, 1258816259751954, 4879754868529595811, "emojies");
    } else {
      init_special_sticker_set(animated_emoji_sticker_set, 1258816259751983, 5100237018658464041, "AnimatedEmojies");
    }
    load_special_sticker_set_info_from_binlog(animated_emoji_sticker_set);
    G()->shared_config().set_option_string(PSLICE() << animated_emoji_sticker_set.type_.type_ << "_name",
                                           animated_emoji_sticker_set.short_name_);
  }

  dice_emojis_str_ =
      G()->shared_config().get_option_string("dice_emojis", "🎲\x01🎯\x01🏀\x01⚽\x01⚽️\x01🎰\x01🎳");
  dice_emojis_ = full_split(dice_emojis_str_, '\x01');
  for (auto &dice_emoji : dice_emojis_) {
    auto &animated_dice_sticker_set = add_special_sticker_set(SpecialStickerSetType::animated_dice(dice_emoji));
    load_special_sticker_set_info_from_binlog(animated_dice_sticker_set);
  }
  send_closure(G()->td(), &Td::send_update, get_update_dice_emojis_object());

  on_update_dice_success_values();

  if (G()->parameters().use_file_db) {
    auto old_featured_sticker_set_count_str = G()->td_db()->get_binlog_pmc()->get("old_featured_sticker_set_count");
    if (!old_featured_sticker_set_count_str.empty()) {
      old_featured_sticker_set_count_ = to_integer<int32>(old_featured_sticker_set_count_str);
    }
    if (!G()->td_db()->get_binlog_pmc()->get("invalidate_old_featured_sticker_sets").empty()) {
      invalidate_old_featured_sticker_sets();
    }
  } else {
    G()->td_db()->get_binlog_pmc()->erase("old_featured_sticker_set_count");
    G()->td_db()->get_binlog_pmc()->erase("invalidate_old_featured_sticker_sets");
  }

  G()->td_db()->get_binlog_pmc()->erase("animated_dice_sticker_set");       // legacy
  G()->shared_config().set_option_empty("animated_dice_sticker_set_name");  // legacy
}

StickersManager::SpecialStickerSet &StickersManager::add_special_sticker_set(const string &type) {
  auto &result = special_sticker_sets_[type];
  if (result.type_.type_.empty()) {
    result.type_.type_ = type;
  } else {
    CHECK(result.type_.type_ == type);
  }
  return result;
}

void StickersManager::init_special_sticker_set(SpecialStickerSet &sticker_set, int64 sticker_set_id, int64 access_hash,
                                               string name) {
  sticker_set.id_ = StickerSetId(sticker_set_id);
  sticker_set.access_hash_ = access_hash;
  sticker_set.short_name_ = std::move(name);
}

void StickersManager::load_special_sticker_set_info_from_binlog(SpecialStickerSet &sticker_set) {
  if (G()->parameters().use_file_db) {
    string sticker_set_string = G()->td_db()->get_binlog_pmc()->get(sticker_set.type_.type_);
    if (!sticker_set_string.empty()) {
      auto parts = full_split(sticker_set_string);
      if (parts.size() != 3) {
        LOG(ERROR) << "Can't parse " << sticker_set_string;
      } else {
        auto r_sticker_set_id = to_integer_safe<int64>(parts[0]);
        auto r_sticker_set_access_hash = to_integer_safe<int64>(parts[1]);
        auto sticker_set_name = parts[2];
        if (r_sticker_set_id.is_error() || r_sticker_set_access_hash.is_error() ||
            clean_username(sticker_set_name) != sticker_set_name || sticker_set_name.empty()) {
          LOG(ERROR) << "Can't parse " << sticker_set_string;
        } else {
          init_special_sticker_set(sticker_set, r_sticker_set_id.ok(), r_sticker_set_access_hash.ok(),
                                   std::move(sticker_set_name));
        }
      }
    }
  } else {
    G()->td_db()->get_binlog_pmc()->erase(sticker_set.type_.type_);
  }

  if (!sticker_set.id_.is_valid()) {
    return;
  }

  add_sticker_set(sticker_set.id_, sticker_set.access_hash_);
  short_name_to_sticker_set_id_.emplace(sticker_set.short_name_, sticker_set.id_);
}

void StickersManager::load_special_sticker_set_by_type(const SpecialStickerSetType &type) {
  if (G()->close_flag()) {
    return;
  }

  auto &sticker_set = add_special_sticker_set(type.type_);
  CHECK(sticker_set.is_being_loaded_);
  sticker_set.is_being_loaded_ = false;
  load_special_sticker_set(sticker_set);
}

void StickersManager::load_special_sticker_set(SpecialStickerSet &sticker_set) {
  CHECK(!sticker_set.is_being_loaded_);
  sticker_set.is_being_loaded_ = true;
  if (sticker_set.id_.is_valid()) {
    auto promise = PromiseCreator::lambda([actor_id = actor_id(this), type = sticker_set.type_](Result<Unit> &&result) {
      send_closure(actor_id, &StickersManager::on_load_special_sticker_set, type,
                   result.is_ok() ? Status::OK() : result.move_as_error());
    });
    load_sticker_sets({sticker_set.id_}, std::move(promise));
  } else {
    reload_special_sticker_set(sticker_set);
  }
}

void StickersManager::reload_special_sticker_set(SpecialStickerSet &sticker_set) {
  td_->create_handler<ReloadSpecialStickerSetQuery>()->send(sticker_set.type_);
}

void StickersManager::on_load_special_sticker_set(const SpecialStickerSetType &type, Status result) {
  if (G()->close_flag()) {
    return;
  }

  auto &special_sticker_set = add_special_sticker_set(type.type_);
  if (!special_sticker_set.is_being_loaded_) {
    return;
  }

  if (result.is_error()) {
    // failed to load the special sticker set; repeat after some time
    create_actor<SleepActor>("RetryLoadSpecialStickerSetActor", Random::fast(300, 600),
                             PromiseCreator::lambda([actor_id = actor_id(this), type](Result<Unit> result) {
                               send_closure(actor_id, &StickersManager::load_special_sticker_set_by_type, type);
                             }))
        .release();
    return;
  }

  special_sticker_set.is_being_loaded_ = false;

  auto emoji = type.get_dice_emoji();
  CHECK(!emoji.empty());

  CHECK(special_sticker_set.id_.is_valid());
  auto sticker_set = get_sticker_set(special_sticker_set.id_);
  CHECK(sticker_set != nullptr);
  CHECK(sticker_set->was_loaded);

  auto it = dice_messages_.find(emoji);
  if (it == dice_messages_.end()) {
    return;
  }

  vector<FullMessageId> full_message_ids;
  for (auto full_message_id : it->second) {
    full_message_ids.push_back(full_message_id);
  }
  CHECK(!full_message_ids.empty());
  for (auto full_message_id : full_message_ids) {
    td_->messages_manager_->on_external_update_message_content(full_message_id);
  }
}

void StickersManager::tear_down() {
  parent_.reset();
}

tl_object_ptr<td_api::MaskPoint> StickersManager::get_mask_point_object(int32 point) {
  switch (point) {
    case 0:
      return td_api::make_object<td_api::maskPointForehead>();
    case 1:
      return td_api::make_object<td_api::maskPointEyes>();
    case 2:
      return td_api::make_object<td_api::maskPointMouth>();
    case 3:
      return td_api::make_object<td_api::maskPointChin>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<td_api::object_ptr<td_api::closedVectorPath>> StickersManager::get_sticker_minithumbnail(
    CSlice path, StickerSetId sticker_set_id, int64 document_id) {
  if (path.empty()) {
    return {};
  }

  auto buf = StackAllocator::alloc(1 << 9);
  StringBuilder sb(buf.as_slice(), true);

  sb << 'M';
  for (unsigned char c : path) {
    if (c >= 128 + 64) {
      sb << "AACAAAAHAAALMAAAQASTAVAAAZaacaaaahaaalmaaaqastava.az0123456789-,"[c - 128 - 64];
    } else {
      if (c >= 128) {
        sb << ',';
      } else if (c >= 64) {
        sb << '-';
      }
      sb << (c & 63);
    }
  }
  sb << 'z';

  CHECK(!sb.is_error());
  path = sb.as_cslice();
  LOG(DEBUG) << "Transform SVG path " << path;

  size_t pos = 0;
  auto skip_commas = [&path, &pos] {
    while (path[pos] == ',') {
      pos++;
    }
  };
  auto get_number = [&] {
    skip_commas();
    int sign = 1;
    if (path[pos] == '-') {
      sign = -1;
      pos++;
    }
    double res = 0;
    while (is_digit(path[pos])) {
      res = res * 10 + path[pos++] - '0';
    }
    if (path[pos] == '.') {
      pos++;
      double mul = 0.1;
      while (is_digit(path[pos])) {
        res += (path[pos] - '0') * mul;
        mul *= 0.1;
        pos++;
      }
    }
    return sign * res;
  };
  auto make_point = [](double x, double y) {
    return td_api::make_object<td_api::point>(x, y);
  };

  vector<td_api::object_ptr<td_api::closedVectorPath>> result;
  double x = 0;
  double y = 0;
  while (path[pos] != '\0') {
    skip_commas();
    if (path[pos] == '\0') {
      break;
    }

    while (path[pos] == 'm' || path[pos] == 'M') {
      auto command = path[pos++];
      do {
        if (command == 'm') {
          x += get_number();
          y += get_number();
        } else {
          x = get_number();
          y = get_number();
        }
        skip_commas();
      } while (path[pos] != '\0' && !is_alpha(path[pos]));
    }

    double start_x = x;
    double start_y = y;

    vector<td_api::object_ptr<td_api::VectorPathCommand>> commands;
    bool have_last_end_control_point = false;
    double last_end_control_point_x = 0;
    double last_end_control_point_y = 0;
    bool is_closed = false;
    char command = '-';
    while (!is_closed) {
      skip_commas();
      if (path[pos] == '\0') {
        LOG(ERROR) << "Receive unclosed path " << path << " in a sticker " << document_id << " from " << sticker_set_id;
        return {};
      }
      if (is_alpha(path[pos])) {
        command = path[pos++];
      }
      switch (command) {
        case 'l':
        case 'L':
        case 'h':
        case 'H':
        case 'v':
        case 'V':
          if (command == 'l' || command == 'h') {
            x += get_number();
          } else if (command == 'L' || command == 'H') {
            x = get_number();
          }
          if (command == 'l' || command == 'v') {
            y += get_number();
          } else if (command == 'L' || command == 'V') {
            y = get_number();
          }
          commands.push_back(td_api::make_object<td_api::vectorPathCommandLine>(make_point(x, y)));
          have_last_end_control_point = false;
          break;
        case 'C':
        case 'c':
        case 'S':
        case 's': {
          double start_control_point_x;
          double start_control_point_y;
          if (command == 'S' || command == 's') {
            if (have_last_end_control_point) {
              start_control_point_x = 2 * x - last_end_control_point_x;
              start_control_point_y = 2 * y - last_end_control_point_y;
            } else {
              start_control_point_x = x;
              start_control_point_y = y;
            }
          } else {
            start_control_point_x = get_number();
            start_control_point_y = get_number();
            if (command == 'c') {
              start_control_point_x += x;
              start_control_point_y += y;
            }
          }

          last_end_control_point_x = get_number();
          last_end_control_point_y = get_number();
          if (command == 'c' || command == 's') {
            last_end_control_point_x += x;
            last_end_control_point_y += y;
          }
          have_last_end_control_point = true;

          if (command == 'c' || command == 's') {
            x += get_number();
            y += get_number();
          } else {
            x = get_number();
            y = get_number();
          }

          commands.push_back(td_api::make_object<td_api::vectorPathCommandCubicBezierCurve>(
              make_point(start_control_point_x, start_control_point_y),
              make_point(last_end_control_point_x, last_end_control_point_y), make_point(x, y)));
          break;
        }
        case 'm':
        case 'M':
          pos--;
          // fallthrough
        case 'z':
        case 'Z':
          if (x != start_x || y != start_y) {
            x = start_x;
            y = start_y;
            commands.push_back(td_api::make_object<td_api::vectorPathCommandLine>(make_point(x, y)));
          }
          if (!commands.empty()) {
            result.push_back(td_api::make_object<td_api::closedVectorPath>(std::move(commands)));
          }
          is_closed = true;
          break;
        default:
          LOG(ERROR) << "Receive invalid command " << command << " at pos " << pos << " in a sticker " << document_id
                     << " from " << sticker_set_id << ": " << path;
          return {};
      }
    }
  }
  /*
  string svg;
  for (const auto &vector_path : result) {
    CHECK(!vector_path->commands_.empty());
    svg += 'M';
    auto add_point = [&](const td_api::object_ptr<td_api::point> &p) {
      svg += to_string(static_cast<int>(p->x_));
      svg += ',';
      svg += to_string(static_cast<int>(p->y_));
      svg += ',';
    };
    auto last_command = vector_path->commands_.back().get();
    switch (last_command->get_id()) {
      case td_api::vectorPathCommandLine::ID:
        add_point(static_cast<const td_api::vectorPathCommandLine *>(last_command)->end_point_);
        break;
      case td_api::vectorPathCommandCubicBezierCurve::ID:
        add_point(static_cast<const td_api::vectorPathCommandCubicBezierCurve *>(last_command)->end_point_);
        break;
      default:
        UNREACHABLE();
    }
    for (auto &command : vector_path->commands_) {
      switch (command->get_id()) {
        case td_api::vectorPathCommandLine::ID: {
          auto line = static_cast<const td_api::vectorPathCommandLine *>(command.get());
          svg += 'L';
          add_point(line->end_point_);
          break;
        }
        case td_api::vectorPathCommandCubicBezierCurve::ID: {
          auto curve = static_cast<const td_api::vectorPathCommandCubicBezierCurve *>(command.get());
          svg += 'C';
          add_point(curve->start_control_point_);
          add_point(curve->end_control_point_);
          add_point(curve->end_point_);
          break;
        }
        default:
          UNREACHABLE();
      }
    }
    svg += 'z';
  }
  */

  return result;
}

tl_object_ptr<td_api::sticker> StickersManager::get_sticker_object(FileId file_id) const {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto it = stickers_.find(file_id);
  CHECK(it != stickers_.end());
  auto sticker = it->second.get();
  CHECK(sticker != nullptr);
  sticker->is_changed = false;

  auto mask_position = sticker->point >= 0
                           ? make_tl_object<td_api::maskPosition>(get_mask_point_object(sticker->point),
                                                                  sticker->x_shift, sticker->y_shift, sticker->scale)
                           : nullptr;

  const PhotoSize &thumbnail = sticker->m_thumbnail.file_id.is_valid() ? sticker->m_thumbnail : sticker->s_thumbnail;
  auto thumbnail_format = PhotoFormat::Webp;
  int64 document_id = -1;
  if (!sticker->set_id.is_valid()) {
    auto sticker_file_view = td_->file_manager_->get_file_view(sticker->file_id);
    if (sticker_file_view.is_encrypted()) {
      // uploaded to secret chats stickers have JPEG thumbnail instead of server-generated WEBP
      thumbnail_format = PhotoFormat::Jpeg;
    } else {
      if (sticker_file_view.has_remote_location() && sticker_file_view.remote_location().is_document()) {
        document_id = sticker_file_view.remote_location().get_id();
      }

      if (thumbnail.file_id.is_valid()) {
        auto thumbnail_file_view = td_->file_manager_->get_file_view(thumbnail.file_id);
        if (ends_with(thumbnail_file_view.suggested_path(), ".jpg")) {
          thumbnail_format = PhotoFormat::Jpeg;
        }
      }
    }
  }
  auto thumbnail_object = get_thumbnail_object(td_->file_manager_.get(), thumbnail, thumbnail_format);
  return make_tl_object<td_api::sticker>(
      sticker->set_id.get(), sticker->dimensions.width, sticker->dimensions.height, sticker->alt, sticker->is_animated,
      sticker->is_mask, std::move(mask_position),
      get_sticker_minithumbnail(sticker->minithumbnail, sticker->set_id, document_id), std::move(thumbnail_object),
      td_->file_manager_->get_file_object(file_id));
}

tl_object_ptr<td_api::stickers> StickersManager::get_stickers_object(const vector<FileId> &sticker_ids) const {
  auto result = make_tl_object<td_api::stickers>();
  result->stickers_.reserve(sticker_ids.size());
  for (auto sticker_id : sticker_ids) {
    result->stickers_.push_back(get_sticker_object(sticker_id));
  }
  return result;
}

tl_object_ptr<td_api::DiceStickers> StickersManager::get_dice_stickers_object(const string &emoji, int32 value) const {
  if (td_->auth_manager_->is_bot()) {
    return nullptr;
  }
  if (!td::contains(dice_emojis_, emoji)) {
    return nullptr;
  }

  auto it = special_sticker_sets_.find(SpecialStickerSetType::animated_dice(emoji));
  if (it == special_sticker_sets_.end()) {
    return nullptr;
  }

  auto sticker_set_id = it->second.id_;
  if (!sticker_set_id.is_valid()) {
    return nullptr;
  }

  auto sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  if (!sticker_set->was_loaded) {
    return nullptr;
  }

  auto get_sticker = [&](int32 value) {
    return get_sticker_object(sticker_set->sticker_ids[value]);
  };

  if (emoji == "🎰") {
    if (sticker_set->sticker_ids.size() < 21 || value < 0 || value > 64) {
      return nullptr;
    }

    int32 background_id = value == 1 || value == 22 || value == 43 || value == 64 ? 1 : 0;
    int32 lever_id = 2;
    int32 left_reel_id = value == 64 ? 3 : 8;
    int32 center_reel_id = value == 64 ? 9 : 14;
    int32 right_reel_id = value == 64 ? 15 : 20;
    if (value != 0 && value != 64) {
      left_reel_id = 4 + (value % 4);
      center_reel_id = 10 + ((value + 3) / 4 % 4);
      right_reel_id = 16 + ((value + 15) / 16 % 4);
    }
    return td_api::make_object<td_api::diceStickersSlotMachine>(get_sticker(background_id), get_sticker(lever_id),
                                                                get_sticker(left_reel_id), get_sticker(center_reel_id),
                                                                get_sticker(right_reel_id));
  }

  if (value >= 0 && value < static_cast<int32>(sticker_set->sticker_ids.size())) {
    return td_api::make_object<td_api::diceStickersRegular>(get_sticker(value));
  }
  return nullptr;
}

int32 StickersManager::get_dice_success_animation_frame_number(const string &emoji, int32 value) const {
  if (td_->auth_manager_->is_bot()) {
    return std::numeric_limits<int32>::max();
  }
  if (value == 0 || !td::contains(dice_emojis_, emoji)) {
    return std::numeric_limits<int32>::max();
  }
  auto pos = static_cast<size_t>(std::find(dice_emojis_.begin(), dice_emojis_.end(), emoji) - dice_emojis_.begin());
  if (pos >= dice_success_values_.size()) {
    return std::numeric_limits<int32>::max();
  }

  auto &result = dice_success_values_[pos];
  return result.first == value ? result.second : std::numeric_limits<int32>::max();
}

tl_object_ptr<td_api::stickerSet> StickersManager::get_sticker_set_object(StickerSetId sticker_set_id) const {
  const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  CHECK(sticker_set->was_loaded);
  sticker_set->was_update_sent = true;

  std::vector<tl_object_ptr<td_api::sticker>> stickers;
  std::vector<tl_object_ptr<td_api::emojis>> emojis;
  for (auto sticker_id : sticker_set->sticker_ids) {
    stickers.push_back(get_sticker_object(sticker_id));

    auto it = sticker_set->sticker_emojis_map_.find(sticker_id);
    if (it == sticker_set->sticker_emojis_map_.end()) {
      emojis.push_back(Auto());
    } else {
      emojis.push_back(make_tl_object<td_api::emojis>(vector<string>(it->second)));
    }
  }
  auto thumbnail = get_thumbnail_object(td_->file_manager_.get(), sticker_set->thumbnail,
                                        sticker_set->is_animated ? PhotoFormat::Tgs : PhotoFormat::Webp);
  return make_tl_object<td_api::stickerSet>(
      sticker_set->id.get(), sticker_set->title, sticker_set->short_name, std::move(thumbnail),
      get_sticker_minithumbnail(sticker_set->minithumbnail, sticker_set->id, -2),
      sticker_set->is_installed && !sticker_set->is_archived, sticker_set->is_archived, sticker_set->is_official,
      sticker_set->is_animated, sticker_set->is_masks, sticker_set->is_viewed, std::move(stickers), std::move(emojis));
}

tl_object_ptr<td_api::stickerSets> StickersManager::get_sticker_sets_object(int32 total_count,
                                                                            const vector<StickerSetId> &sticker_set_ids,
                                                                            size_t covers_limit) const {
  vector<tl_object_ptr<td_api::stickerSetInfo>> result;
  result.reserve(sticker_set_ids.size());
  for (auto sticker_set_id : sticker_set_ids) {
    auto sticker_set_info = get_sticker_set_info_object(sticker_set_id, covers_limit);
    if (sticker_set_info->size_ != 0) {
      result.push_back(std::move(sticker_set_info));
    }
  }

  auto result_size = narrow_cast<int32>(result.size());
  if (total_count < result_size) {
    if (total_count != -1) {
      LOG(ERROR) << "Have total_count = " << total_count << ", but there are " << result_size << " results";
    }
    total_count = result_size;
  }
  return make_tl_object<td_api::stickerSets>(total_count, std::move(result));
}

tl_object_ptr<td_api::stickerSetInfo> StickersManager::get_sticker_set_info_object(StickerSetId sticker_set_id,
                                                                                   size_t covers_limit) const {
  const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  CHECK(sticker_set->is_inited);
  sticker_set->was_update_sent = true;

  std::vector<tl_object_ptr<td_api::sticker>> stickers;
  for (auto sticker_id : sticker_set->sticker_ids) {
    stickers.push_back(get_sticker_object(sticker_id));
    if (stickers.size() >= covers_limit) {
      break;
    }
  }

  auto thumbnail = get_thumbnail_object(td_->file_manager_.get(), sticker_set->thumbnail,
                                        sticker_set->is_animated ? PhotoFormat::Tgs : PhotoFormat::Webp);
  return make_tl_object<td_api::stickerSetInfo>(
      sticker_set->id.get(), sticker_set->title, sticker_set->short_name, std::move(thumbnail),
      get_sticker_minithumbnail(sticker_set->minithumbnail, sticker_set->id, -3),
      sticker_set->is_installed && !sticker_set->is_archived, sticker_set->is_archived, sticker_set->is_official,
      sticker_set->is_animated, sticker_set->is_masks, sticker_set->is_viewed,
      sticker_set->was_loaded ? narrow_cast<int32>(sticker_set->sticker_ids.size()) : sticker_set->sticker_count,
      std::move(stickers));
}

tl_object_ptr<telegram_api::InputStickerSet> StickersManager::get_input_sticker_set(StickerSetId sticker_set_id) const {
  auto sticker_set = get_sticker_set(sticker_set_id);
  if (sticker_set == nullptr) {
    return nullptr;
  }

  return get_input_sticker_set(sticker_set);
}

FileId StickersManager::on_get_sticker(unique_ptr<Sticker> new_sticker, bool replace) {
  auto file_id = new_sticker->file_id;
  CHECK(file_id.is_valid());
  LOG(INFO) << "Receive sticker " << file_id;
  auto &s = stickers_[file_id];
  if (s == nullptr) {
    s = std::move(new_sticker);
  } else if (replace) {
    CHECK(s->file_id == file_id);
    if (s->dimensions != new_sticker->dimensions && new_sticker->dimensions.width != 0) {
      LOG(DEBUG) << "Sticker " << file_id << " dimensions has changed";
      s->dimensions = new_sticker->dimensions;
      s->is_changed = true;
    }
    if (s->set_id != new_sticker->set_id && new_sticker->set_id.is_valid()) {
      LOG_IF(ERROR, s->set_id.is_valid()) << "Sticker " << file_id << " set_id has changed";
      s->set_id = new_sticker->set_id;
      s->is_changed = true;
    }
    if (s->alt != new_sticker->alt && !new_sticker->alt.empty()) {
      LOG(DEBUG) << "Sticker " << file_id << " emoji has changed";
      s->alt = std::move(new_sticker->alt);
      s->is_changed = true;
    }
    if (s->minithumbnail != new_sticker->minithumbnail) {
      LOG(DEBUG) << "Sticker " << file_id << " minithumbnail has changed";
      s->minithumbnail = std::move(new_sticker->minithumbnail);
      s->is_changed = true;
    }
    if (s->s_thumbnail != new_sticker->s_thumbnail && new_sticker->s_thumbnail.file_id.is_valid()) {
      LOG_IF(INFO, s->s_thumbnail.file_id.is_valid()) << "Sticker " << file_id << " s thumbnail has changed from "
                                                      << s->s_thumbnail << " to " << new_sticker->s_thumbnail;
      s->s_thumbnail = std::move(new_sticker->s_thumbnail);
      s->is_changed = true;
    }
    if (s->m_thumbnail != new_sticker->m_thumbnail && new_sticker->m_thumbnail.file_id.is_valid()) {
      LOG_IF(INFO, s->m_thumbnail.file_id.is_valid()) << "Sticker " << file_id << " m thumbnail has changed from "
                                                      << s->m_thumbnail << " to " << new_sticker->m_thumbnail;
      s->m_thumbnail = std::move(new_sticker->m_thumbnail);
      s->is_changed = true;
    }
    if (s->is_animated != new_sticker->is_animated && new_sticker->is_animated) {
      s->is_animated = new_sticker->is_animated;
      s->is_changed = true;
    }
    if (s->is_mask != new_sticker->is_mask && new_sticker->is_mask) {
      s->is_mask = new_sticker->is_mask;
      s->is_changed = true;
    }
    if (s->point != new_sticker->point && new_sticker->point != -1) {
      s->point = new_sticker->point;
      s->x_shift = new_sticker->x_shift;
      s->y_shift = new_sticker->y_shift;
      s->scale = new_sticker->scale;
      s->is_changed = true;
    }
  }

  return file_id;
}

bool StickersManager::has_webp_thumbnail(const vector<tl_object_ptr<telegram_api::PhotoSize>> &thumbnails) {
  // server tries to always replace user-provided thumbnail with server-side WEBP thumbnail
  // but there can be some old sticker documents or some big stickers
  for (auto &size : thumbnails) {
    switch (size->get_id()) {
      case telegram_api::photoStrippedSize::ID:
      case telegram_api::photoSizeProgressive::ID:
        // WEBP thumbnail can't have stripped size or be progressive
        return false;
      default:
        break;
    }
  }
  return true;
}

std::pair<int64, FileId> StickersManager::on_get_sticker_document(
    tl_object_ptr<telegram_api::Document> &&document_ptr) {
  int32 document_constructor_id = document_ptr->get_id();
  if (document_constructor_id == telegram_api::documentEmpty::ID) {
    LOG(ERROR) << "Empty sticker document received";
    return {};
  }
  CHECK(document_constructor_id == telegram_api::document::ID);
  auto document = move_tl_object_as<telegram_api::document>(document_ptr);

  if (!DcId::is_valid(document->dc_id_)) {
    LOG(ERROR) << "Wrong dc_id = " << document->dc_id_ << " in document " << to_string(document);
    return {};
  }
  auto dc_id = DcId::internal(document->dc_id_);

  Dimensions dimensions;
  tl_object_ptr<telegram_api::documentAttributeSticker> sticker;
  for (auto &attribute : document->attributes_) {
    switch (attribute->get_id()) {
      case telegram_api::documentAttributeImageSize::ID: {
        auto image_size = move_tl_object_as<telegram_api::documentAttributeImageSize>(attribute);
        dimensions = get_dimensions(image_size->w_, image_size->h_, "sticker documentAttributeImageSize");
        break;
      }
      case telegram_api::documentAttributeSticker::ID:
        sticker = move_tl_object_as<telegram_api::documentAttributeSticker>(attribute);
        break;
      default:
        continue;
    }
  }
  if (sticker == nullptr) {
    if (document->mime_type_ != "application/x-bad-tgsticker") {
      LOG(ERROR) << "Have no attributeSticker in sticker " << to_string(document);
    }
    return {};
  }

  bool is_animated = document->mime_type_ == "application/x-tgsticker";
  int64 document_id = document->id_;
  FileId sticker_id =
      td_->file_manager_->register_remote(FullRemoteFileLocation(FileType::Sticker, document_id, document->access_hash_,
                                                                 dc_id, document->file_reference_.as_slice().str()),
                                          FileLocationSource::FromServer, DialogId(), document->size_, 0,
                                          PSTRING() << document_id << (is_animated ? ".tgs" : ".webp"));

  PhotoSize thumbnail;
  string minithumbnail;
  auto thumbnail_format = has_webp_thumbnail(document->thumbs_) ? PhotoFormat::Webp : PhotoFormat::Jpeg;
  for (auto &thumb : document->thumbs_) {
    auto photo_size = get_photo_size(td_->file_manager_.get(), {FileType::Thumbnail, 0}, document_id,
                                     document->access_hash_, document->file_reference_.as_slice().str(), dc_id,
                                     DialogId(), std::move(thumb), thumbnail_format);
    if (photo_size.get_offset() == 0) {
      if (!thumbnail.file_id.is_valid()) {
        thumbnail = std::move(photo_size.get<0>());
      }
      break;
    } else {
      if (thumbnail_format == PhotoFormat::Webp) {
        minithumbnail = std::move(photo_size.get<1>());
      }
    }
  }

  create_sticker(sticker_id, std::move(minithumbnail), std::move(thumbnail), dimensions, std::move(sticker),
                 is_animated, nullptr);
  return {document_id, sticker_id};
}

StickersManager::Sticker *StickersManager::get_sticker(FileId file_id) {
  auto sticker = stickers_.find(file_id);
  if (sticker == stickers_.end()) {
    return nullptr;
  }

  CHECK(sticker->second->file_id == file_id);
  return sticker->second.get();
}

const StickersManager::Sticker *StickersManager::get_sticker(FileId file_id) const {
  auto sticker = stickers_.find(file_id);
  if (sticker == stickers_.end()) {
    return nullptr;
  }

  CHECK(sticker->second->file_id == file_id);
  return sticker->second.get();
}

StickersManager::StickerSet *StickersManager::get_sticker_set(StickerSetId sticker_set_id) {
  auto sticker_set = sticker_sets_.find(sticker_set_id);
  if (sticker_set == sticker_sets_.end()) {
    return nullptr;
  }

  return sticker_set->second.get();
}

const StickersManager::StickerSet *StickersManager::get_sticker_set(StickerSetId sticker_set_id) const {
  auto sticker_set = sticker_sets_.find(sticker_set_id);
  if (sticker_set == sticker_sets_.end()) {
    return nullptr;
  }

  return sticker_set->second.get();
}

StickerSetId StickersManager::get_sticker_set_id(const tl_object_ptr<telegram_api::InputStickerSet> &set_ptr) {
  CHECK(set_ptr != nullptr);
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return StickerSetId();
    case telegram_api::inputStickerSetID::ID:
      return StickerSetId(static_cast<const telegram_api::inputStickerSetID *>(set_ptr.get())->id_);
    case telegram_api::inputStickerSetShortName::ID:
      LOG(ERROR) << "Receive sticker set by its short name";
      return search_sticker_set(static_cast<const telegram_api::inputStickerSetShortName *>(set_ptr.get())->short_name_,
                                Auto());
    case telegram_api::inputStickerSetAnimatedEmoji::ID:
      LOG(ERROR) << "Receive special sticker set " << to_string(set_ptr);
      return add_special_sticker_set(SpecialStickerSetType(set_ptr).type_).id_;
    case telegram_api::inputStickerSetDice::ID:
      LOG(ERROR) << "Receive special sticker set " << to_string(set_ptr);
      return StickerSetId();
    default:
      UNREACHABLE();
      return StickerSetId();
  }
}

StickerSetId StickersManager::add_sticker_set(tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr) {
  CHECK(set_ptr != nullptr);
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return StickerSetId();
    case telegram_api::inputStickerSetID::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetID>(set_ptr);
      StickerSetId set_id{set->id_};
      add_sticker_set(set_id, set->access_hash_);
      return set_id;
    }
    case telegram_api::inputStickerSetShortName::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetShortName>(set_ptr);
      LOG(ERROR) << "Receive sticker set by its short name";
      return search_sticker_set(set->short_name_, Auto());
    }
    case telegram_api::inputStickerSetAnimatedEmoji::ID:
      LOG(ERROR) << "Receive special sticker set " << to_string(set_ptr);
      return add_special_sticker_set(SpecialStickerSetType(set_ptr).type_).id_;
    case telegram_api::inputStickerSetDice::ID:
      LOG(ERROR) << "Receive special sticker set " << to_string(set_ptr);
      return StickerSetId();
    default:
      UNREACHABLE();
      return StickerSetId();
  }
}

StickersManager::StickerSet *StickersManager::add_sticker_set(StickerSetId sticker_set_id, int64 access_hash) {
  auto &s = sticker_sets_[sticker_set_id];
  if (s == nullptr) {
    s = make_unique<StickerSet>();

    s->id = sticker_set_id;
    s->access_hash = access_hash;
    s->is_changed = false;
    s->need_save_to_database = false;
  } else {
    CHECK(s->id == sticker_set_id);
    if (s->access_hash != access_hash) {
      LOG(INFO) << "Access hash of " << sticker_set_id << " changed";
      s->access_hash = access_hash;
      s->need_save_to_database = true;
    }
  }
  return s.get();
}

FileId StickersManager::get_sticker_thumbnail_file_id(FileId file_id) const {
  auto sticker = get_sticker(file_id);
  CHECK(sticker != nullptr);
  return sticker->s_thumbnail.file_id;
}

void StickersManager::delete_sticker_thumbnail(FileId file_id) {
  auto &sticker = stickers_[file_id];
  CHECK(sticker != nullptr);
  sticker->s_thumbnail = PhotoSize();
}

vector<FileId> StickersManager::get_sticker_file_ids(FileId file_id) const {
  vector<FileId> result;
  auto sticker = get_sticker(file_id);
  CHECK(sticker != nullptr);
  result.push_back(file_id);
  if (sticker->s_thumbnail.file_id.is_valid()) {
    result.push_back(sticker->s_thumbnail.file_id);
  }
  if (sticker->m_thumbnail.file_id.is_valid()) {
    result.push_back(sticker->m_thumbnail.file_id);
  }
  return result;
}

FileId StickersManager::dup_sticker(FileId new_id, FileId old_id) {
  const Sticker *old_sticker = get_sticker(old_id);
  CHECK(old_sticker != nullptr);
  auto &new_sticker = stickers_[new_id];
  CHECK(!new_sticker);
  new_sticker = make_unique<Sticker>(*old_sticker);
  new_sticker->file_id = new_id;
  // there is no reason to dup m_thumbnail
  new_sticker->s_thumbnail.file_id = td_->file_manager_->dup_file_id(new_sticker->s_thumbnail.file_id);
  return new_id;
}

bool StickersManager::merge_stickers(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge stickers " << new_id << " and " << old_id;
  const Sticker *old_ = get_sticker(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = stickers_.find(new_id);
  if (new_it == stickers_.end()) {
    auto &old = stickers_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_sticker(new_id, old_id);
    } else {
      old->file_id = new_id;
      stickers_.emplace(new_id, std::move(old));
    }
  } else {
    Sticker *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    if (old_->set_id == new_->set_id && (old_->alt != new_->alt || old_->set_id != new_->set_id ||
                                         (!old_->is_animated && !new_->is_animated && old_->dimensions.width != 0 &&
                                          old_->dimensions.height != 0 && old_->dimensions != new_->dimensions))) {
      LOG(ERROR) << "Sticker has changed: alt = (" << old_->alt << ", " << new_->alt << "), set_id = (" << old_->set_id
                 << ", " << new_->set_id << "), dimensions = (" << old_->dimensions << ", " << new_->dimensions << ")";
    }

    new_->is_changed = true;

    if (old_->s_thumbnail != new_->s_thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->s_thumbnail.file_id, old_->s_thumbnail.file_id));
    }
    if (old_->m_thumbnail != new_->m_thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->m_thumbnail.file_id, old_->m_thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    stickers_.erase(old_id);
  }
  return true;
}

tl_object_ptr<telegram_api::InputStickerSet> StickersManager::get_input_sticker_set(const StickerSet *set) {
  CHECK(set != nullptr);
  return make_tl_object<telegram_api::inputStickerSetID>(set->id.get(), set->access_hash);
}

void StickersManager::reload_installed_sticker_sets(bool is_masks, bool force) {
  if (G()->close_flag()) {
    return;
  }

  auto &next_load_time = next_installed_sticker_sets_load_time_[is_masks];
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload sticker sets";
    next_load_time = -1;
    td_->create_handler<GetAllStickersQuery>()->send(is_masks, installed_sticker_sets_hash_[is_masks]);
  }
}

void StickersManager::reload_featured_sticker_sets(bool force) {
  if (G()->close_flag()) {
    return;
  }

  auto &next_load_time = next_featured_sticker_sets_load_time_;
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload trending sticker sets";
    next_load_time = -1;
    td_->create_handler<GetFeaturedStickerSetsQuery>()->send(featured_sticker_sets_hash_);
  }
}

void StickersManager::reload_old_featured_sticker_sets(uint32 generation) {
  if (generation != 0 && generation != old_featured_sticker_set_generation_) {
    return;
  }
  td_->create_handler<GetOldFeaturedStickerSetsQuery>()->send(static_cast<int32>(old_featured_sticker_set_ids_.size()),
                                                              OLD_FEATURED_STICKER_SET_SLICE_SIZE,
                                                              old_featured_sticker_set_generation_);
}

StickerSetId StickersManager::on_get_input_sticker_set(FileId sticker_file_id,
                                                       tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr,
                                                       MultiPromiseActor *load_data_multipromise_ptr) {
  if (set_ptr == nullptr) {
    return StickerSetId();
  }
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return StickerSetId();
    case telegram_api::inputStickerSetID::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetID>(set_ptr);
      StickerSetId set_id{set->id_};
      add_sticker_set(set_id, set->access_hash_);
      return set_id;
    }
    case telegram_api::inputStickerSetShortName::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetShortName>(set_ptr);
      if (load_data_multipromise_ptr == nullptr) {
        LOG(ERROR) << "Receive sticker set " << set->short_name_ << " by its short name";
        return search_sticker_set(set->short_name_, Auto());
      }
      auto set_id = search_sticker_set(set->short_name_, load_data_multipromise_ptr->get_promise());
      if (!set_id.is_valid()) {
        load_data_multipromise_ptr->add_promise(
            PromiseCreator::lambda([td = td_, sticker_file_id, short_name = set->short_name_](Result<Unit> result) {
              if (result.is_ok()) {
                // just in case
                td->stickers_manager_->on_resolve_sticker_set_short_name(sticker_file_id, short_name);
              }
            }));
      }
      // always return empty StickerSetId, because we can't trust the set_id provided by the peer in the secret chat
      // the real sticker set id will be set in on_get_sticker if and only if the sticker is really from the set
      return StickerSetId();
    }
    case telegram_api::inputStickerSetAnimatedEmoji::ID:
      return add_special_sticker_set(SpecialStickerSetType(set_ptr).type_).id_;
    case telegram_api::inputStickerSetDice::ID:
      return StickerSetId();
    default:
      UNREACHABLE();
      return StickerSetId();
  }
}

void StickersManager::on_resolve_sticker_set_short_name(FileId sticker_file_id, const string &short_name) {
  LOG(INFO) << "Resolve sticker " << sticker_file_id << " set to " << short_name;
  StickerSetId set_id = search_sticker_set(short_name, Auto());
  if (set_id.is_valid()) {
    auto &s = stickers_[sticker_file_id];
    CHECK(s != nullptr);
    CHECK(s->file_id == sticker_file_id);
    if (s->set_id != set_id) {
      s->set_id = set_id;
      s->is_changed = true;
    }
  }
}

void StickersManager::add_sticker_thumbnail(Sticker *s, PhotoSize thumbnail) {
  if (!thumbnail.file_id.is_valid()) {
    return;
  }
  if (thumbnail.type == 'm') {
    s->m_thumbnail = thumbnail;
    return;
  }
  if (thumbnail.type == 's' || thumbnail.type == 't') {
    s->s_thumbnail = thumbnail;
    return;
  }
  LOG(ERROR) << "Receive sticker thumbnail of unsupported type " << thumbnail.type;
}

void StickersManager::create_sticker(FileId file_id, string minithumbnail, PhotoSize thumbnail, Dimensions dimensions,
                                     tl_object_ptr<telegram_api::documentAttributeSticker> sticker, bool is_animated,
                                     MultiPromiseActor *load_data_multipromise_ptr) {
  if (is_animated && dimensions.width == 0) {
    dimensions.width = 512;
    dimensions.height = 512;
  }

  auto s = make_unique<Sticker>();
  s->file_id = file_id;
  s->dimensions = dimensions;
  s->minithumbnail = std::move(minithumbnail);
  add_sticker_thumbnail(s.get(), thumbnail);
  if (sticker != nullptr) {
    s->set_id = on_get_input_sticker_set(file_id, std::move(sticker->stickerset_), load_data_multipromise_ptr);
    s->alt = std::move(sticker->alt_);

    s->is_mask = (sticker->flags_ & telegram_api::documentAttributeSticker::MASK_MASK) != 0;
    if ((sticker->flags_ & telegram_api::documentAttributeSticker::MASK_COORDS_MASK) != 0) {
      CHECK(sticker->mask_coords_ != nullptr);
      int32 point = sticker->mask_coords_->n_;
      if (0 <= point && point <= 3) {
        s->point = sticker->mask_coords_->n_;
        s->x_shift = sticker->mask_coords_->x_;
        s->y_shift = sticker->mask_coords_->y_;
        s->scale = sticker->mask_coords_->zoom_;
      }
    }
  }
  s->is_animated = is_animated;
  on_get_sticker(std::move(s), sticker != nullptr);
}

bool StickersManager::has_input_media(FileId sticker_file_id, bool is_secret) const {
  auto file_view = td_->file_manager_->get_file_view(sticker_file_id);
  if (is_secret) {
    const Sticker *sticker = get_sticker(sticker_file_id);
    CHECK(sticker != nullptr);
    if (file_view.is_encrypted_secret()) {
      if (!file_view.encryption_key().empty() && file_view.has_remote_location() &&
          !sticker->s_thumbnail.file_id.is_valid()) {
        return true;
      }
    } else if (!file_view.is_encrypted()) {
      if (sticker->set_id.is_valid()) {
        // stickers within a set can be sent by id and access_hash
        return true;
      }
    }
  } else {
    if (file_view.is_encrypted()) {
      return false;
    }
    if (td_->auth_manager_->is_bot() && file_view.has_remote_location()) {
      return true;
    }
    // having remote location is not enough to have InputMedia, because the file may not have valid file_reference
    // also file_id needs to be duped, because upload can be called to repair the file_reference and every upload
    // request must have unique file_id
    if (/* file_view.has_remote_location() || */ file_view.has_url()) {
      return true;
    }
  }

  return false;
}

SecretInputMedia StickersManager::get_secret_input_media(FileId sticker_file_id,
                                                         tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                         BufferSlice thumbnail) const {
  const Sticker *sticker = get_sticker(sticker_file_id);
  CHECK(sticker != nullptr);
  auto file_view = td_->file_manager_->get_file_view(sticker_file_id);
  if (file_view.is_encrypted_secret()) {
    if (file_view.has_remote_location()) {
      input_file = file_view.main_remote_location().as_input_encrypted_file();
    }
    if (!input_file) {
      return {};
    }
    if (sticker->s_thumbnail.file_id.is_valid() && thumbnail.empty()) {
      return {};
    }
  } else if (!file_view.is_encrypted()) {
    if (!sticker->set_id.is_valid()) {
      // stickers without set can't be sent by id and access_hash
      return {};
    }
  } else {
    return {};
  }

  tl_object_ptr<secret_api::InputStickerSet> input_sticker_set = make_tl_object<secret_api::inputStickerSetEmpty>();
  if (sticker->set_id.is_valid()) {
    const StickerSet *sticker_set = get_sticker_set(sticker->set_id);
    CHECK(sticker_set != nullptr);
    if (sticker_set->is_inited) {
      input_sticker_set = make_tl_object<secret_api::inputStickerSetShortName>(sticker_set->short_name);
    } else {
      // TODO load sticker set
    }
  }

  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  attributes.push_back(
      secret_api::make_object<secret_api::documentAttributeSticker>(sticker->alt, std::move(input_sticker_set)));
  if (sticker->dimensions.width != 0 && sticker->dimensions.height != 0) {
    attributes.push_back(secret_api::make_object<secret_api::documentAttributeImageSize>(sticker->dimensions.width,
                                                                                         sticker->dimensions.height));
  }

  if (file_view.is_encrypted_secret()) {
    auto &encryption_key = file_view.encryption_key();
    return SecretInputMedia{std::move(input_file),
                            make_tl_object<secret_api::decryptedMessageMediaDocument>(
                                std::move(thumbnail), sticker->s_thumbnail.dimensions.width,
                                sticker->s_thumbnail.dimensions.height, get_sticker_mime_type(sticker),
                                narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
                                BufferSlice(encryption_key.iv_slice()), std::move(attributes), "")};
  } else {
    CHECK(!file_view.is_encrypted());
    auto &remote_location = file_view.remote_location();
    if (remote_location.is_web()) {
      // web stickers shouldn't have set_id
      LOG(ERROR) << "Have a web sticker in " << sticker->set_id;
      return {};
    }
    return SecretInputMedia{nullptr, make_tl_object<secret_api::decryptedMessageMediaExternalDocument>(
                                         remote_location.get_id(), remote_location.get_access_hash(), 0 /*date*/,
                                         get_sticker_mime_type(sticker), narrow_cast<int32>(file_view.size()),
                                         make_tl_object<secret_api::photoSizeEmpty>("t"),
                                         remote_location.get_dc_id().get_raw_id(), std::move(attributes))};
  }
}

tl_object_ptr<telegram_api::InputMedia> StickersManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail, const string &emoji) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.main_remote_location().is_web() && input_file == nullptr) {
    int32 flags = 0;
    if (!emoji.empty()) {
      flags |= telegram_api::inputMediaDocument::QUERY_MASK;
    }
    return make_tl_object<telegram_api::inputMediaDocument>(flags, file_view.main_remote_location().as_input_document(),
                                                            0, emoji);
  }
  if (file_view.has_url()) {
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(0, file_view.url(), 0);
  }

  if (input_file != nullptr) {
    const Sticker *s = get_sticker(file_id);
    CHECK(s != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    if (s->dimensions.width != 0 && s->dimensions.height != 0) {
      attributes.push_back(
          make_tl_object<telegram_api::documentAttributeImageSize>(s->dimensions.width, s->dimensions.height));
    }
    attributes.push_back(make_tl_object<telegram_api::documentAttributeSticker>(
        0, false /*ignored*/, s->alt, make_tl_object<telegram_api::inputStickerSetEmpty>(), nullptr));

    int32 flags = 0;
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    auto mime_type = get_sticker_mime_type(s);
    if (!s->is_animated && !s->set_id.is_valid()) {
      auto suggested_path = file_view.suggested_path();
      const PathView path_view(suggested_path);
      if (path_view.extension() == "tgs") {
        mime_type = "application/x-tgsticker";
      }
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), mime_type,
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

StickerSetId StickersManager::on_get_sticker_set(tl_object_ptr<telegram_api::stickerSet> &&set, bool is_changed,
                                                 const char *source) {
  CHECK(set != nullptr);
  StickerSetId set_id{set->id_};
  StickerSet *s = add_sticker_set(set_id, set->access_hash_);

  bool is_installed = (set->flags_ & telegram_api::stickerSet::INSTALLED_DATE_MASK) != 0;
  bool is_archived = (set->flags_ & telegram_api::stickerSet::ARCHIVED_MASK) != 0;
  bool is_official = (set->flags_ & telegram_api::stickerSet::OFFICIAL_MASK) != 0;
  bool is_animated = (set->flags_ & telegram_api::stickerSet::ANIMATED_MASK) != 0;
  bool is_masks = (set->flags_ & telegram_api::stickerSet::MASKS_MASK) != 0;

  PhotoSize thumbnail;
  string minithumbnail;
  for (auto &thumb : set->thumbs_) {
    auto photo_size = get_photo_size(td_->file_manager_.get(), {set_id.get(), s->access_hash}, 0, 0, "",
                                     DcId::create(set->thumb_dc_id_), DialogId(), std::move(thumb),
                                     is_animated ? PhotoFormat::Tgs : PhotoFormat::Webp);
    if (photo_size.get_offset() == 0) {
      if (!thumbnail.file_id.is_valid()) {
        thumbnail = std::move(photo_size.get<0>());
      }
    } else {
      minithumbnail = std::move(photo_size.get<1>());
    }
  }
  if (!s->is_inited) {
    LOG(INFO) << "Init " << set_id;
    s->is_inited = true;
    s->title = std::move(set->title_);
    s->short_name = std::move(set->short_name_);
    s->minithumbnail = std::move(minithumbnail);
    s->thumbnail = std::move(thumbnail);
    s->is_thumbnail_reloaded = true;
    s->are_legacy_sticker_thumbnails_reloaded = true;
    s->sticker_count = set->count_;
    s->hash = set->hash_;
    s->is_official = is_official;
    s->is_animated = is_animated;
    s->is_masks = is_masks;
    s->is_changed = true;
  } else {
    CHECK(s->id == set_id);
    if (s->access_hash != set->access_hash_) {
      LOG(INFO) << "Access hash of " << set_id << " has changed";
      s->access_hash = set->access_hash_;
      s->need_save_to_database = true;
    }
    if (s->title != set->title_) {
      LOG(INFO) << "Title of " << set_id << " has changed";
      s->title = std::move(set->title_);
      s->is_changed = true;

      if (installed_sticker_sets_hints_[s->is_masks].has_key(set_id.get())) {
        installed_sticker_sets_hints_[s->is_masks].add(set_id.get(), PSLICE() << s->title << ' ' << s->short_name);
      }
    }
    if (s->short_name != set->short_name_) {
      LOG(ERROR) << "Short name of " << set_id << " has changed from \"" << s->short_name << "\" to \""
                 << set->short_name_ << "\" from " << source;
      short_name_to_sticker_set_id_.erase(clean_username(s->short_name));
      s->short_name = std::move(set->short_name_);
      s->is_changed = true;

      if (installed_sticker_sets_hints_[s->is_masks].has_key(set_id.get())) {
        installed_sticker_sets_hints_[s->is_masks].add(set_id.get(), PSLICE() << s->title << ' ' << s->short_name);
      }
    }
    if (s->minithumbnail != minithumbnail) {
      LOG(INFO) << "Minithumbnail of " << set_id << " has changed";
      s->minithumbnail = std::move(minithumbnail);
      s->is_changed = true;
    }
    if (s->thumbnail != thumbnail) {
      LOG(INFO) << "Thumbnail of " << set_id << " has changed from " << s->thumbnail << " to " << thumbnail;
      s->thumbnail = std::move(thumbnail);
      s->is_changed = true;
    }
    if (!s->is_thumbnail_reloaded || !s->are_legacy_sticker_thumbnails_reloaded) {
      LOG(INFO) << "Sticker thumbnails and thumbnail of " << set_id << " was reloaded";
      s->is_thumbnail_reloaded = true;
      s->are_legacy_sticker_thumbnails_reloaded = true;
      s->need_save_to_database = true;
    }

    if (s->sticker_count != set->count_ || s->hash != set->hash_) {
      LOG(INFO) << "Number of stickers in " << set_id << " changed from " << s->sticker_count << " to " << set->count_;
      s->is_loaded = false;

      s->sticker_count = set->count_;
      s->hash = set->hash_;
      if (s->was_loaded) {
        s->need_save_to_database = true;
      } else {
        s->is_changed = true;
      }
    }

    if (s->is_official != is_official) {
      LOG(INFO) << "Official flag of " << set_id << " changed to " << is_official;
      s->is_official = is_official;
      s->is_changed = true;
    }
    if (s->is_animated != is_animated) {
      LOG(ERROR) << "Animated type of " << set_id << "/" << s->short_name << " has changed from " << s->is_animated
                 << " to " << is_animated << " from " << source;
      s->is_animated = is_animated;
      s->is_changed = true;
    }
    LOG_IF(ERROR, s->is_masks != is_masks) << "Masks type of " << set_id << "/" << s->short_name << " has changed from "
                                           << s->is_masks << " to " << is_masks << " from " << source;
  }
  short_name_to_sticker_set_id_.emplace(clean_username(s->short_name), set_id);

  on_update_sticker_set(s, is_installed, is_archived, is_changed);

  return set_id;
}

StickerSetId StickersManager::on_get_sticker_set_covered(tl_object_ptr<telegram_api::StickerSetCovered> &&set_ptr,
                                                         bool is_changed, const char *source) {
  StickerSetId set_id;
  switch (set_ptr->get_id()) {
    case telegram_api::stickerSetCovered::ID: {
      auto covered_set = move_tl_object_as<telegram_api::stickerSetCovered>(set_ptr);
      set_id = on_get_sticker_set(std::move(covered_set->set_), is_changed, source);
      if (!set_id.is_valid()) {
        break;
      }

      auto sticker_set = get_sticker_set(set_id);
      CHECK(sticker_set != nullptr);
      CHECK(sticker_set->is_inited);
      if (sticker_set->was_loaded) {
        break;
      }
      if (sticker_set->sticker_count == 0) {
        break;
      }

      auto &sticker_ids = sticker_set->sticker_ids;

      auto sticker_id = on_get_sticker_document(std::move(covered_set->cover_)).second;
      if (sticker_id.is_valid() && !td::contains(sticker_ids, sticker_id)) {
        sticker_ids.push_back(sticker_id);
        sticker_set->is_changed = true;
      }

      break;
    }
    case telegram_api::stickerSetMultiCovered::ID: {
      auto multicovered_set = move_tl_object_as<telegram_api::stickerSetMultiCovered>(set_ptr);
      set_id = on_get_sticker_set(std::move(multicovered_set->set_), is_changed, source);
      if (!set_id.is_valid()) {
        break;
      }

      auto sticker_set = get_sticker_set(set_id);
      CHECK(sticker_set != nullptr);
      CHECK(sticker_set->is_inited);
      if (sticker_set->was_loaded) {
        break;
      }
      auto &sticker_ids = sticker_set->sticker_ids;

      for (auto &cover : multicovered_set->covers_) {
        auto sticker_id = on_get_sticker_document(std::move(cover)).second;
        if (sticker_id.is_valid() && !td::contains(sticker_ids, sticker_id)) {
          sticker_ids.push_back(sticker_id);
          sticker_set->is_changed = true;
        }
      }

      break;
    }
    default:
      UNREACHABLE();
  }
  return set_id;
}

StickerSetId StickersManager::on_get_messages_sticker_set(StickerSetId sticker_set_id,
                                                          tl_object_ptr<telegram_api::messages_stickerSet> &&set,
                                                          bool is_changed, const char *source) {
  LOG(INFO) << "Receive sticker set " << to_string(set);

  auto set_id = on_get_sticker_set(std::move(set->set_), is_changed, source);
  if (!set_id.is_valid()) {
    return set_id;
  }
  if (sticker_set_id.is_valid() && sticker_set_id != set_id) {
    LOG(ERROR) << "Expected " << sticker_set_id << ", but receive " << set_id << " from " << source;
    on_load_sticker_set_fail(sticker_set_id, Status::Error(500, "Internal Server Error"));
    return StickerSetId();
  }

  auto s = get_sticker_set(set_id);
  CHECK(s != nullptr);
  CHECK(s->is_inited);

  s->expires_at = G()->unix_time() +
                  (td_->auth_manager_->is_bot() ? Random::fast(10 * 60, 15 * 60) : Random::fast(30 * 60, 50 * 60));

  if (s->is_loaded) {
    update_sticker_set(s);
    send_update_installed_sticker_sets();
    return set_id;
  }
  s->was_loaded = true;
  s->is_loaded = true;
  s->is_changed = true;

  vector<tl_object_ptr<telegram_api::stickerPack>> packs = std::move(set->packs_);
  vector<tl_object_ptr<telegram_api::Document>> documents = std::move(set->documents_);

  std::unordered_map<int64, FileId> document_id_to_sticker_id;

  s->sticker_ids.clear();
  bool is_bot = td_->auth_manager_->is_bot();
  for (auto &document_ptr : documents) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr));
    if (!sticker_id.second.is_valid()) {
      continue;
    }

    s->sticker_ids.push_back(sticker_id.second);
    if (!is_bot) {
      document_id_to_sticker_id.insert(sticker_id);
    }
  }
  if (static_cast<int32>(s->sticker_ids.size()) != s->sticker_count) {
    LOG(ERROR) << "Wrong sticker set size " << s->sticker_count << " instead of " << s->sticker_ids.size()
               << " specified in " << set_id << "/" << s->short_name << " from " << source;
    s->sticker_count = static_cast<int32>(s->sticker_ids.size());
  }

  if (!is_bot) {
    s->emoji_stickers_map_.clear();
    s->sticker_emojis_map_.clear();
    for (auto &pack : packs) {
      vector<FileId> stickers;
      stickers.reserve(pack->documents_.size());
      for (int64 document_id : pack->documents_) {
        auto it = document_id_to_sticker_id.find(document_id);
        if (it == document_id_to_sticker_id.end()) {
          LOG(ERROR) << "Can't find document with id " << document_id << " in " << set_id << "/" << s->short_name
                     << " from " << source;
          continue;
        }

        stickers.push_back(it->second);
        s->sticker_emojis_map_[it->second].push_back(pack->emoticon_);
      }
      auto &sticker_ids = s->emoji_stickers_map_[remove_emoji_modifiers(pack->emoticon_)];
      for (auto sticker_id : stickers) {
        if (!td::contains(sticker_ids, sticker_id)) {
          sticker_ids.push_back(sticker_id);
        }
      }
    }
  }

  update_sticker_set(s);
  update_load_requests(s, true, Status::OK());
  send_update_installed_sticker_sets();
  return set_id;
}

void StickersManager::on_load_sticker_set_fail(StickerSetId sticker_set_id, const Status &error) {
  if (!sticker_set_id.is_valid()) {
    return;
  }
  update_load_requests(get_sticker_set(sticker_set_id), true, error);
}

void StickersManager::update_load_requests(StickerSet *sticker_set, bool with_stickers, const Status &status) {
  if (sticker_set == nullptr) {
    return;
  }
  if (with_stickers) {
    for (auto load_request_id : sticker_set->load_requests) {
      update_load_request(load_request_id, status);
    }

    sticker_set->load_requests.clear();
  }
  for (auto load_request_id : sticker_set->load_without_stickers_requests) {
    update_load_request(load_request_id, status);
  }

  sticker_set->load_without_stickers_requests.clear();

  if (status.message() == "STICKERSET_INVALID") {
    // the sticker set is likely to be deleted
    // clear short_name_to_sticker_set_id_ to allow next searchStickerSet request to succeed
    short_name_to_sticker_set_id_.erase(clean_username(sticker_set->short_name));
  }
}

void StickersManager::update_load_request(uint32 load_request_id, const Status &status) {
  auto it = sticker_set_load_requests_.find(load_request_id);
  CHECK(it != sticker_set_load_requests_.end());
  CHECK(it->second.left_queries > 0);
  if (status.is_error() && it->second.error.is_ok()) {
    it->second.error = status.clone();
  }
  if (--it->second.left_queries == 0) {
    if (it->second.error.is_ok()) {
      it->second.promise.set_value(Unit());
    } else {
      it->second.promise.set_error(std::move(it->second.error));
    }
    sticker_set_load_requests_.erase(it);
  }
}

void StickersManager::on_get_special_sticker_set(const SpecialStickerSetType &type, StickerSetId sticker_set_id) {
  auto s = get_sticker_set(sticker_set_id);
  CHECK(s != nullptr);
  CHECK(s->is_inited);
  CHECK(s->is_loaded);

  auto &sticker_set = add_special_sticker_set(type.type_);
  if (sticker_set_id == sticker_set.id_ && s->short_name == sticker_set.short_name_ && !s->short_name.empty()) {
    on_load_special_sticker_set(type, Status::OK());
    return;
  }

  sticker_set.id_ = sticker_set_id;
  sticker_set.access_hash_ = s->access_hash;
  sticker_set.short_name_ = clean_username(s->short_name);
  sticker_set.type_ = type;

  G()->td_db()->get_binlog_pmc()->set(type.type_, PSTRING() << sticker_set.id_.get() << ' ' << sticker_set.access_hash_
                                                            << ' ' << sticker_set.short_name_);
  if (type.type_ == SpecialStickerSetType::animated_emoji()) {
    G()->shared_config().set_option_string(PSLICE() << type.type_ << "_name", sticker_set.short_name_);
  } else if (!type.get_dice_emoji().empty()) {
    sticker_set.is_being_loaded_ = true;
  }
  on_load_special_sticker_set(type, Status::OK());
}

void StickersManager::on_get_installed_sticker_sets(bool is_masks,
                                                    tl_object_ptr<telegram_api::messages_AllStickers> &&stickers_ptr) {
  next_installed_sticker_sets_load_time_[is_masks] = Time::now_cached() + Random::fast(30 * 60, 50 * 60);

  CHECK(stickers_ptr != nullptr);
  int32 constructor_id = stickers_ptr->get_id();
  if (constructor_id == telegram_api::messages_allStickersNotModified::ID) {
    LOG(INFO) << (is_masks ? "Masks" : "Stickers") << " are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_allStickers::ID);
  auto stickers = move_tl_object_as<telegram_api::messages_allStickers>(stickers_ptr);

  std::unordered_set<StickerSetId, StickerSetIdHash> uninstalled_sticker_sets(
      installed_sticker_set_ids_[is_masks].begin(), installed_sticker_set_ids_[is_masks].end());

  vector<StickerSetId> sets_to_load;
  vector<StickerSetId> installed_sticker_set_ids;
  vector<int32> debug_hashes;
  vector<int64> debug_sticker_set_ids;
  std::reverse(stickers->sets_.begin(), stickers->sets_.end());  // apply installed sticker sets in reverse order
  for (auto &set : stickers->sets_) {
    debug_hashes.push_back(set->hash_);
    debug_sticker_set_ids.push_back(set->id_);
    StickerSetId set_id = on_get_sticker_set(std::move(set), false, "on_get_installed_sticker_sets");
    if (!set_id.is_valid()) {
      continue;
    }

    auto sticker_set = get_sticker_set(set_id);
    CHECK(sticker_set != nullptr);
    LOG_IF(ERROR, !sticker_set->is_installed) << "Receive non-installed sticker set in getAllStickers";
    LOG_IF(ERROR, sticker_set->is_archived) << "Receive archived sticker set in getAllStickers";
    LOG_IF(ERROR, sticker_set->is_masks != is_masks) << "Receive sticker set of a wrong type in getAllStickers";
    CHECK(sticker_set->is_inited);

    if (sticker_set->is_installed && !sticker_set->is_archived && sticker_set->is_masks == is_masks) {
      installed_sticker_set_ids.push_back(set_id);
      uninstalled_sticker_sets.erase(set_id);
    }
    update_sticker_set(sticker_set);

    if (!sticker_set->is_archived && !sticker_set->is_loaded) {
      sets_to_load.push_back(set_id);
    }
  }
  std::reverse(debug_hashes.begin(), debug_hashes.end());
  std::reverse(installed_sticker_set_ids.begin(), installed_sticker_set_ids.end());
  std::reverse(debug_sticker_set_ids.begin(), debug_sticker_set_ids.end());

  if (!sets_to_load.empty()) {
    load_sticker_sets(std::move(sets_to_load), Auto());
  }

  for (auto set_id : uninstalled_sticker_sets) {
    auto sticker_set = get_sticker_set(set_id);
    CHECK(sticker_set != nullptr);
    CHECK(sticker_set->is_installed && !sticker_set->is_archived);
    on_update_sticker_set(sticker_set, false, false, true);
    update_sticker_set(sticker_set);
  }

  on_load_installed_sticker_sets_finished(is_masks, std::move(installed_sticker_set_ids));

  if (installed_sticker_sets_hash_[is_masks] != stickers->hash_) {
    LOG(ERROR) << "Sticker sets hash mismatch: server hash list = " << format::as_array(debug_hashes)
               << ", client hash list = "
               << format::as_array(
                      transform(installed_sticker_set_ids_[is_masks],
                                [this](StickerSetId sticker_set_id) { return get_sticker_set(sticker_set_id)->hash; }))
               << ", server sticker set list = " << format::as_array(debug_sticker_set_ids)
               << ", client sticker set list = " << format::as_array(installed_sticker_set_ids_[is_masks])
               << ", server hash = " << stickers->hash_ << ", client hash = " << installed_sticker_sets_hash_[is_masks];
  }
}

void StickersManager::on_get_installed_sticker_sets_failed(bool is_masks, Status error) {
  CHECK(error.is_error());
  next_installed_sticker_sets_load_time_[is_masks] = Time::now_cached() + Random::fast(5, 10);
  auto promises = std::move(load_installed_sticker_sets_queries_[is_masks]);
  load_installed_sticker_sets_queries_[is_masks].clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

vector<FileId> StickersManager::get_stickers(string emoji, int32 limit, bool force, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    promise.set_error(Status::Error(7, "Method is not available for bots"));
    return {};
  }
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return {};
  }
  if (!are_installed_sticker_sets_loaded_[0]) {
    load_installed_sticker_sets(false, std::move(promise));
    return {};
  }

  emoji = remove_emoji_modifiers(emoji);
  if (!emoji.empty()) {
    if (!are_recent_stickers_loaded_[0]) {
      load_recent_stickers(false, std::move(promise));
      return {};
    }
    if (!are_favorite_stickers_loaded_) {
      load_favorite_stickers(std::move(promise));
      return {};
    }
    /*
    if (!are_featured_sticker_sets_loaded_) {
      load_featured_sticker_sets(std::move(promise));
      return {};
    }
    */
  }

  vector<StickerSetId> sets_to_load;
  bool need_load = false;
  for (const auto &sticker_set_id : installed_sticker_set_ids_[0]) {
    const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    CHECK(sticker_set->is_inited);
    CHECK(!sticker_set->is_archived);
    if (!sticker_set->is_loaded) {
      sets_to_load.push_back(sticker_set_id);
      if (!sticker_set->was_loaded) {
        need_load = true;
      }
    }
  }

  vector<FileId> prepend_sticker_ids;
  if (!emoji.empty()) {
    prepend_sticker_ids.reserve(favorite_sticker_ids_.size() + recent_sticker_ids_[0].size());
    append(prepend_sticker_ids, recent_sticker_ids_[0]);
    for (auto sticker_id : favorite_sticker_ids_) {
      if (!td::contains(prepend_sticker_ids, sticker_id)) {
        prepend_sticker_ids.push_back(sticker_id);
      }
    }

    auto prefer_animated = [this](FileId lhs, FileId rhs) {
      const Sticker *lhs_s = get_sticker(lhs);
      const Sticker *rhs_s = get_sticker(rhs);
      return lhs_s->is_animated && !rhs_s->is_animated;
    };
    // std::stable_sort(prepend_sticker_ids.begin(), prepend_sticker_ids.begin() + recent_sticker_ids_[0].size(),
    //                  prefer_animated);
    std::stable_sort(prepend_sticker_ids.begin() + recent_sticker_ids_[0].size(), prepend_sticker_ids.end(),
                     prefer_animated);

    LOG(INFO) << "Have " << recent_sticker_ids_[0] << " recent and " << favorite_sticker_ids_ << " favorite stickers";
    for (const auto &sticker_id : prepend_sticker_ids) {
      const Sticker *s = get_sticker(sticker_id);
      LOG(INFO) << "Have prepend sticker " << sticker_id << " from " << s->set_id;
      if (s->set_id.is_valid() && !td::contains(sets_to_load, s->set_id)) {
        const StickerSet *sticker_set = get_sticker_set(s->set_id);
        if (sticker_set == nullptr || !sticker_set->is_loaded) {
          sets_to_load.push_back(s->set_id);
          if (sticker_set == nullptr || !sticker_set->was_loaded) {
            need_load = true;
          }
        }
      }
    }
  }

  if (!sets_to_load.empty()) {
    if (need_load && !force) {
      load_sticker_sets(std::move(sets_to_load),
                        PromiseCreator::lambda([promise = std::move(promise)](Result<Unit> result) mutable {
                          if (result.is_error() && result.error().message() != "STICKERSET_INVALID") {
                            LOG(ERROR) << "Failed to load sticker sets: " << result.error();
                          }
                          promise.set_value(Unit());
                        }));
      return {};
    } else {
      load_sticker_sets(std::move(sets_to_load), Auto());
    }
  }

  vector<FileId> result;
  auto limit_size_t = static_cast<size_t>(limit);
  if (emoji.empty()) {
    for (const auto &sticker_set_id : installed_sticker_set_ids_[0]) {
      const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
      if (sticker_set == nullptr || !sticker_set->was_loaded) {
        continue;
      }

      append(result, sticker_set->sticker_ids);
      if (result.size() > limit_size_t) {
        result.resize(limit_size_t);
        break;
      }
    }
  } else {
    vector<const StickerSet *> examined_sticker_sets;
    for (const auto &sticker_set_id : installed_sticker_set_ids_[0]) {
      const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
      if (sticker_set == nullptr || !sticker_set->was_loaded) {
        continue;
      }

      if (!td::contains(examined_sticker_sets, sticker_set)) {
        examined_sticker_sets.push_back(sticker_set);
      }
    }
    std::stable_sort(
        examined_sticker_sets.begin(), examined_sticker_sets.end(),
        [](const StickerSet *lhs, const StickerSet *rhs) { return lhs->is_animated && !rhs->is_animated; });
    for (auto sticker_set : examined_sticker_sets) {
      auto it = sticker_set->emoji_stickers_map_.find(emoji);
      if (it != sticker_set->emoji_stickers_map_.end()) {
        LOG(INFO) << "Add " << it->second << " stickers from " << sticker_set->id;
        append(result, it->second);
      }
    }

    vector<FileId> sorted;
    sorted.reserve(min(limit_size_t, result.size()));
    auto recent_stickers_size = recent_sticker_ids_[0].size();
    const size_t MAX_RECENT_STICKERS = 5;
    for (size_t i = 0; i < prepend_sticker_ids.size(); i++) {
      if (sorted.size() == MAX_RECENT_STICKERS && i < recent_stickers_size) {
        LOG(INFO) << "Skip recent sticker " << prepend_sticker_ids[i];
        continue;
      }

      auto sticker_id = prepend_sticker_ids[i];
      bool is_good = false;
      auto it = std::find(result.begin(), result.end(), sticker_id);
      if (it != result.end()) {
        LOG(INFO) << "Found prepend sticker " << sticker_id << " in installed packs at position "
                  << (it - result.begin());
        *it = FileId();
        is_good = true;
      } else {
        const Sticker *s = get_sticker(sticker_id);
        if (remove_emoji_modifiers(s->alt) == emoji) {
          LOG(INFO) << "Found prepend sticker " << sticker_id << " main emoji matches";
          is_good = true;
        } else if (s->set_id.is_valid()) {
          const StickerSet *sticker_set = get_sticker_set(s->set_id);
          if (sticker_set != nullptr && sticker_set->was_loaded) {
            auto map_it = sticker_set->emoji_stickers_map_.find(emoji);
            if (map_it != sticker_set->emoji_stickers_map_.end()) {
              if (td::contains(map_it->second, sticker_id)) {
                LOG(INFO) << "Found prepend sticker " << sticker_id << " has matching emoji";
                is_good = true;
              }
            }
          }
        }
      }

      if (is_good) {
        sorted.push_back(sticker_id);
        if (sorted.size() == limit_size_t) {
          break;
        }
      }
    }
    if (sorted.size() != limit_size_t) {
      for (const auto &sticker_id : result) {
        if (sticker_id.is_valid()) {
          LOG(INFO) << "Add sticker " << sticker_id << " from installed sticker set";
          sorted.push_back(sticker_id);
          if (sorted.size() == limit_size_t) {
            break;
          }
        } else {
          LOG(INFO) << "Skip already added sticker";
        }
      }
    }

    result = std::move(sorted);
  }

  promise.set_value(Unit());
  return result;
}

vector<FileId> StickersManager::search_stickers(string emoji, int32 limit, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    promise.set_error(Status::Error(7, "Method is not available for bots"));
    return {};
  }
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return {};
  }
  if (limit > MAX_FOUND_STICKERS) {
    limit = MAX_FOUND_STICKERS;
  }
  if (emoji.empty()) {
    promise.set_error(Status::Error(3, "Emoji must be non-empty"));
    return {};
  }

  emoji = remove_emoji_modifiers(emoji);
  if (emoji.empty()) {
    promise.set_value(Unit());
    return {};
  }

  auto it = found_stickers_.find(emoji);
  if (it != found_stickers_.end() && Time::now() < it->second.next_reload_time_) {
    promise.set_value(Unit());
    const auto &sticker_ids = it->second.sticker_ids_;
    auto result_size = min(static_cast<size_t>(limit), sticker_ids.size());
    return vector<FileId>(sticker_ids.begin(), sticker_ids.begin() + result_size);
  }

  auto &promises = search_stickers_queries_[emoji];
  promises.push_back(std::move(promise));
  if (promises.size() == 1u) {
    int32 hash = 0;
    if (it != found_stickers_.end()) {
      hash = get_recent_stickers_hash(it->second.sticker_ids_);
    }
    td_->create_handler<SearchStickersQuery>()->send(std::move(emoji), hash);
  }

  return {};
}

void StickersManager::on_find_stickers_success(const string &emoji,
                                               tl_object_ptr<telegram_api::messages_Stickers> &&stickers) {
  CHECK(stickers != nullptr);
  switch (stickers->get_id()) {
    case telegram_api::messages_stickersNotModified::ID: {
      auto it = found_stickers_.find(emoji);
      if (it == found_stickers_.end()) {
        return on_find_stickers_fail(emoji, Status::Error(500, "Receive messages.stickerNotModified"));
      }
      auto &found_stickers = it->second;
      found_stickers.next_reload_time_ = Time::now() + found_stickers.cache_time_;
      break;
    }
    case telegram_api::messages_stickers::ID: {
      auto received_stickers = move_tl_object_as<telegram_api::messages_stickers>(stickers);

      auto &found_stickers = found_stickers_[emoji];
      found_stickers.cache_time_ = 300;
      found_stickers.next_reload_time_ = Time::now() + found_stickers.cache_time_;
      found_stickers.sticker_ids_.clear();

      for (auto &sticker : received_stickers->stickers_) {
        FileId sticker_id = on_get_sticker_document(std::move(sticker)).second;
        if (sticker_id.is_valid()) {
          found_stickers.sticker_ids_.push_back(sticker_id);
        }
      }
      break;
    }
    default:
      UNREACHABLE();
  }

  auto it = search_stickers_queries_.find(emoji);
  CHECK(it != search_stickers_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  search_stickers_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void StickersManager::on_find_stickers_fail(const string &emoji, Status &&error) {
  if (found_stickers_.count(emoji) != 0) {
    found_stickers_[emoji].cache_time_ = Random::fast(40, 80);
    return on_find_stickers_success(emoji, make_tl_object<telegram_api::messages_stickersNotModified>());
  }

  auto it = search_stickers_queries_.find(emoji);
  CHECK(it != search_stickers_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  search_stickers_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

vector<StickerSetId> StickersManager::get_installed_sticker_sets(bool is_masks, Promise<Unit> &&promise) {
  if (!are_installed_sticker_sets_loaded_[is_masks]) {
    load_installed_sticker_sets(is_masks, std::move(promise));
    return {};
  }
  reload_installed_sticker_sets(is_masks, false);

  promise.set_value(Unit());
  return installed_sticker_set_ids_[is_masks];
}

bool StickersManager::update_sticker_set_cache(const StickerSet *sticker_set, Promise<Unit> &promise) {
  CHECK(sticker_set != nullptr);
  auto set_id = sticker_set->id;
  if (!sticker_set->is_loaded) {
    if (!sticker_set->was_loaded || td_->auth_manager_->is_bot()) {
      load_sticker_sets({set_id}, std::move(promise));
      return true;
    } else {
      load_sticker_sets({set_id}, Auto());
    }
  } else if (sticker_set->is_installed) {
    reload_installed_sticker_sets(sticker_set->is_masks, false);
  } else {
    if (G()->unix_time() >= sticker_set->expires_at) {
      if (td_->auth_manager_->is_bot()) {
        do_reload_sticker_set(set_id, get_input_sticker_set(sticker_set), std::move(promise));
        return true;
      } else {
        do_reload_sticker_set(set_id, get_input_sticker_set(sticker_set), Auto());
      }
    }
  }

  return false;
}

StickerSetId StickersManager::get_sticker_set(StickerSetId set_id, Promise<Unit> &&promise) {
  const StickerSet *sticker_set = get_sticker_set(set_id);
  if (sticker_set == nullptr) {
    if (set_id.get() == GREAT_MINDS_SET_ID) {
      do_reload_sticker_set(set_id, make_tl_object<telegram_api::inputStickerSetID>(set_id.get(), 0),
                            std::move(promise));
      return StickerSetId();
    }

    promise.set_error(Status::Error(400, "Sticker set not found"));
    return StickerSetId();
  }

  if (update_sticker_set_cache(sticker_set, promise)) {
    return StickerSetId();
  }

  promise.set_value(Unit());
  return set_id;
}

StickerSetId StickersManager::search_sticker_set(const string &short_name_to_search, Promise<Unit> &&promise) {
  string short_name = clean_username(short_name_to_search);
  auto it = short_name_to_sticker_set_id_.find(short_name);
  const StickerSet *sticker_set = it == short_name_to_sticker_set_id_.end() ? nullptr : get_sticker_set(it->second);

  if (sticker_set == nullptr) {
    auto set_to_load = make_tl_object<telegram_api::inputStickerSetShortName>(short_name);
    do_reload_sticker_set(StickerSetId(), std::move(set_to_load), std::move(promise));
    return StickerSetId();
  }

  if (update_sticker_set_cache(sticker_set, promise)) {
    return StickerSetId();
  }

  promise.set_value(Unit());
  return sticker_set->id;
}

std::pair<int32, vector<StickerSetId>> StickersManager::search_installed_sticker_sets(bool is_masks,
                                                                                      const string &query, int32 limit,
                                                                                      Promise<Unit> &&promise) {
  LOG(INFO) << "Search installed " << (is_masks ? "mask " : "") << "sticker sets with query = \"" << query
            << "\" and limit = " << limit;

  if (limit < 0) {
    promise.set_error(Status::Error(400, "Limit must be non-negative"));
    return {};
  }

  if (!are_installed_sticker_sets_loaded_[is_masks]) {
    load_installed_sticker_sets(is_masks, std::move(promise));
    return {};
  }
  reload_installed_sticker_sets(is_masks, false);

  std::pair<size_t, vector<int64>> result = installed_sticker_sets_hints_[is_masks].search(query, limit);
  promise.set_value(Unit());
  return {narrow_cast<int32>(result.first), convert_sticker_set_ids(result.second)};
}

vector<StickerSetId> StickersManager::search_sticker_sets(const string &query, Promise<Unit> &&promise) {
  auto q = clean_name(query, 1000);
  auto it = found_sticker_sets_.find(q);
  if (it != found_sticker_sets_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  auto &promises = search_sticker_sets_queries_[q];
  promises.push_back(std::move(promise));
  if (promises.size() == 1u) {
    td_->create_handler<SearchStickerSetsQuery>()->send(std::move(q));
  }

  return {};
}

void StickersManager::on_find_sticker_sets_success(
    const string &query, tl_object_ptr<telegram_api::messages_FoundStickerSets> &&sticker_sets) {
  CHECK(sticker_sets != nullptr);
  switch (sticker_sets->get_id()) {
    case telegram_api::messages_foundStickerSetsNotModified::ID:
      return on_find_sticker_sets_fail(query, Status::Error(500, "Receive messages.foundStickerSetsNotModified"));
    case telegram_api::messages_foundStickerSets::ID: {
      auto found_stickers_sets = move_tl_object_as<telegram_api::messages_foundStickerSets>(sticker_sets);
      vector<StickerSetId> &sticker_set_ids = found_sticker_sets_[query];
      CHECK(sticker_set_ids.empty());

      for (auto &sticker_set : found_stickers_sets->sets_) {
        StickerSetId set_id = on_get_sticker_set_covered(std::move(sticker_set), true, "on_find_sticker_sets_success");
        if (!set_id.is_valid()) {
          continue;
        }

        update_sticker_set(get_sticker_set(set_id));
        sticker_set_ids.push_back(set_id);
      }

      send_update_installed_sticker_sets();
      break;
    }
    default:
      UNREACHABLE();
  }

  auto it = search_sticker_sets_queries_.find(query);
  CHECK(it != search_sticker_sets_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  search_sticker_sets_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void StickersManager::on_find_sticker_sets_fail(const string &query, Status &&error) {
  CHECK(found_sticker_sets_.count(query) == 0);

  auto it = search_sticker_sets_queries_.find(query);
  CHECK(it != search_sticker_sets_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  search_sticker_sets_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

void StickersManager::change_sticker_set(StickerSetId set_id, bool is_installed, bool is_archived,
                                         Promise<Unit> &&promise) {
  if (is_installed && is_archived) {
    return promise.set_error(Status::Error(400, "Sticker set can't be installed and archived simultaneously"));
  }
  const StickerSet *sticker_set = get_sticker_set(set_id);
  if (sticker_set == nullptr) {
    return promise.set_error(Status::Error(400, "Sticker set not found"));
  }
  if (!sticker_set->is_inited) {
    load_sticker_sets({set_id}, std::move(promise));
    return;
  }
  if (!are_installed_sticker_sets_loaded_[sticker_set->is_masks]) {
    load_installed_sticker_sets(sticker_set->is_masks, std::move(promise));
    return;
  }

  if (is_archived) {
    is_installed = true;
  }
  if (is_installed) {
    if (sticker_set->is_installed && is_archived == sticker_set->is_archived) {
      return promise.set_value(Unit());
    }

    td_->create_handler<InstallStickerSetQuery>(std::move(promise))
        ->send(set_id, get_input_sticker_set(sticker_set), is_archived);
    return;
  }

  if (!sticker_set->is_installed) {
    return promise.set_value(Unit());
  }

  td_->create_handler<UninstallStickerSetQuery>(std::move(promise))->send(set_id, get_input_sticker_set(sticker_set));
}

void StickersManager::on_update_sticker_set(StickerSet *sticker_set, bool is_installed, bool is_archived,
                                            bool is_changed, bool from_database) {
  LOG(INFO) << "Update " << sticker_set->id << ": installed = " << is_installed << ", archived = " << is_archived
            << ", changed = " << is_changed << ", from_database = " << from_database;
  CHECK(sticker_set->is_inited);
  if (is_archived) {
    is_installed = true;
  }
  if (sticker_set->is_installed == is_installed && sticker_set->is_archived == is_archived) {
    return;
  }

  bool was_added = sticker_set->is_installed && !sticker_set->is_archived;
  bool was_archived = sticker_set->is_archived;
  sticker_set->is_installed = is_installed;
  sticker_set->is_archived = is_archived;
  if (!from_database) {
    sticker_set->is_changed = true;
  }

  bool is_added = sticker_set->is_installed && !sticker_set->is_archived;
  if (was_added != is_added) {
    vector<StickerSetId> &sticker_set_ids = installed_sticker_set_ids_[sticker_set->is_masks];
    need_update_installed_sticker_sets_[sticker_set->is_masks] = true;

    if (is_added) {
      installed_sticker_sets_hints_[sticker_set->is_masks].add(
          sticker_set->id.get(), PSLICE() << sticker_set->title << ' ' << sticker_set->short_name);
      sticker_set_ids.insert(sticker_set_ids.begin(), sticker_set->id);
    } else {
      installed_sticker_sets_hints_[sticker_set->is_masks].remove(sticker_set->id.get());
      td::remove(sticker_set_ids, sticker_set->id);
    }
  }
  if (was_archived != is_archived && is_changed) {
    int32 &total_count = total_archived_sticker_set_count_[sticker_set->is_masks];
    vector<StickerSetId> &sticker_set_ids = archived_sticker_set_ids_[sticker_set->is_masks];
    if (total_count < 0) {
      return;
    }

    if (is_archived) {
      if (!td::contains(sticker_set_ids, sticker_set->id)) {
        total_count++;
        sticker_set_ids.insert(sticker_set_ids.begin(), sticker_set->id);
      }
    } else {
      total_count--;
      if (total_count < 0) {
        LOG(ERROR) << "Total count of archived sticker sets became negative";
        total_count = 0;
      }
      td::remove(sticker_set_ids, sticker_set->id);
    }
  }
}

void StickersManager::load_installed_sticker_sets(bool is_masks, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_installed_sticker_sets_loaded_[is_masks] = true;
  }
  if (are_installed_sticker_sets_loaded_[is_masks]) {
    promise.set_value(Unit());
    return;
  }
  load_installed_sticker_sets_queries_[is_masks].push_back(std::move(promise));
  if (load_installed_sticker_sets_queries_[is_masks].size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load installed " << (is_masks ? "mask " : "") << "sticker sets from database";
      G()->td_db()->get_sqlite_pmc()->get(is_masks ? "sss1" : "sss0", PromiseCreator::lambda([is_masks](string value) {
                                            send_closure(G()->stickers_manager(),
                                                         &StickersManager::on_load_installed_sticker_sets_from_database,
                                                         is_masks, std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load installed " << (is_masks ? "mask " : "") << "sticker sets from server";
      reload_installed_sticker_sets(is_masks, true);
    }
  }
}

void StickersManager::on_load_installed_sticker_sets_from_database(bool is_masks, string value) {
  if (G()->close_flag()) {
    return;
  }
  if (value.empty()) {
    LOG(INFO) << "Installed " << (is_masks ? "mask " : "") << "sticker sets aren't found in database";
    reload_installed_sticker_sets(is_masks, true);
    return;
  }

  LOG(INFO) << "Successfully loaded installed " << (is_masks ? "mask " : "") << "sticker set list of size "
            << value.size() << " from database";

  StickerSetListLogEvent log_event;
  auto status = log_event_parse(log_event, value);
  if (status.is_error()) {
    // can't happen unless database is broken
    LOG(ERROR) << "Can't load installed sticker set list: " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    return reload_installed_sticker_sets(is_masks, true);
  }

  vector<StickerSetId> sets_to_load;
  for (auto sticker_set_id : log_event.sticker_set_ids) {
    StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    if (!sticker_set->is_inited) {
      sets_to_load.push_back(sticker_set_id);
    }
  }
  std::reverse(sets_to_load.begin(), sets_to_load.end());  // load installed sticker sets in reverse order

  load_sticker_sets_without_stickers(
      std::move(sets_to_load),
      PromiseCreator::lambda(
          [is_masks, sticker_set_ids = std::move(log_event.sticker_set_ids)](Result<> result) mutable {
            if (result.is_ok()) {
              send_closure(G()->stickers_manager(), &StickersManager::on_load_installed_sticker_sets_finished, is_masks,
                           std::move(sticker_set_ids), true);
            } else {
              send_closure(G()->stickers_manager(), &StickersManager::reload_installed_sticker_sets, is_masks, true);
            }
          }));
}

void StickersManager::on_load_installed_sticker_sets_finished(bool is_masks,
                                                              vector<StickerSetId> &&installed_sticker_set_ids,
                                                              bool from_database) {
  bool need_reload = false;
  vector<StickerSetId> old_installed_sticker_set_ids;
  if (!are_installed_sticker_sets_loaded_[is_masks] && !installed_sticker_set_ids_[is_masks].empty()) {
    old_installed_sticker_set_ids = std::move(installed_sticker_set_ids_[is_masks]);
  }
  installed_sticker_set_ids_[is_masks].clear();
  for (auto set_id : installed_sticker_set_ids) {
    CHECK(set_id.is_valid());

    auto sticker_set = get_sticker_set(set_id);
    CHECK(sticker_set != nullptr);
    CHECK(sticker_set->is_inited);
    CHECK(sticker_set->is_masks == is_masks);
    if (sticker_set->is_installed && !sticker_set->is_archived) {
      installed_sticker_set_ids_[is_masks].push_back(set_id);
    } else {
      need_reload = true;
    }
  }
  if (need_reload) {
    LOG(ERROR) << "Reload installed " << (is_masks ? "mask " : "") << "sticker sets, because only "
               << installed_sticker_set_ids_[is_masks].size() << " of " << installed_sticker_set_ids.size()
               << " are really installed after loading from " << (from_database ? "database" : "server");
    reload_installed_sticker_sets(is_masks, true);
  } else if (!old_installed_sticker_set_ids.empty() &&
             old_installed_sticker_set_ids != installed_sticker_set_ids_[is_masks]) {
    LOG(ERROR) << "Reload installed " << (is_masks ? "mask " : "") << "sticker sets, because they has changed from "
               << old_installed_sticker_set_ids << " to " << installed_sticker_set_ids_[is_masks]
               << " after loading from " << (from_database ? "database" : "server");
    reload_installed_sticker_sets(is_masks, true);
  }

  are_installed_sticker_sets_loaded_[is_masks] = true;
  need_update_installed_sticker_sets_[is_masks] = true;
  send_update_installed_sticker_sets(from_database);
  auto promises = std::move(load_installed_sticker_sets_queries_[is_masks]);
  load_installed_sticker_sets_queries_[is_masks].clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

string StickersManager::get_sticker_set_database_key(StickerSetId set_id) {
  return PSTRING() << "ss" << set_id.get();
}

string StickersManager::get_full_sticker_set_database_key(StickerSetId set_id) {
  return PSTRING() << "ssf" << set_id.get();
}

string StickersManager::get_sticker_set_database_value(const StickerSet *s, bool with_stickers) {
  LogEventStorerCalcLength storer_calc_length;
  store_sticker_set(s, with_stickers, storer_calc_length);

  BufferSlice value_buffer{storer_calc_length.get_length()};
  auto value = value_buffer.as_slice();

  LOG(DEBUG) << "Serialized size of " << s->id << " is " << value.size();

  LogEventStorerUnsafe storer_unsafe(value.ubegin());
  store_sticker_set(s, with_stickers, storer_unsafe);

  return value.str();
}

void StickersManager::update_sticker_set(StickerSet *sticker_set) {
  CHECK(sticker_set != nullptr);
  if (sticker_set->is_changed || sticker_set->need_save_to_database) {
    if (G()->parameters().use_file_db && !G()->close_flag()) {
      LOG(INFO) << "Save " << sticker_set->id << " to database";
      if (sticker_set->is_inited) {
        G()->td_db()->get_sqlite_pmc()->set(get_sticker_set_database_key(sticker_set->id),
                                            get_sticker_set_database_value(sticker_set, false), Auto());
      }
      if (sticker_set->was_loaded) {
        G()->td_db()->get_sqlite_pmc()->set(get_full_sticker_set_database_key(sticker_set->id),
                                            get_sticker_set_database_value(sticker_set, true), Auto());
      }
    }
    if (sticker_set->is_changed && sticker_set->was_loaded && sticker_set->was_update_sent) {
      send_closure(G()->td(), &Td::send_update,
                   td_api::make_object<td_api::updateStickerSet>(get_sticker_set_object(sticker_set->id)));
    }
    sticker_set->is_changed = false;
    sticker_set->need_save_to_database = false;
    if (sticker_set->is_inited) {
      update_load_requests(sticker_set, false, Status::OK());
    }
  }
}

void StickersManager::load_sticker_sets(vector<StickerSetId> &&sticker_set_ids, Promise<Unit> &&promise) {
  if (sticker_set_ids.empty()) {
    promise.set_value(Unit());
    return;
  }

  auto load_request_id = current_sticker_set_load_request_++;
  StickerSetLoadRequest &load_request = sticker_set_load_requests_[load_request_id];
  load_request.promise = std::move(promise);
  load_request.left_queries = sticker_set_ids.size();

  for (auto sticker_set_id : sticker_set_ids) {
    StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    CHECK(!sticker_set->is_loaded);

    sticker_set->load_requests.push_back(load_request_id);
    if (sticker_set->load_requests.size() == 1u) {
      if (G()->parameters().use_file_db && !sticker_set->was_loaded) {
        LOG(INFO) << "Trying to load " << sticker_set_id << " with stickers from database";
        G()->td_db()->get_sqlite_pmc()->get(
            get_full_sticker_set_database_key(sticker_set_id), PromiseCreator::lambda([sticker_set_id](string value) {
              send_closure(G()->stickers_manager(), &StickersManager::on_load_sticker_set_from_database, sticker_set_id,
                           true, std::move(value));
            }));
      } else {
        LOG(INFO) << "Trying to load " << sticker_set_id << " with stickers from server";
        do_reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
      }
    }
  }
}

void StickersManager::load_sticker_sets_without_stickers(vector<StickerSetId> &&sticker_set_ids,
                                                         Promise<Unit> &&promise) {
  if (sticker_set_ids.empty()) {
    promise.set_value(Unit());
    return;
  }

  auto load_request_id = current_sticker_set_load_request_++;
  StickerSetLoadRequest &load_request = sticker_set_load_requests_[load_request_id];
  load_request.promise = std::move(promise);
  load_request.left_queries = sticker_set_ids.size();

  for (auto sticker_set_id : sticker_set_ids) {
    StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    CHECK(!sticker_set->is_inited);

    if (!sticker_set->load_requests.empty()) {
      sticker_set->load_requests.push_back(load_request_id);
    } else {
      sticker_set->load_without_stickers_requests.push_back(load_request_id);
      if (sticker_set->load_without_stickers_requests.size() == 1u) {
        if (G()->parameters().use_file_db) {
          LOG(INFO) << "Trying to load " << sticker_set_id << " from database";
          G()->td_db()->get_sqlite_pmc()->get(
              get_sticker_set_database_key(sticker_set_id), PromiseCreator::lambda([sticker_set_id](string value) {
                send_closure(G()->stickers_manager(), &StickersManager::on_load_sticker_set_from_database,
                             sticker_set_id, false, std::move(value));
              }));
        } else {
          LOG(INFO) << "Trying to load " << sticker_set_id << " from server";
          do_reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
        }
      }
    }
  }
}

void StickersManager::on_load_sticker_set_from_database(StickerSetId sticker_set_id, bool with_stickers, string value) {
  if (G()->close_flag()) {
    return;
  }
  StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  if (sticker_set->was_loaded) {
    LOG(INFO) << "Receive from database previously loaded " << sticker_set_id;
    return;
  }
  if (!with_stickers && sticker_set->is_inited) {
    LOG(INFO) << "Receive from database previously inited " << sticker_set_id;
    return;
  }

  // it is possible that a server reload_sticker_set request has failed and cleared requests list with an error
  if (with_stickers) {
    // CHECK(!sticker_set->load_requests.empty());
  } else {
    // CHECK(!sticker_set->load_without_stickers_requests.empty());
  }

  if (value.empty()) {
    LOG(INFO) << "Failed to find in the database " << sticker_set_id;
    return do_reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
  }

  LOG(INFO) << "Successfully loaded " << sticker_set_id << " with" << (with_stickers ? "" : "out")
            << " stickers of size " << value.size() << " from database";

  auto old_sticker_count = sticker_set->sticker_ids.size();

  {
    LOG_IF(ERROR, sticker_set->is_changed) << sticker_set_id << " with" << (with_stickers ? "" : "out")
                                           << " stickers was changed before it is loaded from database";
    LogEventParser parser(value);
    parse_sticker_set(sticker_set, parser);
    LOG_IF(ERROR, sticker_set->is_changed)
        << sticker_set_id << " with" << (with_stickers ? "" : "out") << " stickers is changed";
    parser.fetch_end();
    auto status = parser.get_status();
    if (status.is_error()) {
      G()->td_db()->get_sqlite_sync_pmc()->erase(with_stickers ? get_full_sticker_set_database_key(sticker_set_id)
                                                               : get_sticker_set_database_key(sticker_set_id));
      // need to crash, because the current StickerSet state is spoiled by parse_sticker_set
      LOG(FATAL) << "Failed to parse " << sticker_set_id << ": " << status << ' '
                 << format::as_hex_dump<4>(Slice(value));
    }
  }
  if (!sticker_set->is_thumbnail_reloaded || !sticker_set->are_legacy_sticker_thumbnails_reloaded) {
    do_reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
  }

  if (with_stickers && old_sticker_count < 5 && old_sticker_count < sticker_set->sticker_ids.size()) {
    sticker_set->need_save_to_database = true;
    update_sticker_set(sticker_set);
  }

  update_load_requests(sticker_set, with_stickers, Status::OK());
}

void StickersManager::reload_sticker_set(StickerSetId sticker_set_id, int64 access_hash, Promise<Unit> &&promise) {
  do_reload_sticker_set(sticker_set_id,
                        make_tl_object<telegram_api::inputStickerSetID>(sticker_set_id.get(), access_hash),
                        std::move(promise));
}

void StickersManager::do_reload_sticker_set(StickerSetId sticker_set_id,
                                            tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set,
                                            Promise<Unit> &&promise) const {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  td_->create_handler<GetStickerSetQuery>(std::move(promise))->send(sticker_set_id, std::move(input_sticker_set));
}

void StickersManager::on_install_sticker_set(StickerSetId set_id, bool is_archived,
                                             tl_object_ptr<telegram_api::messages_StickerSetInstallResult> &&result) {
  StickerSet *sticker_set = get_sticker_set(set_id);
  CHECK(sticker_set != nullptr);
  on_update_sticker_set(sticker_set, true, is_archived, true);
  update_sticker_set(sticker_set);

  switch (result->get_id()) {
    case telegram_api::messages_stickerSetInstallResultSuccess::ID:
      break;
    case telegram_api::messages_stickerSetInstallResultArchive::ID: {
      auto archived_sets = move_tl_object_as<telegram_api::messages_stickerSetInstallResultArchive>(result);
      for (auto &archived_set_ptr : archived_sets->sets_) {
        StickerSetId archived_sticker_set_id =
            on_get_sticker_set_covered(std::move(archived_set_ptr), true, "on_install_sticker_set");
        if (archived_sticker_set_id.is_valid()) {
          auto archived_sticker_set = get_sticker_set(archived_sticker_set_id);
          CHECK(archived_sticker_set != nullptr);
          update_sticker_set(archived_sticker_set);
        }
      }
      break;
    }
    default:
      UNREACHABLE();
  }

  send_update_installed_sticker_sets();
}

void StickersManager::on_uninstall_sticker_set(StickerSetId set_id) {
  StickerSet *sticker_set = get_sticker_set(set_id);
  CHECK(sticker_set != nullptr);
  on_update_sticker_set(sticker_set, false, false, true);
  update_sticker_set(sticker_set);
  send_update_installed_sticker_sets();
}

td_api::object_ptr<td_api::updateDiceEmojis> StickersManager::get_update_dice_emojis_object() const {
  return td_api::make_object<td_api::updateDiceEmojis>(vector<string>(dice_emojis_));
}

void StickersManager::on_update_dice_emojis() {
  if (G()->close_flag()) {
    return;
  }
  if (td_->auth_manager_->is_bot()) {
    G()->shared_config().set_option_empty("dice_emojis");
    return;
  }
  if (!is_inited_) {
    return;
  }

  auto dice_emojis_str =
      G()->shared_config().get_option_string("dice_emojis", "🎲\x01🎯\x01🏀\x01⚽\x01⚽️\x01🎰\x01🎳");
  if (dice_emojis_str == dice_emojis_str_) {
    return;
  }
  dice_emojis_str_ = std::move(dice_emojis_str);
  auto new_dice_emojis = full_split(dice_emojis_str_, '\x01');
  for (auto &emoji : new_dice_emojis) {
    if (!td::contains(dice_emojis_, emoji)) {
      auto &special_sticker_set = add_special_sticker_set(SpecialStickerSetType::animated_dice(emoji));
      CHECK(!special_sticker_set.id_.is_valid());

      if (G()->parameters().use_file_db) {
        LOG(INFO) << "Load new dice sticker set for emoji " << emoji;
        load_special_sticker_set(special_sticker_set);
      }
    }
  }
  dice_emojis_ = std::move(new_dice_emojis);

  send_closure(G()->td(), &Td::send_update, get_update_dice_emojis_object());
}

void StickersManager::on_update_dice_success_values() {
  if (G()->close_flag()) {
    return;
  }
  if (td_->auth_manager_->is_bot()) {
    G()->shared_config().set_option_empty("dice_success_values");
    return;
  }
  if (!is_inited_) {
    return;
  }

  auto dice_success_values_str =
      G()->shared_config().get_option_string("dice_success_values", "0,6:62,5:110,5:110,5:110,64:110,6:110");
  if (dice_success_values_str == dice_success_values_str_) {
    return;
  }

  LOG(INFO) << "Change dice success values to " << dice_success_values_str;
  dice_success_values_str_ = std::move(dice_success_values_str);
  dice_success_values_ = transform(full_split(dice_success_values_str_, ','), [](Slice value) {
    auto result = split(value, ':');
    return std::make_pair(to_integer<int32>(result.first), to_integer<int32>(result.second));
  });
}

void StickersManager::on_update_sticker_sets() {
  // TODO better support
  archived_sticker_set_ids_[0].clear();
  total_archived_sticker_set_count_[0] = -1;
  reload_installed_sticker_sets(false, true);

  archived_sticker_set_ids_[1].clear();
  total_archived_sticker_set_count_[1] = -1;
  reload_installed_sticker_sets(true, true);
}

void StickersManager::register_dice(const string &emoji, int32 value, FullMessageId full_message_id,
                                    const char *source) {
  CHECK(!emoji.empty());
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  LOG(INFO) << "Register dice " << emoji << " with value " << value << " from " << full_message_id << " from "
            << source;
  bool is_inserted = dice_messages_[emoji].insert(full_message_id).second;
  LOG_CHECK(is_inserted) << source << " " << emoji << " " << value << " " << full_message_id;

  if (!td::contains(dice_emojis_, emoji)) {
    if (full_message_id.get_message_id().is_any_server() &&
        full_message_id.get_dialog_id().get_type() != DialogType::SecretChat) {
      send_closure(G()->config_manager(), &ConfigManager::get_app_config,
                   Promise<td_api::object_ptr<td_api::JsonValue>>());
    }
    return;
  }

  auto &special_sticker_set = add_special_sticker_set(SpecialStickerSetType::animated_dice(emoji));
  bool need_load = false;
  if (!special_sticker_set.id_.is_valid()) {
    need_load = true;
  } else {
    auto sticker_set = get_sticker_set(special_sticker_set.id_);
    CHECK(sticker_set != nullptr);
    need_load = !sticker_set->was_loaded;
  }

  if (need_load) {
    LOG(INFO) << "Waiting for a dice sticker set needed in " << full_message_id;
    if (!special_sticker_set.is_being_loaded_) {
      load_special_sticker_set(special_sticker_set);
    }
  } else {
    // TODO reload once in a while
    // reload_special_sticker_set(special_sticker_set);
  }
}

void StickersManager::unregister_dice(const string &emoji, int32 value, FullMessageId full_message_id,
                                      const char *source) {
  CHECK(!emoji.empty());
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  LOG(INFO) << "Unregister dice " << emoji << " with value " << value << " from " << full_message_id << " from "
            << source;
  auto &message_ids = dice_messages_[emoji];
  auto is_deleted = message_ids.erase(full_message_id) > 0;
  LOG_CHECK(is_deleted) << source << " " << emoji << " " << value << " " << full_message_id;

  if (message_ids.empty()) {
    dice_messages_.erase(emoji);
  }
}

void StickersManager::view_featured_sticker_sets(const vector<StickerSetId> &sticker_set_ids) {
  for (auto sticker_set_id : sticker_set_ids) {
    auto set = get_sticker_set(sticker_set_id);
    if (set != nullptr && !set->is_viewed) {
      if (td::contains(featured_sticker_set_ids_, sticker_set_id)) {
        need_update_featured_sticker_sets_ = true;
      }
      set->is_viewed = true;
      pending_viewed_featured_sticker_set_ids_.insert(sticker_set_id);
      update_sticker_set(set);
    }
  }

  send_update_featured_sticker_sets();

  if (!pending_viewed_featured_sticker_set_ids_.empty() && !pending_featured_sticker_set_views_timeout_.has_timeout()) {
    LOG(INFO) << "Have pending viewed trending sticker sets";
    pending_featured_sticker_set_views_timeout_.set_callback(read_featured_sticker_sets);
    pending_featured_sticker_set_views_timeout_.set_callback_data(static_cast<void *>(td_));
    pending_featured_sticker_set_views_timeout_.set_timeout_in(MAX_FEATURED_STICKER_SET_VIEW_DELAY);
  }
}

void StickersManager::read_featured_sticker_sets(void *td_void) {
  CHECK(td_void != nullptr);
  auto td = static_cast<Td *>(td_void);

  auto &set_ids = td->stickers_manager_->pending_viewed_featured_sticker_set_ids_;
  td->create_handler<ReadFeaturedStickerSetsQuery>()->send(vector<StickerSetId>(set_ids.begin(), set_ids.end()));
  set_ids.clear();
}

std::pair<int32, vector<StickerSetId>> StickersManager::get_archived_sticker_sets(bool is_masks,
                                                                                  StickerSetId offset_sticker_set_id,
                                                                                  int32 limit, bool force,
                                                                                  Promise<Unit> &&promise) {
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return {};
  }

  vector<StickerSetId> &sticker_set_ids = archived_sticker_set_ids_[is_masks];
  int32 total_count = total_archived_sticker_set_count_[is_masks];
  if (total_count >= 0) {
    auto offset_it = sticker_set_ids.begin();
    if (offset_sticker_set_id.is_valid()) {
      offset_it = std::find(sticker_set_ids.begin(), sticker_set_ids.end(), offset_sticker_set_id);
      if (offset_it == sticker_set_ids.end()) {
        offset_it = sticker_set_ids.begin();
      } else {
        ++offset_it;
      }
    }
    vector<StickerSetId> result;
    while (result.size() < static_cast<size_t>(limit)) {
      if (offset_it == sticker_set_ids.end()) {
        break;
      }
      auto sticker_set_id = *offset_it++;
      if (!sticker_set_id.is_valid()) {  // end of the list
        promise.set_value(Unit());
        return {total_count, std::move(result)};
      }
      result.push_back(sticker_set_id);
    }
    if (result.size() == static_cast<size_t>(limit) || force) {
      promise.set_value(Unit());
      return {total_count, std::move(result)};
    }
  }

  td_->create_handler<GetArchivedStickerSetsQuery>(std::move(promise))->send(is_masks, offset_sticker_set_id, limit);
  return {};
}

void StickersManager::on_get_archived_sticker_sets(
    bool is_masks, StickerSetId offset_sticker_set_id,
    vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets, int32 total_count) {
  vector<StickerSetId> &sticker_set_ids = archived_sticker_set_ids_[is_masks];
  if (!sticker_set_ids.empty() && sticker_set_ids.back() == StickerSetId()) {
    return;
  }
  if (total_count < 0) {
    LOG(ERROR) << "Receive " << total_count << " as total count of archived sticker sets";
  }

  // if 0 sticker sets are received, then set offset_sticker_set_id was found and there is no stickers after it
  // or it wasn't found and there is no archived sets at all
  bool is_last =
      sticker_sets.empty() && (!offset_sticker_set_id.is_valid() ||
                               (!sticker_set_ids.empty() && offset_sticker_set_id == sticker_set_ids.back()));

  total_archived_sticker_set_count_[is_masks] = total_count;
  for (auto &sticker_set_covered : sticker_sets) {
    auto sticker_set_id =
        on_get_sticker_set_covered(std::move(sticker_set_covered), false, "on_get_archived_sticker_sets");
    if (sticker_set_id.is_valid()) {
      auto sticker_set = get_sticker_set(sticker_set_id);
      CHECK(sticker_set != nullptr);
      update_sticker_set(sticker_set);

      if (!td::contains(sticker_set_ids, sticker_set_id)) {
        sticker_set_ids.push_back(sticker_set_id);
      }
    }
  }
  if (sticker_set_ids.size() >= static_cast<size_t>(total_count) || is_last) {
    if (sticker_set_ids.size() != static_cast<size_t>(total_count)) {
      LOG(ERROR) << "Expected total of " << total_count << " archived sticker sets, but " << sticker_set_ids.size()
                 << " found";
      total_archived_sticker_set_count_[is_masks] = static_cast<int32>(sticker_set_ids.size());
    }
    sticker_set_ids.push_back(StickerSetId());
  }
  send_update_installed_sticker_sets();
}

std::pair<int32, vector<StickerSetId>> StickersManager::get_featured_sticker_sets(int32 offset, int32 limit,
                                                                                  Promise<Unit> &&promise) {
  if (offset < 0) {
    promise.set_error(Status::Error(3, "Parameter offset must be non-negative"));
    return {};
  }

  if (limit < 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be non-negative"));
    return {};
  }
  if (limit == 0) {
    offset = 0;
  }

  if (!are_featured_sticker_sets_loaded_) {
    load_featured_sticker_sets(std::move(promise));
    return {};
  }
  reload_featured_sticker_sets(false);

  auto set_count = static_cast<int32>(featured_sticker_set_ids_.size());
  auto total_count = set_count + (old_featured_sticker_set_count_ == -1 ? 1 : old_featured_sticker_set_count_);
  if (offset < set_count) {
    if (limit > set_count - offset) {
      limit = set_count - offset;
    }
    promise.set_value(Unit());
    auto begin = featured_sticker_set_ids_.begin() + offset;
    return {total_count, {begin, begin + limit}};
  }

  if (offset == set_count && are_old_featured_sticker_sets_invalidated_) {
    invalidate_old_featured_sticker_sets();
  }

  if (offset < total_count || old_featured_sticker_set_count_ == -1) {
    offset -= set_count;
    set_count = static_cast<int32>(old_featured_sticker_set_ids_.size());
    if (offset < set_count) {
      if (limit > set_count - offset) {
        limit = set_count - offset;
      }
      promise.set_value(Unit());
      auto begin = old_featured_sticker_set_ids_.begin() + offset;
      return {total_count, {begin, begin + limit}};
    }
    if (offset > set_count) {
      promise.set_error(
          Status::Error(400, "Too big offset specified; trending sticker sets can be received only consequently"));
      return {};
    }

    load_old_featured_sticker_sets(std::move(promise));
    return {};
  }

  promise.set_value(Unit());
  return {total_count, vector<StickerSetId>()};
}

void StickersManager::on_old_featured_sticker_sets_invalidated() {
  LOG(INFO) << "Invalidate old trending sticker sets";
  are_old_featured_sticker_sets_invalidated_ = true;

  if (!G()->parameters().use_file_db) {
    return;
  }

  G()->td_db()->get_binlog_pmc()->set("invalidate_old_featured_sticker_sets", "1");
}

void StickersManager::invalidate_old_featured_sticker_sets() {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Invalidate old featured sticker sets";
  if (G()->parameters().use_file_db) {
    G()->td_db()->get_binlog_pmc()->erase("invalidate_old_featured_sticker_sets");
    G()->td_db()->get_sqlite_pmc()->erase_by_prefix("sssoldfeatured", Auto());
  }
  are_old_featured_sticker_sets_invalidated_ = false;
  old_featured_sticker_set_ids_.clear();

  old_featured_sticker_set_generation_++;
  auto promises = std::move(load_old_featured_sticker_sets_queries_);
  load_old_featured_sticker_sets_queries_.clear();
  for (auto &promise : promises) {
    promise.set_error(Status::Error(400, "Trending sticker sets was updated"));
  }
}

void StickersManager::set_old_featured_sticker_set_count(int32 count) {
  if (old_featured_sticker_set_count_ == count) {
    return;
  }

  on_old_featured_sticker_sets_invalidated();

  old_featured_sticker_set_count_ = count;
  need_update_featured_sticker_sets_ = true;

  if (!G()->parameters().use_file_db) {
    return;
  }

  LOG(INFO) << "Save old trending sticker set count " << count << " to binlog";
  G()->td_db()->get_binlog_pmc()->set("old_featured_sticker_set_count", to_string(count));
}

void StickersManager::fix_old_featured_sticker_set_count() {
  auto known_count = static_cast<int32>(old_featured_sticker_set_ids_.size());
  if (old_featured_sticker_set_count_ < known_count) {
    if (old_featured_sticker_set_count_ >= 0) {
      LOG(ERROR) << "Have old trending sticker set count " << old_featured_sticker_set_count_ << ", but have "
                 << known_count << " old trending sticker sets";
    }
    set_old_featured_sticker_set_count(known_count);
  }
  if (old_featured_sticker_set_count_ > known_count && known_count % OLD_FEATURED_STICKER_SET_SLICE_SIZE != 0) {
    LOG(ERROR) << "Have " << known_count << " old sticker sets out of " << old_featured_sticker_set_count_;
    set_old_featured_sticker_set_count(known_count);
  }
}

void StickersManager::on_get_featured_sticker_sets(
    int32 offset, int32 limit, uint32 generation,
    tl_object_ptr<telegram_api::messages_FeaturedStickers> &&sticker_sets_ptr) {
  if (offset < 0) {
    next_featured_sticker_sets_load_time_ = Time::now_cached() + Random::fast(30 * 60, 50 * 60);
  }

  int32 constructor_id = sticker_sets_ptr->get_id();
  if (constructor_id == telegram_api::messages_featuredStickersNotModified::ID) {
    LOG(INFO) << "Trending sticker sets are not modified";
    auto *stickers = static_cast<const telegram_api::messages_featuredStickersNotModified *>(sticker_sets_ptr.get());
    if (offset >= 0 && generation == old_featured_sticker_set_generation_) {
      set_old_featured_sticker_set_count(stickers->count_);
      fix_old_featured_sticker_set_count();
    }
    send_update_featured_sticker_sets();
    return;
  }
  CHECK(constructor_id == telegram_api::messages_featuredStickers::ID);
  auto featured_stickers = move_tl_object_as<telegram_api::messages_featuredStickers>(sticker_sets_ptr);

  if (offset >= 0 && generation == old_featured_sticker_set_generation_) {
    set_old_featured_sticker_set_count(featured_stickers->count_);
    // the count will be fixed in on_load_old_featured_sticker_sets_finished
  }

  std::unordered_set<StickerSetId, StickerSetIdHash> unread_sticker_set_ids;
  for (auto &unread_sticker_set_id : featured_stickers->unread_) {
    unread_sticker_set_ids.insert(StickerSetId(unread_sticker_set_id));
  }

  vector<StickerSetId> featured_sticker_set_ids;
  for (auto &sticker_set : featured_stickers->sets_) {
    StickerSetId set_id = on_get_sticker_set_covered(std::move(sticker_set), true, "on_get_featured_sticker_sets");
    if (!set_id.is_valid()) {
      continue;
    }

    auto set = get_sticker_set(set_id);
    CHECK(set != nullptr);
    bool is_viewed = unread_sticker_set_ids.count(set_id) == 0;
    if (is_viewed != set->is_viewed) {
      set->is_viewed = is_viewed;
      set->is_changed = true;
    }

    update_sticker_set(set);

    featured_sticker_set_ids.push_back(set_id);
  }

  send_update_installed_sticker_sets();

  if (offset >= 0) {
    if (generation == old_featured_sticker_set_generation_) {
      if (G()->parameters().use_file_db && !G()->close_flag()) {
        LOG(INFO) << "Save old trending sticker sets to database with offset " << old_featured_sticker_set_ids_.size();
        CHECK(old_featured_sticker_set_ids_.size() % OLD_FEATURED_STICKER_SET_SLICE_SIZE == 0);
        StickerSetListLogEvent log_event(featured_sticker_set_ids);
        G()->td_db()->get_sqlite_pmc()->set(PSTRING() << "sssoldfeatured" << old_featured_sticker_set_ids_.size(),
                                            log_event_store(log_event).as_slice().str(), Auto());
      }
      on_load_old_featured_sticker_sets_finished(generation, std::move(featured_sticker_set_ids));
    }

    send_update_featured_sticker_sets();  // because of changed count
    return;
  }

  on_load_featured_sticker_sets_finished(std::move(featured_sticker_set_ids));

  LOG_IF(ERROR, featured_sticker_sets_hash_ != featured_stickers->hash_) << "Trending sticker sets hash mismatch";

  if (!G()->parameters().use_file_db || G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Save trending sticker sets to database";
  StickerSetListLogEvent log_event(featured_sticker_set_ids_);
  G()->td_db()->get_sqlite_pmc()->set("sssfeatured", log_event_store(log_event).as_slice().str(), Auto());
}

void StickersManager::on_get_featured_sticker_sets_failed(int32 offset, int32 limit, uint32 generation, Status error) {
  CHECK(error.is_error());
  vector<Promise<Unit>> promises;
  if (offset >= 0) {
    if (generation != old_featured_sticker_set_generation_) {
      return;
    }
    promises = std::move(load_old_featured_sticker_sets_queries_);
    load_old_featured_sticker_sets_queries_.clear();
  } else {
    next_featured_sticker_sets_load_time_ = Time::now_cached() + Random::fast(5, 10);
    promises = std::move(load_featured_sticker_sets_queries_);
    load_featured_sticker_sets_queries_.clear();
  }

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

void StickersManager::load_featured_sticker_sets(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_featured_sticker_sets_loaded_ = true;
    old_featured_sticker_set_count_ = 0;
  }
  if (are_featured_sticker_sets_loaded_) {
    promise.set_value(Unit());
    return;
  }
  load_featured_sticker_sets_queries_.push_back(std::move(promise));
  if (load_featured_sticker_sets_queries_.size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load trending sticker sets from database";
      G()->td_db()->get_sqlite_pmc()->get("sssfeatured", PromiseCreator::lambda([](string value) {
                                            send_closure(G()->stickers_manager(),
                                                         &StickersManager::on_load_featured_sticker_sets_from_database,
                                                         std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load trending sticker sets from server";
      reload_featured_sticker_sets(true);
    }
  }
}

void StickersManager::on_load_featured_sticker_sets_from_database(string value) {
  if (G()->close_flag()) {
    return;
  }
  if (value.empty()) {
    LOG(INFO) << "Trending sticker sets aren't found in database";
    reload_featured_sticker_sets(true);
    return;
  }

  LOG(INFO) << "Successfully loaded trending sticker set list of size " << value.size() << " from database";

  StickerSetListLogEvent log_event;
  auto status = log_event_parse(log_event, value);
  if (status.is_error()) {
    // can't happen unless database is broken
    LOG(ERROR) << "Can't load trending sticker set list: " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    return reload_featured_sticker_sets(true);
  }

  vector<StickerSetId> sets_to_load;
  for (auto sticker_set_id : log_event.sticker_set_ids) {
    StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    if (!sticker_set->is_inited) {
      sets_to_load.push_back(sticker_set_id);
    }
  }

  load_sticker_sets_without_stickers(
      std::move(sets_to_load),
      PromiseCreator::lambda([sticker_set_ids = std::move(log_event.sticker_set_ids)](Result<> result) mutable {
        if (result.is_ok()) {
          send_closure(G()->stickers_manager(), &StickersManager::on_load_featured_sticker_sets_finished,
                       std::move(sticker_set_ids));
        } else {
          send_closure(G()->stickers_manager(), &StickersManager::reload_featured_sticker_sets, true);
        }
      }));
}

void StickersManager::on_load_featured_sticker_sets_finished(vector<StickerSetId> &&featured_sticker_set_ids) {
  if (!featured_sticker_set_ids_.empty() && featured_sticker_set_ids != featured_sticker_set_ids_) {
    // always invalidate old featured sticker sets when current featured sticker sets change
    on_old_featured_sticker_sets_invalidated();
  }
  featured_sticker_set_ids_ = std::move(featured_sticker_set_ids);
  are_featured_sticker_sets_loaded_ = true;
  need_update_featured_sticker_sets_ = true;
  send_update_featured_sticker_sets();
  auto promises = std::move(load_featured_sticker_sets_queries_);
  load_featured_sticker_sets_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void StickersManager::load_old_featured_sticker_sets(Promise<Unit> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(old_featured_sticker_set_ids_.size() % OLD_FEATURED_STICKER_SET_SLICE_SIZE == 0);
  load_old_featured_sticker_sets_queries_.push_back(std::move(promise));
  if (load_old_featured_sticker_sets_queries_.size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load old trending sticker sets from database with offset "
                << old_featured_sticker_set_ids_.size();
      G()->td_db()->get_sqlite_pmc()->get(
          PSTRING() << "sssoldfeatured" << old_featured_sticker_set_ids_.size(),
          PromiseCreator::lambda([generation = old_featured_sticker_set_generation_](string value) {
            send_closure(G()->stickers_manager(), &StickersManager::on_load_old_featured_sticker_sets_from_database,
                         generation, std::move(value));
          }));
    } else {
      LOG(INFO) << "Trying to load old trending sticker sets from server with offset "
                << old_featured_sticker_set_ids_.size();
      reload_old_featured_sticker_sets();
    }
  }
}

void StickersManager::on_load_old_featured_sticker_sets_from_database(uint32 generation, string value) {
  if (G()->close_flag()) {
    return;
  }
  if (generation != old_featured_sticker_set_generation_) {
    return;
  }
  if (value.empty()) {
    LOG(INFO) << "Old trending sticker sets aren't found in database";
    return reload_old_featured_sticker_sets();
  }

  LOG(INFO) << "Successfully loaded old trending sticker set list of size " << value.size()
            << " from database with offset " << old_featured_sticker_set_ids_.size();

  StickerSetListLogEvent log_event;
  auto status = log_event_parse(log_event, value);
  if (status.is_error()) {
    // can't happen unless database is broken
    LOG(ERROR) << "Can't load old trending sticker set list: " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    return reload_old_featured_sticker_sets();
  }

  vector<StickerSetId> sets_to_load;
  for (auto sticker_set_id : log_event.sticker_set_ids) {
    StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    if (!sticker_set->is_inited) {
      sets_to_load.push_back(sticker_set_id);
    }
  }

  load_sticker_sets_without_stickers(
      std::move(sets_to_load),
      PromiseCreator::lambda(
          [generation, sticker_set_ids = std::move(log_event.sticker_set_ids)](Result<> result) mutable {
            if (result.is_ok()) {
              send_closure(G()->stickers_manager(), &StickersManager::on_load_old_featured_sticker_sets_finished,
                           generation, std::move(sticker_set_ids));
            } else {
              send_closure(G()->stickers_manager(), &StickersManager::reload_old_featured_sticker_sets, generation);
            }
          }));
}

void StickersManager::on_load_old_featured_sticker_sets_finished(uint32 generation,
                                                                 vector<StickerSetId> &&featured_sticker_set_ids) {
  if (generation != old_featured_sticker_set_generation_) {
    fix_old_featured_sticker_set_count();  // must never be needed
    return;
  }
  append(old_featured_sticker_set_ids_, std::move(featured_sticker_set_ids));
  fix_old_featured_sticker_set_count();
  auto promises = std::move(load_old_featured_sticker_sets_queries_);
  load_old_featured_sticker_sets_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

vector<StickerSetId> StickersManager::get_attached_sticker_sets(FileId file_id, Promise<Unit> &&promise) {
  if (!file_id.is_valid()) {
    promise.set_error(Status::Error(5, "Wrong file_id specified"));
    return {};
  }

  auto it = attached_sticker_sets_.find(file_id);
  if (it != attached_sticker_sets_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  send_get_attached_stickers_query(file_id, std::move(promise));
  return {};
}

void StickersManager::send_get_attached_stickers_query(FileId file_id, Promise<Unit> &&promise) {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.empty()) {
    return promise.set_error(Status::Error(5, "File not found"));
  }
  if (!file_view.has_remote_location() ||
      (!file_view.remote_location().is_document() && !file_view.remote_location().is_photo()) ||
      file_view.remote_location().is_web()) {
    return promise.set_value(Unit());
  }

  tl_object_ptr<telegram_api::InputStickeredMedia> input_stickered_media;
  string file_reference;
  if (file_view.main_remote_location().is_photo()) {
    auto input_photo = file_view.main_remote_location().as_input_photo();
    file_reference = input_photo->file_reference_.as_slice().str();
    input_stickered_media = make_tl_object<telegram_api::inputStickeredMediaPhoto>(std::move(input_photo));
  } else {
    auto input_document = file_view.main_remote_location().as_input_document();
    file_reference = input_document->file_reference_.as_slice().str();
    input_stickered_media = make_tl_object<telegram_api::inputStickeredMediaDocument>(std::move(input_document));
  }

  td_->create_handler<GetAttachedStickerSetsQuery>(std::move(promise))
      ->send(file_id, std::move(file_reference), std::move(input_stickered_media));
}

void StickersManager::on_get_attached_sticker_sets(
    FileId file_id, vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets) {
  vector<StickerSetId> &sticker_set_ids = attached_sticker_sets_[file_id];
  sticker_set_ids.clear();
  for (auto &sticker_set_covered : sticker_sets) {
    auto sticker_set_id =
        on_get_sticker_set_covered(std::move(sticker_set_covered), true, "on_get_attached_sticker_sets");
    if (sticker_set_id.is_valid()) {
      auto sticker_set = get_sticker_set(sticker_set_id);
      CHECK(sticker_set != nullptr);
      update_sticker_set(sticker_set);

      sticker_set_ids.push_back(sticker_set_id);
    }
  }
  send_update_installed_sticker_sets();
}

// -1 - order can't be applied, because some sticker sets aren't loaded or aren't installed,
// 0 - order wasn't changed, 1 - order was partly replaced by the new order, 2 - order was replaced by the new order
int StickersManager::apply_installed_sticker_sets_order(bool is_masks, const vector<StickerSetId> &sticker_set_ids) {
  if (!are_installed_sticker_sets_loaded_[is_masks]) {
    return -1;
  }

  vector<StickerSetId> &current_sticker_set_ids = installed_sticker_set_ids_[is_masks];
  if (sticker_set_ids == current_sticker_set_ids) {
    return 0;
  }

  std::unordered_set<StickerSetId, StickerSetIdHash> valid_set_ids(current_sticker_set_ids.begin(),
                                                                   current_sticker_set_ids.end());
  vector<StickerSetId> new_sticker_set_ids;
  for (auto sticker_set_id : sticker_set_ids) {
    auto it = valid_set_ids.find(sticker_set_id);
    if (it != valid_set_ids.end()) {
      new_sticker_set_ids.push_back(sticker_set_id);
      valid_set_ids.erase(it);
    } else {
      return -1;
    }
  }
  if (new_sticker_set_ids.empty()) {
    return 0;
  }
  if (!valid_set_ids.empty()) {
    vector<StickerSetId> missed_sticker_set_ids;
    for (auto sticker_set_id : current_sticker_set_ids) {
      auto it = valid_set_ids.find(sticker_set_id);
      if (it != valid_set_ids.end()) {
        missed_sticker_set_ids.push_back(sticker_set_id);
        valid_set_ids.erase(it);
      }
    }
    append(missed_sticker_set_ids, new_sticker_set_ids);
    new_sticker_set_ids = std::move(missed_sticker_set_ids);
  }
  CHECK(valid_set_ids.empty());

  if (new_sticker_set_ids == current_sticker_set_ids) {
    return 0;
  }
  current_sticker_set_ids = std::move(new_sticker_set_ids);

  need_update_installed_sticker_sets_[is_masks] = true;
  if (sticker_set_ids != current_sticker_set_ids) {
    return 1;
  }
  return 2;
}

void StickersManager::on_update_sticker_sets_order(bool is_masks, const vector<StickerSetId> &sticker_set_ids) {
  int result = apply_installed_sticker_sets_order(is_masks, sticker_set_ids);
  if (result < 0) {
    return reload_installed_sticker_sets(is_masks, true);
  }
  if (result > 0) {
    send_update_installed_sticker_sets();
  }
}

void StickersManager::reorder_installed_sticker_sets(bool is_masks, const vector<StickerSetId> &sticker_set_ids,
                                                     Promise<Unit> &&promise) {
  auto result = apply_installed_sticker_sets_order(is_masks, sticker_set_ids);
  if (result < 0) {
    return promise.set_error(Status::Error(400, "Wrong sticker set list"));
  }
  if (result > 0) {
    td_->create_handler<ReorderStickerSetsQuery>()->send(is_masks, installed_sticker_set_ids_[is_masks]);
    send_update_installed_sticker_sets();
  }
  promise.set_value(Unit());
}

string &StickersManager::get_input_sticker_emojis(td_api::InputSticker *sticker) {
  CHECK(sticker != nullptr);
  auto constructor_id = sticker->get_id();
  if (constructor_id == td_api::inputStickerStatic::ID) {
    return static_cast<td_api::inputStickerStatic *>(sticker)->emojis_;
  }
  CHECK(constructor_id == td_api::inputStickerAnimated::ID);
  return static_cast<td_api::inputStickerAnimated *>(sticker)->emojis_;
}

Result<std::tuple<FileId, bool, bool, bool>> StickersManager::prepare_input_sticker(td_api::InputSticker *sticker) {
  if (sticker == nullptr) {
    return Status::Error(3, "Input sticker must be non-empty");
  }

  if (!clean_input_string(get_input_sticker_emojis(sticker))) {
    return Status::Error(400, "Emojis must be encoded in UTF-8");
  }

  switch (sticker->get_id()) {
    case td_api::inputStickerStatic::ID:
      return prepare_input_file(static_cast<td_api::inputStickerStatic *>(sticker)->sticker_, false, false);
    case td_api::inputStickerAnimated::ID:
      return prepare_input_file(static_cast<td_api::inputStickerAnimated *>(sticker)->sticker_, true, false);
    default:
      UNREACHABLE();
      return {};
  }
}

Result<std::tuple<FileId, bool, bool, bool>> StickersManager::prepare_input_file(
    const tl_object_ptr<td_api::InputFile> &input_file, bool is_animated, bool for_thumbnail) {
  auto r_file_id = td_->file_manager_->get_input_file_id(is_animated ? FileType::Sticker : FileType::Document,
                                                         input_file, DialogId(), for_thumbnail, false);
  if (r_file_id.is_error()) {
    return Status::Error(7, r_file_id.error().message());
  }
  auto file_id = r_file_id.move_as_ok();
  if (file_id.empty()) {
    return std::make_tuple(FileId(), false, false, false);
  }

  if (is_animated) {
    int32 width = for_thumbnail ? 100 : 512;
    create_sticker(file_id, string(), PhotoSize(), get_dimensions(width, width, "prepare_input_file"), nullptr, true,
                   nullptr);
  } else {
    td_->documents_manager_->create_document(file_id, string(), PhotoSize(), "sticker.png", "image/png", false);
  }

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return Status::Error(400, "Can't use encrypted file");
  }

  if (file_view.has_remote_location() && file_view.main_remote_location().is_web()) {
    return Status::Error(400, "Can't use web file to create a sticker");
  }
  bool is_url = false;
  bool is_local = false;
  if (file_view.has_remote_location()) {
    CHECK(file_view.main_remote_location().is_document());
  } else {
    if (file_view.has_url()) {
      is_url = true;
    } else {
      auto max_file_size = [&] {
        if (for_thumbnail) {
          return is_animated ? MAX_ANIMATED_THUMBNAIL_FILE_SIZE : MAX_THUMBNAIL_FILE_SIZE;
        } else {
          return is_animated ? MAX_ANIMATED_STICKER_FILE_SIZE : MAX_STICKER_FILE_SIZE;
        }
      }();
      if (file_view.has_local_location() && file_view.expected_size() > max_file_size) {
        return Status::Error(400, "File is too big");
      }
      is_local = true;
    }
  }
  return std::make_tuple(file_id, is_url, is_local, is_animated);
}

FileId StickersManager::upload_sticker_file(UserId user_id, const tl_object_ptr<td_api::InputFile> &sticker,
                                            Promise<Unit> &&promise) {
  auto input_user = td_->contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    promise.set_error(Status::Error(3, "User not found"));
    return FileId();
  }
  DialogId dialog_id(user_id);
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
  if (input_peer == nullptr) {
    promise.set_error(Status::Error(3, "Have no access to the user"));
    return FileId();
  }

  auto r_file_id = prepare_input_file(sticker, false, false);
  if (r_file_id.is_error()) {
    promise.set_error(r_file_id.move_as_error());
    return FileId();
  }
  auto file_id = std::get<0>(r_file_id.ok());
  auto is_url = std::get<1>(r_file_id.ok());
  auto is_local = std::get<2>(r_file_id.ok());

  if (is_url) {
    do_upload_sticker_file(user_id, file_id, nullptr, std::move(promise));
  } else if (is_local) {
    upload_sticker_file(user_id, file_id, std::move(promise));
  } else {
    promise.set_value(Unit());
  }

  return file_id;
}

tl_object_ptr<telegram_api::inputStickerSetItem> StickersManager::get_input_sticker(td_api::InputSticker *sticker,
                                                                                    FileId file_id) const {
  CHECK(sticker != nullptr);
  FileView file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(file_view.has_remote_location());
  auto input_document = file_view.main_remote_location().as_input_document();

  tl_object_ptr<telegram_api::maskCoords> mask_coords;
  if (sticker->get_id() == td_api::inputStickerStatic::ID) {
    auto mask_position = static_cast<td_api::inputStickerStatic *>(sticker)->mask_position_.get();
    if (mask_position != nullptr && mask_position->point_ != nullptr) {
      auto point = [mask_point = std::move(mask_position->point_)] {
        switch (mask_point->get_id()) {
          case td_api::maskPointForehead::ID:
            return 0;
          case td_api::maskPointEyes::ID:
            return 1;
          case td_api::maskPointMouth::ID:
            return 2;
          case td_api::maskPointChin::ID:
            return 3;
          default:
            UNREACHABLE();
            return -1;
        }
      }();
      mask_coords = make_tl_object<telegram_api::maskCoords>(point, mask_position->x_shift_, mask_position->y_shift_,
                                                             mask_position->scale_);
    }
  }

  int32 flags = 0;
  if (mask_coords != nullptr) {
    flags |= telegram_api::inputStickerSetItem::MASK_COORDS_MASK;
  }

  return make_tl_object<telegram_api::inputStickerSetItem>(flags, std::move(input_document),
                                                           get_input_sticker_emojis(sticker), std::move(mask_coords));
}

void StickersManager::create_new_sticker_set(UserId user_id, string &title, string &short_name, bool is_masks,
                                             vector<tl_object_ptr<td_api::InputSticker>> &&stickers,
                                             Promise<Unit> &&promise) {
  auto input_user = td_->contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }
  DialogId dialog_id(user_id);
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(3, "Have no access to the user"));
  }

  title = strip_empty_characters(title, MAX_STICKER_SET_TITLE_LENGTH);
  if (title.empty()) {
    return promise.set_error(Status::Error(3, "Sticker set title can't be empty"));
  }

  short_name = strip_empty_characters(short_name, MAX_STICKER_SET_SHORT_NAME_LENGTH);
  if (short_name.empty()) {
    return promise.set_error(Status::Error(3, "Sticker set name can't be empty"));
  }

  if (stickers.empty()) {
    return promise.set_error(Status::Error(400, "At least 1 sticker must be specified"));
  }

  vector<FileId> file_ids;
  file_ids.reserve(stickers.size());
  vector<FileId> local_file_ids;
  vector<FileId> url_file_ids;
  size_t animated_sticker_count = 0;
  for (auto &sticker : stickers) {
    auto r_file_id = prepare_input_sticker(sticker.get());
    if (r_file_id.is_error()) {
      return promise.set_error(r_file_id.move_as_error());
    }
    auto file_id = std::get<0>(r_file_id.ok());
    auto is_url = std::get<1>(r_file_id.ok());
    auto is_local = std::get<2>(r_file_id.ok());
    auto is_animated = std::get<3>(r_file_id.ok());
    if (is_animated) {
      animated_sticker_count++;
      if (is_url) {
        return promise.set_error(Status::Error(400, "Animated stickers can't be uploaded by URL"));
      }
    }

    file_ids.push_back(file_id);
    if (is_url) {
      url_file_ids.push_back(file_id);
    } else if (is_local) {
      local_file_ids.push_back(file_id);
    }
  }
  if (animated_sticker_count != stickers.size() && animated_sticker_count != 0) {
    return promise.set_error(Status::Error(400, "All stickers must be either animated or static"));
  }
  bool is_animated = animated_sticker_count == stickers.size();

  auto pending_new_sticker_set = make_unique<PendingNewStickerSet>();
  pending_new_sticker_set->user_id = user_id;
  pending_new_sticker_set->title = std::move(title);
  pending_new_sticker_set->short_name = short_name;
  pending_new_sticker_set->is_masks = is_masks;
  pending_new_sticker_set->is_animated = is_animated;
  pending_new_sticker_set->file_ids = std::move(file_ids);
  pending_new_sticker_set->stickers = std::move(stickers);
  pending_new_sticker_set->promise = std::move(promise);

  auto &multipromise = pending_new_sticker_set->upload_files_multipromise;

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || pending_new_sticker_sets_.find(random_id) != pending_new_sticker_sets_.end());
  pending_new_sticker_sets_[random_id] = std::move(pending_new_sticker_set);

  multipromise.add_promise(PromiseCreator::lambda([random_id](Result<Unit> result) {
    send_closure_later(G()->stickers_manager(), &StickersManager::on_new_stickers_uploaded, random_id,
                       std::move(result));
  }));
  auto lock_promise = multipromise.get_promise();

  for (auto file_id : url_file_ids) {
    do_upload_sticker_file(user_id, file_id, nullptr, multipromise.get_promise());
  }

  for (auto file_id : local_file_ids) {
    upload_sticker_file(user_id, file_id, multipromise.get_promise());
  }

  lock_promise.set_value(Unit());
}

void StickersManager::upload_sticker_file(UserId user_id, FileId file_id, Promise<Unit> &&promise) {
  FileId upload_file_id;
  if (td_->file_manager_->get_file_view(file_id).get_type() == FileType::Sticker) {
    CHECK(get_input_media(file_id, nullptr, nullptr, string()) == nullptr);
    upload_file_id = dup_sticker(td_->file_manager_->dup_file_id(file_id), file_id);
  } else {
    CHECK(td_->documents_manager_->get_input_media(file_id, nullptr, nullptr) == nullptr);
    upload_file_id = td_->documents_manager_->dup_document(td_->file_manager_->dup_file_id(file_id), file_id);
  }

  being_uploaded_files_[upload_file_id] = {user_id, std::move(promise)};
  LOG(INFO) << "Ask to upload sticker file " << upload_file_id;
  td_->file_manager_->upload(upload_file_id, upload_sticker_file_callback_, 2, 0);
}

void StickersManager::on_upload_sticker_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  LOG(INFO) << "Sticker file " << file_id << " has been uploaded";

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());

  auto user_id = it->second.first;
  auto promise = std::move(it->second.second);

  being_uploaded_files_.erase(it);

  do_upload_sticker_file(user_id, file_id, std::move(input_file), std::move(promise));
}

void StickersManager::on_upload_sticker_file_error(FileId file_id, Status status) {
  if (G()->close_flag()) {
    // do not fail upload if closing
    return;
  }

  LOG(WARNING) << "Sticker file " << file_id << " has upload error " << status;
  CHECK(status.is_error());

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());

  auto promise = std::move(it->second.second);

  being_uploaded_files_.erase(it);

  // TODO FILE_PART_X_MISSING support

  promise.set_error(Status::Error(status.code() > 0 ? status.code() : 500,
                                  status.message()));  // TODO CHECK that status has always a code
}

void StickersManager::do_upload_sticker_file(UserId user_id, FileId file_id,
                                             tl_object_ptr<telegram_api::InputFile> &&input_file,
                                             Promise<Unit> &&promise) {
  DialogId dialog_id(user_id);
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(3, "Have no access to the user"));
  }

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  bool is_animated = file_view.get_type() == FileType::Sticker;

  bool had_input_file = input_file != nullptr;
  auto input_media = is_animated ? get_input_media(file_id, std::move(input_file), nullptr, string())
                                 : td_->documents_manager_->get_input_media(file_id, std::move(input_file), nullptr);
  CHECK(input_media != nullptr);
  if (had_input_file && !FileManager::extract_was_uploaded(input_media)) {
    // if we had InputFile, but has failed to use it, then we need to immediately cancel file upload
    // so the next upload with the same file can succeed
    td_->file_manager_->cancel_upload(file_id);
  }

  td_->create_handler<UploadStickerFileQuery>(std::move(promise))
      ->send(std::move(input_peer), file_id, std::move(input_media));
}

void StickersManager::on_uploaded_sticker_file(FileId file_id, tl_object_ptr<telegram_api::MessageMedia> media,
                                               Promise<Unit> &&promise) {
  CHECK(media != nullptr);
  LOG(INFO) << "Receive uploaded sticker file " << to_string(media);
  if (media->get_id() != telegram_api::messageMediaDocument::ID) {
    return promise.set_error(Status::Error(400, "Can't upload sticker file: wrong file type"));
  }

  auto message_document = move_tl_object_as<telegram_api::messageMediaDocument>(media);
  auto document_ptr = std::move(message_document->document_);
  int32 document_id = document_ptr->get_id();
  if (document_id == telegram_api::documentEmpty::ID) {
    return promise.set_error(Status::Error(400, "Can't upload sticker file: empty file"));
  }
  CHECK(document_id == telegram_api::document::ID);

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  bool is_animated = file_view.get_type() == FileType::Sticker;
  auto expected_document_type = is_animated ? Document::Type::Sticker : Document::Type::General;

  auto parsed_document = td_->documents_manager_->on_get_document(
      move_tl_object_as<telegram_api::document>(document_ptr), DialogId(), nullptr);
  if (parsed_document.type != expected_document_type) {
    return promise.set_error(Status::Error(400, "Wrong file type"));
  }

  if (is_animated) {
    merge_stickers(parsed_document.file_id, file_id, true);
  } else {
    td_->documents_manager_->merge_documents(parsed_document.file_id, file_id, true);
  }
  promise.set_value(Unit());
}

void StickersManager::on_new_stickers_uploaded(int64 random_id, Result<Unit> result) {
  auto it = pending_new_sticker_sets_.find(random_id);
  CHECK(it != pending_new_sticker_sets_.end());

  auto pending_new_sticker_set = std::move(it->second);
  CHECK(pending_new_sticker_set != nullptr);

  pending_new_sticker_sets_.erase(it);

  if (result.is_error()) {
    pending_new_sticker_set->promise.set_error(result.move_as_error());
    return;
  }

  CHECK(pending_new_sticker_set->upload_files_multipromise.promise_count() == 0);

  auto input_user = td_->contacts_manager_->get_input_user(pending_new_sticker_set->user_id);
  if (input_user == nullptr) {
    return pending_new_sticker_set->promise.set_error(Status::Error(3, "User not found"));
  }

  bool is_masks = pending_new_sticker_set->is_masks;
  bool is_animated = pending_new_sticker_set->is_animated;

  auto sticker_count = pending_new_sticker_set->stickers.size();
  vector<tl_object_ptr<telegram_api::inputStickerSetItem>> input_stickers;
  input_stickers.reserve(sticker_count);
  for (size_t i = 0; i < sticker_count; i++) {
    input_stickers.push_back(
        get_input_sticker(pending_new_sticker_set->stickers[i].get(), pending_new_sticker_set->file_ids[i]));
  }

  td_->create_handler<CreateNewStickerSetQuery>(std::move(pending_new_sticker_set->promise))
      ->send(std::move(input_user), pending_new_sticker_set->title, pending_new_sticker_set->short_name, is_masks,
             is_animated, std::move(input_stickers));
}

void StickersManager::add_sticker_to_set(UserId user_id, string &short_name,
                                         tl_object_ptr<td_api::InputSticker> &&sticker, Promise<Unit> &&promise) {
  auto input_user = td_->contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }
  DialogId dialog_id(user_id);
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(3, "Have no access to the user"));
  }

  short_name = strip_empty_characters(short_name, MAX_STICKER_SET_SHORT_NAME_LENGTH);
  if (short_name.empty()) {
    return promise.set_error(Status::Error(3, "Sticker set name can't be empty"));
  }

  auto r_file_id = prepare_input_sticker(sticker.get());
  if (r_file_id.is_error()) {
    return promise.set_error(r_file_id.move_as_error());
  }
  auto file_id = std::get<0>(r_file_id.ok());
  auto is_url = std::get<1>(r_file_id.ok());
  auto is_local = std::get<2>(r_file_id.ok());

  auto pending_add_sticker_to_set = make_unique<PendingAddStickerToSet>();
  pending_add_sticker_to_set->short_name = short_name;
  pending_add_sticker_to_set->file_id = file_id;
  pending_add_sticker_to_set->sticker = std::move(sticker);
  pending_add_sticker_to_set->promise = std::move(promise);

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || pending_add_sticker_to_sets_.find(random_id) != pending_add_sticker_to_sets_.end());
  pending_add_sticker_to_sets_[random_id] = std::move(pending_add_sticker_to_set);

  auto on_upload_promise = PromiseCreator::lambda([random_id](Result<Unit> result) {
    send_closure(G()->stickers_manager(), &StickersManager::on_added_sticker_uploaded, random_id, std::move(result));
  });

  if (is_url) {
    do_upload_sticker_file(user_id, file_id, nullptr, std::move(on_upload_promise));
  } else if (is_local) {
    upload_sticker_file(user_id, file_id, std::move(on_upload_promise));
  } else {
    on_upload_promise.set_value(Unit());
  }
}

void StickersManager::on_added_sticker_uploaded(int64 random_id, Result<Unit> result) {
  auto it = pending_add_sticker_to_sets_.find(random_id);
  CHECK(it != pending_add_sticker_to_sets_.end());

  auto pending_add_sticker_to_set = std::move(it->second);
  CHECK(pending_add_sticker_to_set != nullptr);

  pending_add_sticker_to_sets_.erase(it);

  if (result.is_error()) {
    pending_add_sticker_to_set->promise.set_error(result.move_as_error());
    return;
  }

  td_->create_handler<AddStickerToSetQuery>(std::move(pending_add_sticker_to_set->promise))
      ->send(pending_add_sticker_to_set->short_name,
             get_input_sticker(pending_add_sticker_to_set->sticker.get(), pending_add_sticker_to_set->file_id));
}

void StickersManager::set_sticker_set_thumbnail(UserId user_id, string &short_name,
                                                tl_object_ptr<td_api::InputFile> &&thumbnail, Promise<Unit> &&promise) {
  auto input_user = td_->contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }
  DialogId dialog_id(user_id);
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(3, "Have no access to the user"));
  }

  short_name = clean_username(strip_empty_characters(short_name, MAX_STICKER_SET_SHORT_NAME_LENGTH));
  if (short_name.empty()) {
    return promise.set_error(Status::Error(3, "Sticker set name can't be empty"));
  }

  auto it = short_name_to_sticker_set_id_.find(short_name);
  const StickerSet *sticker_set = it == short_name_to_sticker_set_id_.end() ? nullptr : get_sticker_set(it->second);
  if (sticker_set != nullptr && sticker_set->was_loaded) {
    return do_set_sticker_set_thumbnail(user_id, short_name, std::move(thumbnail), std::move(promise));
  }

  do_reload_sticker_set(
      StickerSetId(), make_tl_object<telegram_api::inputStickerSetShortName>(short_name),
      PromiseCreator::lambda([actor_id = actor_id(this), user_id, short_name, thumbnail = std::move(thumbnail),
                              promise = std::move(promise)](Result<Unit> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure(actor_id, &StickersManager::do_set_sticker_set_thumbnail, user_id, std::move(short_name),
                       std::move(thumbnail), std::move(promise));
        }
      }));
}

void StickersManager::do_set_sticker_set_thumbnail(UserId user_id, string short_name,
                                                   tl_object_ptr<td_api::InputFile> &&thumbnail,
                                                   Promise<Unit> &&promise) {
  auto it = short_name_to_sticker_set_id_.find(short_name);
  const StickerSet *sticker_set = it == short_name_to_sticker_set_id_.end() ? nullptr : get_sticker_set(it->second);
  if (sticker_set == nullptr || !sticker_set->was_loaded) {
    return promise.set_error(Status::Error(3, "Sticker set not found"));
  }

  auto r_file_id = prepare_input_file(thumbnail, sticker_set->is_animated, true);
  if (r_file_id.is_error()) {
    return promise.set_error(r_file_id.move_as_error());
  }
  auto file_id = std::get<0>(r_file_id.ok());
  auto is_url = std::get<1>(r_file_id.ok());
  auto is_local = std::get<2>(r_file_id.ok());

  if (!file_id.is_valid()) {
    td_->create_handler<SetStickerSetThumbnailQuery>(std::move(promise))
        ->send(short_name, telegram_api::make_object<telegram_api::inputDocumentEmpty>());
    return;
  }

  auto pending_set_sticker_set_thumbnail = make_unique<PendingSetStickerSetThumbnail>();
  pending_set_sticker_set_thumbnail->short_name = short_name;
  pending_set_sticker_set_thumbnail->file_id = file_id;
  pending_set_sticker_set_thumbnail->promise = std::move(promise);

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 ||
           pending_set_sticker_set_thumbnails_.find(random_id) != pending_set_sticker_set_thumbnails_.end());
  pending_set_sticker_set_thumbnails_[random_id] = std::move(pending_set_sticker_set_thumbnail);

  auto on_upload_promise = PromiseCreator::lambda([random_id](Result<Unit> result) {
    send_closure(G()->stickers_manager(), &StickersManager::on_sticker_set_thumbnail_uploaded, random_id,
                 std::move(result));
  });

  if (is_url) {
    do_upload_sticker_file(user_id, file_id, nullptr, std::move(on_upload_promise));
  } else if (is_local) {
    upload_sticker_file(user_id, file_id, std::move(on_upload_promise));
  } else {
    on_upload_promise.set_value(Unit());
  }
}

void StickersManager::on_sticker_set_thumbnail_uploaded(int64 random_id, Result<Unit> result) {
  auto it = pending_set_sticker_set_thumbnails_.find(random_id);
  CHECK(it != pending_set_sticker_set_thumbnails_.end());

  auto pending_set_sticker_set_thumbnail = std::move(it->second);
  CHECK(pending_set_sticker_set_thumbnail != nullptr);

  pending_set_sticker_set_thumbnails_.erase(it);

  if (result.is_error()) {
    pending_set_sticker_set_thumbnail->promise.set_error(result.move_as_error());
    return;
  }

  FileView file_view = td_->file_manager_->get_file_view(pending_set_sticker_set_thumbnail->file_id);
  CHECK(file_view.has_remote_location());

  td_->create_handler<SetStickerSetThumbnailQuery>(std::move(pending_set_sticker_set_thumbnail->promise))
      ->send(pending_set_sticker_set_thumbnail->short_name, file_view.main_remote_location().as_input_document());
}

void StickersManager::set_sticker_position_in_set(const tl_object_ptr<td_api::InputFile> &sticker, int32 position,
                                                  Promise<Unit> &&promise) {
  if (position < 0) {
    return promise.set_error(Status::Error(7, "Wrong sticker position specified"));
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, sticker, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  auto file_id = r_file_id.move_as_ok();
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (!file_view.has_remote_location() || !file_view.main_remote_location().is_document() ||
      file_view.main_remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Wrong sticker file specified"));
  }

  td_->create_handler<SetStickerPositionQuery>(std::move(promise))
      ->send(file_view.main_remote_location().as_input_document(), position);
}

void StickersManager::remove_sticker_from_set(const tl_object_ptr<td_api::InputFile> &sticker,
                                              Promise<Unit> &&promise) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, sticker, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  auto file_id = r_file_id.move_as_ok();
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (!file_view.has_remote_location() || !file_view.main_remote_location().is_document() ||
      file_view.main_remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Wrong sticker file specified"));
  }

  td_->create_handler<DeleteStickerFromSetQuery>(std::move(promise))
      ->send(file_view.main_remote_location().as_input_document());
}

vector<FileId> StickersManager::get_attached_sticker_file_ids(const vector<int32> &int_file_ids) {
  vector<FileId> result;

  result.reserve(int_file_ids.size());
  for (auto int_file_id : int_file_ids) {
    FileId file_id(int_file_id, 0);
    const Sticker *s = get_sticker(file_id);
    if (s == nullptr) {
      LOG(WARNING) << "Can't find sticker " << file_id;
      continue;
    }
    if (!s->set_id.is_valid()) {
      // only stickers from sticker sets can be attached to files
      continue;
    }

    auto file_view = td_->file_manager_->get_file_view(file_id);
    CHECK(!file_view.empty());
    if (!file_view.has_remote_location()) {
      LOG(ERROR) << "Sticker " << file_id << " has no remote location";
      continue;
    }
    if (file_view.remote_location().is_web()) {
      LOG(ERROR) << "Sticker " << file_id << " is web";
      continue;
    }
    if (!file_view.remote_location().is_document()) {
      LOG(ERROR) << "Sticker " << file_id << " is encrypted";
      continue;
    }
    result.push_back(file_id);

    if (!td_->auth_manager_->is_bot()) {
      add_recent_sticker_by_id(true, file_id);
    }
  }

  return result;
}

int32 StickersManager::get_sticker_sets_hash(const vector<StickerSetId> &sticker_set_ids) const {
  vector<uint32> numbers;
  numbers.reserve(sticker_set_ids.size());
  for (auto sticker_set_id : sticker_set_ids) {
    const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    CHECK(sticker_set->is_inited);
    numbers.push_back(static_cast<uint32>(sticker_set->hash));
  }
  return get_vector_hash(numbers);
}

int32 StickersManager::get_featured_sticker_sets_hash() const {
  vector<uint32> numbers;
  numbers.reserve(featured_sticker_set_ids_.size());
  for (auto sticker_set_id : featured_sticker_set_ids_) {
    const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
    CHECK(sticker_set != nullptr);
    CHECK(sticker_set->is_inited);

    uint64 pack_id = static_cast<uint64>(sticker_set_id.get());
    numbers.push_back(static_cast<uint32>(pack_id >> 32));
    numbers.push_back(static_cast<uint32>(pack_id & 0xFFFFFFFF));

    if (!sticker_set->is_viewed) {
      numbers.push_back(1);
    }
  }
  return get_vector_hash(numbers);
}

vector<int64> StickersManager::convert_sticker_set_ids(const vector<StickerSetId> &sticker_set_ids) {
  return transform(sticker_set_ids, [](StickerSetId sticker_set_id) { return sticker_set_id.get(); });
}

vector<StickerSetId> StickersManager::convert_sticker_set_ids(const vector<int64> &sticker_set_ids) {
  return transform(sticker_set_ids, [](int64 sticker_set_id) { return StickerSetId(sticker_set_id); });
}

td_api::object_ptr<td_api::updateInstalledStickerSets> StickersManager::get_update_installed_sticker_sets_object(
    int is_masks) const {
  return td_api::make_object<td_api::updateInstalledStickerSets>(
      is_masks != 0, convert_sticker_set_ids(installed_sticker_set_ids_[is_masks]));
}

void StickersManager::send_update_installed_sticker_sets(bool from_database) {
  for (int is_masks = 0; is_masks < 2; is_masks++) {
    if (need_update_installed_sticker_sets_[is_masks]) {
      need_update_installed_sticker_sets_[is_masks] = false;
      if (are_installed_sticker_sets_loaded_[is_masks]) {
        installed_sticker_sets_hash_[is_masks] = get_sticker_sets_hash(installed_sticker_set_ids_[is_masks]);
        send_closure(G()->td(), &Td::send_update, get_update_installed_sticker_sets_object(is_masks));

        if (G()->parameters().use_file_db && !from_database && !G()->close_flag()) {
          LOG(INFO) << "Save installed " << (is_masks ? "mask " : "") << "sticker sets to database";
          StickerSetListLogEvent log_event(installed_sticker_set_ids_[is_masks]);
          G()->td_db()->get_sqlite_pmc()->set(is_masks ? "sss1" : "sss0", log_event_store(log_event).as_slice().str(),
                                              Auto());
        }
      }
    }
  }
}

td_api::object_ptr<td_api::updateTrendingStickerSets> StickersManager::get_update_trending_sticker_sets_object() const {
  auto total_count = static_cast<int32>(featured_sticker_set_ids_.size()) +
                     (old_featured_sticker_set_count_ == -1 ? 1 : old_featured_sticker_set_count_);
  return td_api::make_object<td_api::updateTrendingStickerSets>(
      get_sticker_sets_object(total_count, featured_sticker_set_ids_, 5));
}

void StickersManager::send_update_featured_sticker_sets() {
  if (need_update_featured_sticker_sets_) {
    need_update_featured_sticker_sets_ = false;
    featured_sticker_sets_hash_ = get_featured_sticker_sets_hash();

    send_closure(G()->td(), &Td::send_update, get_update_trending_sticker_sets_object());
  }
}

void StickersManager::reload_recent_stickers(bool is_attached, bool force) {
  if (G()->close_flag()) {
    return;
  }

  auto &next_load_time = next_recent_stickers_load_time_[is_attached];
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload recent " << (is_attached ? "attached " : "") << "stickers";
    next_load_time = -1;
    td_->create_handler<GetRecentStickersQuery>()->send(false, is_attached, recent_stickers_hash_[is_attached]);
  }
}

void StickersManager::repair_recent_stickers(bool is_attached, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots has no recent stickers"));
  }

  repair_recent_stickers_queries_[is_attached].push_back(std::move(promise));
  if (repair_recent_stickers_queries_[is_attached].size() == 1u) {
    td_->create_handler<GetRecentStickersQuery>()->send(true, is_attached, 0);
  }
}

vector<FileId> StickersManager::get_recent_stickers(bool is_attached, Promise<Unit> &&promise) {
  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, std::move(promise));
    return {};
  }
  reload_recent_stickers(is_attached, false);

  promise.set_value(Unit());
  return recent_sticker_ids_[is_attached];
}

void StickersManager::load_recent_stickers(bool is_attached, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_recent_stickers_loaded_[is_attached] = true;
  }
  if (are_recent_stickers_loaded_[is_attached]) {
    promise.set_value(Unit());
    return;
  }
  load_recent_stickers_queries_[is_attached].push_back(std::move(promise));
  if (load_recent_stickers_queries_[is_attached].size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load recent " << (is_attached ? "attached " : "") << "stickers from database";
      G()->td_db()->get_sqlite_pmc()->get(
          is_attached ? "ssr1" : "ssr0", PromiseCreator::lambda([is_attached](string value) {
            send_closure(G()->stickers_manager(), &StickersManager::on_load_recent_stickers_from_database, is_attached,
                         std::move(value));
          }));
    } else {
      LOG(INFO) << "Trying to load recent " << (is_attached ? "attached " : "") << "stickers from server";
      reload_recent_stickers(is_attached, true);
    }
  }
}

void StickersManager::on_load_recent_stickers_from_database(bool is_attached, string value) {
  if (G()->close_flag()) {
    return;
  }
  if (value.empty()) {
    LOG(INFO) << "Recent " << (is_attached ? "attached " : "") << "stickers aren't found in database";
    reload_recent_stickers(is_attached, true);
    return;
  }

  LOG(INFO) << "Successfully loaded recent " << (is_attached ? "attached " : "") << "stickers list of size "
            << value.size() << " from database";

  StickerListLogEvent log_event;
  auto status = log_event_parse(log_event, value);
  if (status.is_error()) {
    // can't happen unless database is broken, but has been seen in the wild
    LOG(ERROR) << "Can't load recent stickers: " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    return reload_recent_stickers(is_attached, true);
  }

  on_load_recent_stickers_finished(is_attached, std::move(log_event.sticker_ids), true);
}

void StickersManager::on_load_recent_stickers_finished(bool is_attached, vector<FileId> &&recent_sticker_ids,
                                                       bool from_database) {
  if (static_cast<int32>(recent_sticker_ids.size()) > recent_stickers_limit_) {
    recent_sticker_ids.resize(recent_stickers_limit_);
  }
  recent_sticker_ids_[is_attached] = std::move(recent_sticker_ids);
  are_recent_stickers_loaded_[is_attached] = true;
  need_update_recent_stickers_[is_attached] = true;
  send_update_recent_stickers(from_database);
  auto promises = std::move(load_recent_stickers_queries_[is_attached]);
  load_recent_stickers_queries_[is_attached].clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void StickersManager::on_get_recent_stickers(bool is_repair, bool is_attached,
                                             tl_object_ptr<telegram_api::messages_RecentStickers> &&stickers_ptr) {
  CHECK(!td_->auth_manager_->is_bot());
  if (!is_repair) {
    next_recent_stickers_load_time_[is_attached] = Time::now_cached() + Random::fast(30 * 60, 50 * 60);
  }

  CHECK(stickers_ptr != nullptr);
  int32 constructor_id = stickers_ptr->get_id();
  if (constructor_id == telegram_api::messages_recentStickersNotModified::ID) {
    if (is_repair) {
      return on_get_recent_stickers_failed(true, is_attached, Status::Error(500, "Failed to reload recent stickers"));
    }
    LOG(INFO) << (is_attached ? "Attached r" : "R") << "ecent stickers are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_recentStickers::ID);
  auto stickers = move_tl_object_as<telegram_api::messages_recentStickers>(stickers_ptr);

  vector<FileId> recent_sticker_ids;
  recent_sticker_ids.reserve(stickers->stickers_.size());
  for (auto &document_ptr : stickers->stickers_) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr)).second;
    if (!sticker_id.is_valid()) {
      continue;
    }
    recent_sticker_ids.push_back(sticker_id);
  }

  if (is_repair) {
    auto promises = std::move(repair_recent_stickers_queries_[is_attached]);
    repair_recent_stickers_queries_[is_attached].clear();
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
  } else {
    on_load_recent_stickers_finished(is_attached, std::move(recent_sticker_ids));

    LOG_IF(ERROR, recent_stickers_hash_[is_attached] != stickers->hash_) << "Stickers hash mismatch";
  }
}

void StickersManager::on_get_recent_stickers_failed(bool is_repair, bool is_attached, Status error) {
  CHECK(error.is_error());
  if (!is_repair) {
    next_recent_stickers_load_time_[is_attached] = Time::now_cached() + Random::fast(5, 10);
  }
  auto &queries = is_repair ? repair_recent_stickers_queries_[is_attached] : load_recent_stickers_queries_[is_attached];
  auto promises = std::move(queries);
  queries.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

int32 StickersManager::get_recent_stickers_hash(const vector<FileId> &sticker_ids) const {
  vector<uint32> numbers;
  numbers.reserve(sticker_ids.size() * 2);
  for (auto sticker_id : sticker_ids) {
    auto sticker = get_sticker(sticker_id);
    CHECK(sticker != nullptr);
    auto file_view = td_->file_manager_->get_file_view(sticker_id);
    CHECK(file_view.has_remote_location());
    if (!file_view.remote_location().is_document()) {
      LOG(ERROR) << "Recent sticker remote location is not document: " << file_view.remote_location();
      continue;
    }
    auto id = static_cast<uint64>(file_view.remote_location().get_id());
    numbers.push_back(static_cast<uint32>(id >> 32));
    numbers.push_back(static_cast<uint32>(id & 0xFFFFFFFF));
  }
  return get_vector_hash(numbers);
}

FileSourceId StickersManager::get_recent_stickers_file_source_id(int is_attached) {
  if (!recent_stickers_file_source_id_[is_attached].is_valid()) {
    recent_stickers_file_source_id_[is_attached] =
        td_->file_reference_manager_->create_recent_stickers_file_source(is_attached != 0);
  }
  return recent_stickers_file_source_id_[is_attached];
}

void StickersManager::add_recent_sticker(bool is_attached, const tl_object_ptr<td_api::InputFile> &input_file,
                                         Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  add_recent_sticker_impl(is_attached, r_file_id.ok(), true, std::move(promise));
}

void StickersManager::send_save_recent_sticker_query(bool is_attached, FileId sticker_id, bool unsave,
                                                     Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  // TODO invokeAfter and log event
  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  CHECK(file_view.has_remote_location());
  CHECK(file_view.remote_location().is_document());
  CHECK(!file_view.remote_location().is_web());
  td_->create_handler<SaveRecentStickerQuery>(std::move(promise))
      ->send(is_attached, sticker_id, file_view.remote_location().as_input_document(), unsave);
}

void StickersManager::add_recent_sticker_by_id(bool is_attached, FileId sticker_id) {
  // TODO log event
  add_recent_sticker_impl(is_attached, sticker_id, false, Auto());
}

void StickersManager::add_recent_sticker_impl(bool is_attached, FileId sticker_id, bool add_on_server,
                                              Promise<Unit> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());

  LOG(INFO) << "Add recent " << (is_attached ? "attached " : "") << "sticker " << sticker_id;
  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, PromiseCreator::lambda([is_attached, sticker_id, add_on_server,
                                                              promise = std::move(promise)](Result<> result) mutable {
                           if (result.is_ok()) {
                             send_closure(G()->stickers_manager(), &StickersManager::add_recent_sticker_impl,
                                          is_attached, sticker_id, add_on_server, std::move(promise));
                           } else {
                             promise.set_error(result.move_as_error());
                           }
                         }));
    return;
  }

  auto is_equal = [sticker_id](FileId file_id) {
    return file_id == sticker_id || (file_id.get_remote() == sticker_id.get_remote() && sticker_id.get_remote() != 0);
  };

  vector<FileId> &sticker_ids = recent_sticker_ids_[is_attached];
  if (!sticker_ids.empty() && is_equal(sticker_ids[0])) {
    if (sticker_ids[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
      sticker_ids[0] = sticker_id;
      save_recent_stickers_to_database(is_attached);
    }

    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(sticker_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }
  if (!sticker->set_id.is_valid()) {
    return promise.set_error(Status::Error(7, "Stickers without sticker set can't be added to recent"));
  }

  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  if (!file_view.has_remote_location()) {
    return promise.set_error(Status::Error(7, "Can save only sent stickers"));
  }
  if (file_view.remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Can't save web stickers"));
  }
  if (!file_view.remote_location().is_document()) {
    return promise.set_error(Status::Error(7, "Can't save encrypted stickers"));
  }

  need_update_recent_stickers_[is_attached] = true;

  auto it = std::find_if(sticker_ids.begin(), sticker_ids.end(), is_equal);
  if (it == sticker_ids.end()) {
    if (static_cast<int32>(sticker_ids.size()) == recent_stickers_limit_) {
      sticker_ids.back() = sticker_id;
    } else {
      sticker_ids.push_back(sticker_id);
    }
    it = sticker_ids.end() - 1;
  }
  std::rotate(sticker_ids.begin(), it, it + 1);
  if (sticker_ids[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
    sticker_ids[0] = sticker_id;
  }

  send_update_recent_stickers();
  if (add_on_server) {
    send_save_recent_sticker_query(is_attached, sticker_id, false, std::move(promise));
  }
}

void StickersManager::remove_recent_sticker(bool is_attached, const tl_object_ptr<td_api::InputFile> &input_file,
                                            Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  vector<FileId> &sticker_ids = recent_sticker_ids_[is_attached];
  FileId file_id = r_file_id.ok();
  if (!td::remove(sticker_ids, file_id)) {
    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(file_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }

  send_save_recent_sticker_query(is_attached, file_id, true, std::move(promise));

  need_update_recent_stickers_[is_attached] = true;
  send_update_recent_stickers();
}

void StickersManager::clear_recent_stickers(bool is_attached, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, std::move(promise));
    return;
  }

  vector<FileId> &sticker_ids = recent_sticker_ids_[is_attached];
  if (sticker_ids.empty()) {
    return promise.set_value(Unit());
  }

  // TODO invokeAfter
  td_->create_handler<ClearRecentStickersQuery>(std::move(promise))->send(is_attached);

  sticker_ids.clear();

  need_update_recent_stickers_[is_attached] = true;
  send_update_recent_stickers();
}

td_api::object_ptr<td_api::updateRecentStickers> StickersManager::get_update_recent_stickers_object(
    int is_attached) const {
  return td_api::make_object<td_api::updateRecentStickers>(
      is_attached != 0, td_->file_manager_->get_file_ids_object(recent_sticker_ids_[is_attached]));
}

void StickersManager::send_update_recent_stickers(bool from_database) {
  for (int is_attached = 0; is_attached < 2; is_attached++) {
    if (need_update_recent_stickers_[is_attached]) {
      need_update_recent_stickers_[is_attached] = false;
      if (are_recent_stickers_loaded_[is_attached]) {
        vector<FileId> new_recent_sticker_file_ids;
        for (auto &sticker_id : recent_sticker_ids_[is_attached]) {
          append(new_recent_sticker_file_ids, get_sticker_file_ids(sticker_id));
        }
        std::sort(new_recent_sticker_file_ids.begin(), new_recent_sticker_file_ids.end());
        if (new_recent_sticker_file_ids != recent_sticker_file_ids_[is_attached]) {
          td_->file_manager_->change_files_source(get_recent_stickers_file_source_id(is_attached),
                                                  recent_sticker_file_ids_[is_attached], new_recent_sticker_file_ids);
          recent_sticker_file_ids_[is_attached] = std::move(new_recent_sticker_file_ids);
        }

        recent_stickers_hash_[is_attached] = get_recent_stickers_hash(recent_sticker_ids_[is_attached]);
        send_closure(G()->td(), &Td::send_update, get_update_recent_stickers_object(is_attached));

        if (!from_database) {
          save_recent_stickers_to_database(is_attached != 0);
        }
      }
    }
  }
}

void StickersManager::save_recent_stickers_to_database(bool is_attached) {
  if (G()->parameters().use_file_db && !G()->close_flag()) {
    LOG(INFO) << "Save recent " << (is_attached ? "attached " : "") << "stickers to database";
    StickerListLogEvent log_event(recent_sticker_ids_[is_attached]);
    G()->td_db()->get_sqlite_pmc()->set(is_attached ? "ssr1" : "ssr0", log_event_store(log_event).as_slice().str(),
                                        Auto());
  }
}

void StickersManager::on_update_recent_stickers_limit(int32 recent_stickers_limit) {
  if (recent_stickers_limit != recent_stickers_limit_) {
    if (recent_stickers_limit > 0) {
      LOG(INFO) << "Update recent stickers limit to " << recent_stickers_limit;
      recent_stickers_limit_ = recent_stickers_limit;
      for (int is_attached = 0; is_attached < 2; is_attached++) {
        if (static_cast<int32>(recent_sticker_ids_[is_attached].size()) > recent_stickers_limit) {
          recent_sticker_ids_[is_attached].resize(recent_stickers_limit);
          send_update_recent_stickers();
        }
      }
    } else {
      LOG(ERROR) << "Receive wrong recent stickers limit = " << recent_stickers_limit;
    }
  }
}

void StickersManager::on_update_favorite_stickers_limit(int32 favorite_stickers_limit) {
  if (favorite_stickers_limit != favorite_stickers_limit_) {
    if (favorite_stickers_limit > 0) {
      LOG(INFO) << "Update favorite stickers limit to " << favorite_stickers_limit;
      favorite_stickers_limit_ = favorite_stickers_limit;
      if (static_cast<int32>(favorite_sticker_ids_.size()) > favorite_stickers_limit) {
        favorite_sticker_ids_.resize(favorite_stickers_limit);
        send_update_favorite_stickers();
      }
    } else {
      LOG(ERROR) << "Receive wrong favorite stickers limit = " << favorite_stickers_limit;
    }
  }
}

void StickersManager::reload_favorite_stickers(bool force) {
  if (G()->close_flag()) {
    return;
  }

  auto &next_load_time = next_favorite_stickers_load_time_;
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload favorite stickers";
    next_load_time = -1;
    td_->create_handler<GetFavedStickersQuery>()->send(false, get_favorite_stickers_hash());
  }
}

void StickersManager::repair_favorite_stickers(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots has no favorite stickers"));
  }

  repair_favorite_stickers_queries_.push_back(std::move(promise));
  if (repair_favorite_stickers_queries_.size() == 1u) {
    td_->create_handler<GetFavedStickersQuery>()->send(true, 0);
  }
}

vector<FileId> StickersManager::get_favorite_stickers(Promise<Unit> &&promise) {
  if (!are_favorite_stickers_loaded_) {
    load_favorite_stickers(std::move(promise));
    return {};
  }
  reload_favorite_stickers(false);

  promise.set_value(Unit());
  return favorite_sticker_ids_;
}

void StickersManager::load_favorite_stickers(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_favorite_stickers_loaded_ = true;
  }
  if (are_favorite_stickers_loaded_) {
    promise.set_value(Unit());
    return;
  }
  load_favorite_stickers_queries_.push_back(std::move(promise));
  if (load_favorite_stickers_queries_.size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load favorite stickers from database";
      G()->td_db()->get_sqlite_pmc()->get("ssfav", PromiseCreator::lambda([](string value) {
                                            send_closure(G()->stickers_manager(),
                                                         &StickersManager::on_load_favorite_stickers_from_database,
                                                         std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load favorite stickers from server";
      reload_favorite_stickers(true);
    }
  }
}

void StickersManager::on_load_favorite_stickers_from_database(const string &value) {
  if (G()->close_flag()) {
    return;
  }
  if (value.empty()) {
    LOG(INFO) << "Favorite stickers aren't found in database";
    reload_favorite_stickers(true);
    return;
  }

  LOG(INFO) << "Successfully loaded favorite stickers list of size " << value.size() << " from database";

  StickerListLogEvent log_event;
  auto status = log_event_parse(log_event, value);
  if (status.is_error()) {
    // can't happen unless database is broken, but has been seen in the wild
    LOG(ERROR) << "Can't load favorite stickers: " << status << ' ' << format::as_hex_dump<4>(Slice(value));
    return reload_favorite_stickers(true);
  }

  on_load_favorite_stickers_finished(std::move(log_event.sticker_ids), true);
}

void StickersManager::on_load_favorite_stickers_finished(vector<FileId> &&favorite_sticker_ids, bool from_database) {
  if (static_cast<int32>(favorite_sticker_ids.size()) > favorite_stickers_limit_) {
    favorite_sticker_ids.resize(favorite_stickers_limit_);
  }
  favorite_sticker_ids_ = std::move(favorite_sticker_ids);
  are_favorite_stickers_loaded_ = true;
  send_update_favorite_stickers(from_database);
  auto promises = std::move(load_favorite_stickers_queries_);
  load_favorite_stickers_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void StickersManager::on_get_favorite_stickers(
    bool is_repair, tl_object_ptr<telegram_api::messages_FavedStickers> &&favorite_stickers_ptr) {
  CHECK(!td_->auth_manager_->is_bot());
  if (!is_repair) {
    next_favorite_stickers_load_time_ = Time::now_cached() + Random::fast(30 * 60, 50 * 60);
  }

  CHECK(favorite_stickers_ptr != nullptr);
  int32 constructor_id = favorite_stickers_ptr->get_id();
  if (constructor_id == telegram_api::messages_favedStickersNotModified::ID) {
    if (is_repair) {
      return on_get_favorite_stickers_failed(true, Status::Error(500, "Failed to reload favorite stickers"));
    }
    LOG(INFO) << "Favorite stickers are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_favedStickers::ID);
  auto favorite_stickers = move_tl_object_as<telegram_api::messages_favedStickers>(favorite_stickers_ptr);

  // TODO use favorite_stickers->packs_

  vector<FileId> favorite_sticker_ids;
  favorite_sticker_ids.reserve(favorite_stickers->stickers_.size());
  for (auto &document_ptr : favorite_stickers->stickers_) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr)).second;
    if (!sticker_id.is_valid()) {
      continue;
    }

    favorite_sticker_ids.push_back(sticker_id);
  }

  if (is_repair) {
    auto promises = std::move(repair_favorite_stickers_queries_);
    repair_favorite_stickers_queries_.clear();
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
  } else {
    on_load_favorite_stickers_finished(std::move(favorite_sticker_ids));

    LOG_IF(ERROR, get_favorite_stickers_hash() != favorite_stickers->hash_) << "Favorite stickers hash mismatch";
  }
}

void StickersManager::on_get_favorite_stickers_failed(bool is_repair, Status error) {
  CHECK(error.is_error());
  if (!is_repair) {
    next_favorite_stickers_load_time_ = Time::now_cached() + Random::fast(5, 10);
  }
  auto &queries = is_repair ? repair_favorite_stickers_queries_ : load_favorite_stickers_queries_;
  auto promises = std::move(queries);
  queries.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

int32 StickersManager::get_favorite_stickers_hash() const {
  return get_recent_stickers_hash(favorite_sticker_ids_);
}

FileSourceId StickersManager::get_favorite_stickers_file_source_id() {
  if (!favorite_stickers_file_source_id_.is_valid()) {
    favorite_stickers_file_source_id_ = td_->file_reference_manager_->create_favorite_stickers_file_source();
  }
  return favorite_stickers_file_source_id_;
}

void StickersManager::add_favorite_sticker(const tl_object_ptr<td_api::InputFile> &input_file,
                                           Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_favorite_stickers_loaded_) {
    load_favorite_stickers(std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  add_favorite_sticker_impl(r_file_id.ok(), true, std::move(promise));
}

void StickersManager::send_fave_sticker_query(FileId sticker_id, bool unsave, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  // TODO invokeAfter and log event
  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  CHECK(file_view.has_remote_location());
  CHECK(file_view.remote_location().is_document());
  CHECK(!file_view.remote_location().is_web());
  td_->create_handler<FaveStickerQuery>(std::move(promise))
      ->send(sticker_id, file_view.remote_location().as_input_document(), unsave);
}

void StickersManager::add_favorite_sticker_by_id(FileId sticker_id) {
  // TODO log event
  add_favorite_sticker_impl(sticker_id, false, Auto());
}

void StickersManager::add_favorite_sticker_impl(FileId sticker_id, bool add_on_server, Promise<Unit> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());

  if (!are_favorite_stickers_loaded_) {
    load_favorite_stickers(
        PromiseCreator::lambda([sticker_id, add_on_server, promise = std::move(promise)](Result<> result) mutable {
          if (result.is_ok()) {
            send_closure(G()->stickers_manager(), &StickersManager::add_favorite_sticker_impl, sticker_id,
                         add_on_server, std::move(promise));
          } else {
            promise.set_error(result.move_as_error());
          }
        }));
    return;
  }

  auto is_equal = [sticker_id](FileId file_id) {
    return file_id == sticker_id || (file_id.get_remote() == sticker_id.get_remote() && sticker_id.get_remote() != 0);
  };

  if (!favorite_sticker_ids_.empty() && is_equal(favorite_sticker_ids_[0])) {
    if (favorite_sticker_ids_[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
      favorite_sticker_ids_[0] = sticker_id;
      save_favorite_stickers_to_database();
    }

    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(sticker_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }
  if (!sticker->set_id.is_valid()) {
    return promise.set_error(Status::Error(7, "Stickers without sticker set can't be favorite"));
  }

  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  if (!file_view.has_remote_location()) {
    return promise.set_error(Status::Error(7, "Can add to favorites only sent stickers"));
  }
  if (file_view.remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Can't add to favorites web stickers"));
  }
  if (!file_view.remote_location().is_document()) {
    return promise.set_error(Status::Error(7, "Can't add to favorites encrypted stickers"));
  }

  auto it = std::find_if(favorite_sticker_ids_.begin(), favorite_sticker_ids_.end(), is_equal);
  if (it == favorite_sticker_ids_.end()) {
    if (static_cast<int32>(favorite_sticker_ids_.size()) == favorite_stickers_limit_) {
      favorite_sticker_ids_.back() = sticker_id;
    } else {
      favorite_sticker_ids_.push_back(sticker_id);
    }
    it = favorite_sticker_ids_.end() - 1;
  }
  std::rotate(favorite_sticker_ids_.begin(), it, it + 1);
  if (favorite_sticker_ids_[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
    favorite_sticker_ids_[0] = sticker_id;
  }

  send_update_favorite_stickers();
  if (add_on_server) {
    send_fave_sticker_query(sticker_id, false, std::move(promise));
  }
}

void StickersManager::remove_favorite_sticker(const tl_object_ptr<td_api::InputFile> &input_file,
                                              Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(7, "Method is not available for bots"));
  }
  if (!are_favorite_stickers_loaded_) {
    load_favorite_stickers(std::move(promise));
    return;
  }

  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  FileId file_id = r_file_id.ok();
  if (!td::remove(favorite_sticker_ids_, file_id)) {
    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(file_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }

  send_fave_sticker_query(file_id, true, std::move(promise));

  send_update_favorite_stickers();
}

td_api::object_ptr<td_api::updateFavoriteStickers> StickersManager::get_update_favorite_stickers_object() const {
  return td_api::make_object<td_api::updateFavoriteStickers>(
      td_->file_manager_->get_file_ids_object(favorite_sticker_ids_));
}

void StickersManager::send_update_favorite_stickers(bool from_database) {
  if (are_favorite_stickers_loaded_) {
    vector<FileId> new_favorite_sticker_file_ids;
    for (auto &sticker_id : favorite_sticker_ids_) {
      append(new_favorite_sticker_file_ids, get_sticker_file_ids(sticker_id));
    }
    std::sort(new_favorite_sticker_file_ids.begin(), new_favorite_sticker_file_ids.end());
    if (new_favorite_sticker_file_ids != favorite_sticker_file_ids_) {
      td_->file_manager_->change_files_source(get_favorite_stickers_file_source_id(), favorite_sticker_file_ids_,
                                              new_favorite_sticker_file_ids);
      favorite_sticker_file_ids_ = std::move(new_favorite_sticker_file_ids);
    }

    send_closure(G()->td(), &Td::send_update, get_update_favorite_stickers_object());

    if (!from_database) {
      save_favorite_stickers_to_database();
    }
  }
}

void StickersManager::save_favorite_stickers_to_database() {
  if (G()->parameters().use_file_db && !G()->close_flag()) {
    LOG(INFO) << "Save favorite stickers to database";
    StickerListLogEvent log_event(favorite_sticker_ids_);
    G()->td_db()->get_sqlite_pmc()->set("ssfav", log_event_store(log_event).as_slice().str(), Auto());
  }
}

vector<string> StickersManager::get_sticker_emojis(const tl_object_ptr<td_api::InputFile> &input_file,
                                                   Promise<Unit> &&promise) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, input_file, DialogId(), false, false);
  if (r_file_id.is_error()) {
    promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
    return {};
  }

  FileId file_id = r_file_id.ok();

  auto sticker = get_sticker(file_id);
  if (sticker == nullptr) {
    promise.set_value(Unit());
    return {};
  }
  if (!sticker->set_id.is_valid()) {
    promise.set_value(Unit());
    return {};
  }

  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (!file_view.has_remote_location()) {
    promise.set_value(Unit());
    return {};
  }
  if (!file_view.remote_location().is_document()) {
    promise.set_value(Unit());
    return {};
  }
  if (file_view.remote_location().is_web()) {
    promise.set_value(Unit());
    return {};
  }

  const StickerSet *sticker_set = get_sticker_set(sticker->set_id);
  if (update_sticker_set_cache(sticker_set, promise)) {
    return {};
  }

  promise.set_value(Unit());
  auto it = sticker_set->sticker_emojis_map_.find(file_id);
  if (it == sticker_set->sticker_emojis_map_.end()) {
    return {};
  }

  return it->second;
}

string StickersManager::get_sticker_mime_type(const Sticker *s) {
  return s->is_animated ? "application/x-tgsticker" : "image/webp";
}

string StickersManager::get_emoji_language_code_version_database_key(const string &language_code) {
  return PSTRING() << "emojiv$" << language_code;
}

int32 StickersManager::get_emoji_language_code_version(const string &language_code) {
  auto it = emoji_language_code_versions_.find(language_code);
  if (it != emoji_language_code_versions_.end()) {
    return it->second;
  }
  auto &result = emoji_language_code_versions_[language_code];
  result = to_integer<int32>(
      G()->td_db()->get_sqlite_sync_pmc()->get(get_emoji_language_code_version_database_key(language_code)));
  return result;
}

string StickersManager::get_emoji_language_code_last_difference_time_database_key(const string &language_code) {
  return PSTRING() << "emojid$" << language_code;
}

double StickersManager::get_emoji_language_code_last_difference_time(const string &language_code) {
  auto it = emoji_language_code_last_difference_times_.find(language_code);
  if (it != emoji_language_code_last_difference_times_.end()) {
    return it->second;
  }
  auto &result = emoji_language_code_last_difference_times_[language_code];
  int32 old_unix_time = to_integer<int32>(G()->td_db()->get_sqlite_sync_pmc()->get(
      get_emoji_language_code_last_difference_time_database_key(language_code)));
  int32 passed_time = max(static_cast<int32>(0), G()->unix_time() - old_unix_time);
  result = Time::now_cached() - passed_time;
  return result;
}

string StickersManager::get_language_emojis_database_key(const string &language_code, const string &text) {
  return PSTRING() << "emoji$" << language_code << '$' << text;
}

vector<string> StickersManager::search_language_emojis(const string &language_code, const string &text,
                                                       bool exact_match) const {
  LOG(INFO) << "Search for \"" << text << "\" in language " << language_code;
  auto key = get_language_emojis_database_key(language_code, text);
  if (exact_match) {
    string emojis = G()->td_db()->get_sqlite_sync_pmc()->get(key);
    return full_split(emojis, '$');
  } else {
    vector<string> result;
    G()->td_db()->get_sqlite_sync_pmc()->get_by_prefix(key, [&result](Slice key, Slice value) {
      for (auto &emoji : full_split(value, '$')) {
        result.push_back(emoji.str());
      }
      return true;
    });
    return result;
  }
}

string StickersManager::get_emoji_language_codes_database_key(const vector<string> &language_codes) {
  return PSTRING() << "emojilc$" << implode(language_codes, '$');
}

void StickersManager::load_language_codes(vector<string> language_codes, string key, Promise<Unit> &&promise) {
  auto &promises = load_language_codes_queries_[key];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), key = std::move(key)](Result<vector<string>> &&result) {
        send_closure(actor_id, &StickersManager::on_get_language_codes, key, std::move(result));
      });
  td_->create_handler<GetEmojiKeywordsLanguageQuery>(std::move(query_promise))->send(std::move(language_codes));
}

void StickersManager::on_get_language_codes(const string &key, Result<vector<string>> &&result) {
  auto queries_it = load_language_codes_queries_.find(key);
  CHECK(queries_it != load_language_codes_queries_.end());
  CHECK(!queries_it->second.empty());
  auto promises = std::move(queries_it->second);
  load_language_codes_queries_.erase(queries_it);

  if (result.is_error()) {
    if (!G()->is_expected_error(result.error())) {
      LOG(ERROR) << "Receive " << result.error() << " from GetEmojiKeywordsLanguageQuery";
    }
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto language_codes = result.move_as_ok();
  LOG(INFO) << "Receive language codes " << language_codes << " for emojis search with key " << key;
  td::remove_if(language_codes, [](const string &language_code) {
    if (language_code.empty() || language_code.find('$') != string::npos) {
      LOG(ERROR) << "Receive language_code \"" << language_code << '"';
      return true;
    }
    return false;
  });
  if (language_codes.empty()) {
    LOG(ERROR) << "Language codes list is empty";
    language_codes.emplace_back("en");
  }
  td::unique(language_codes);

  auto it = emoji_language_codes_.find(key);
  CHECK(it != emoji_language_codes_.end());
  if (it->second != language_codes) {
    LOG(INFO) << "Update emoji language codes for " << key << " to " << language_codes;
    if (!G()->close_flag()) {
      CHECK(G()->parameters().use_file_db);
      G()->td_db()->get_sqlite_pmc()->set(key, implode(language_codes, '$'), Auto());
    }
    it->second = std::move(language_codes);
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

vector<string> StickersManager::get_emoji_language_codes(const vector<string> &input_language_codes, Slice text,
                                                         Promise<Unit> &promise) {
  vector<string> language_codes = td_->language_pack_manager_->get_actor_unsafe()->get_used_language_codes();
  auto system_language_code = G()->mtproto_header().get_system_language_code();
  if (system_language_code.size() >= 2 && system_language_code.find('$') == string::npos &&
      (system_language_code.size() == 2 || system_language_code[2] == '-')) {
    language_codes.push_back(system_language_code.substr(0, 2));
  }
  for (auto &input_language_code : input_language_codes) {
    if (input_language_code.size() >= 2 && input_language_code.find('$') == string::npos &&
        (input_language_code.size() == 2 || input_language_code[2] == '-')) {
      language_codes.push_back(input_language_code.substr(0, 2));
    }
  }
  if (!text.empty()) {
    uint32 code = 0;
    next_utf8_unsafe(text.ubegin(), &code, "get_emoji_language_codes");
    if ((0x410 <= code && code <= 0x44F) || code == 0x401 || code == 0x451) {
      // the first letter is cyrillic
      if (!td::contains(language_codes, "ru") && !td::contains(language_codes, "uk") &&
          !td::contains(language_codes, "bg") && !td::contains(language_codes, "be") &&
          !td::contains(language_codes, "mk") && !td::contains(language_codes, "sr") &&
          !td::contains(language_codes, "mn") && !td::contains(language_codes, "ky") &&
          !td::contains(language_codes, "kk") && !td::contains(language_codes, "uz") &&
          !td::contains(language_codes, "tk")) {
        language_codes.push_back("ru");
      }
    }
  }

  if (language_codes.empty()) {
    LOG(INFO) << "List of language codes is empty";
    language_codes.push_back("en");
  }
  td::unique(language_codes);

  LOG(DEBUG) << "Have language codes " << language_codes;
  auto key = get_emoji_language_codes_database_key(language_codes);
  auto it = emoji_language_codes_.find(key);
  if (it == emoji_language_codes_.end()) {
    it = emoji_language_codes_.emplace(key, full_split(G()->td_db()->get_sqlite_sync_pmc()->get(key), '$')).first;
  }
  if (it->second.empty()) {
    load_language_codes(std::move(language_codes), std::move(key), std::move(promise));
  } else {
    LOG(DEBUG) << "Have emoji language codes " << it->second;
    double now = Time::now_cached();
    for (auto &language_code : it->second) {
      double last_difference_time = get_emoji_language_code_last_difference_time(language_code);
      if (last_difference_time < now - EMOJI_KEYWORDS_UPDATE_DELAY &&
          get_emoji_language_code_version(language_code) != 0) {
        load_emoji_keywords_difference(language_code);
      }
    }
    if (reloaded_emoji_keywords_.insert(key).second) {
      load_language_codes(std::move(language_codes), std::move(key), Auto());
    }
  }
  return it->second;
}

void StickersManager::load_emoji_keywords(const string &language_code, Promise<Unit> &&promise) {
  auto &promises = load_emoji_keywords_queries_[language_code];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this),
       language_code](Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result) mutable {
        send_closure(actor_id, &StickersManager::on_get_emoji_keywords, language_code, std::move(result));
      });
  td_->create_handler<GetEmojiKeywordsQuery>(std::move(query_promise))->send(language_code);
}

void StickersManager::on_get_emoji_keywords(
    const string &language_code, Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result) {
  auto it = load_emoji_keywords_queries_.find(language_code);
  CHECK(it != load_emoji_keywords_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  load_emoji_keywords_queries_.erase(it);

  if (result.is_error()) {
    if (!G()->is_expected_error(result.error())) {
      LOG(ERROR) << "Receive " << result.error() << " from GetEmojiKeywordsQuery";
    }
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto version = get_emoji_language_code_version(language_code);
  CHECK(version == 0);

  MultiPromiseActorSafe mpas{"SaveEmojiKeywordsMultiPromiseActor"};
  for (auto &promise : promises) {
    mpas.add_promise(std::move(promise));
  }

  auto lock = mpas.get_promise();

  auto keywords = result.move_as_ok();
  LOG(INFO) << "Receive " << keywords->keywords_.size() << " emoji keywords for language " << language_code;
  LOG_IF(ERROR, language_code != keywords->lang_code_)
      << "Receive keywords for " << keywords->lang_code_ << " instead of " << language_code;
  LOG_IF(ERROR, keywords->from_version_ != 0) << "Receive keywords from version " << keywords->from_version_;
  version = keywords->version_;
  if (version <= 0) {
    LOG(ERROR) << "Receive keywords of version " << version;
    version = 1;
  }
  for (auto &keyword_ptr : keywords->keywords_) {
    switch (keyword_ptr->get_id()) {
      case telegram_api::emojiKeyword::ID: {
        auto keyword = telegram_api::move_object_as<telegram_api::emojiKeyword>(keyword_ptr);
        auto text = utf8_to_lower(keyword->keyword_);
        bool is_good = true;
        for (auto &emoji : keyword->emoticons_) {
          if (emoji.find('$') != string::npos) {
            LOG(ERROR) << "Receive emoji \"" << emoji << "\" from server for " << text;
            is_good = false;
          }
        }
        if (is_good && !G()->close_flag()) {
          CHECK(G()->parameters().use_file_db);
          G()->td_db()->get_sqlite_pmc()->set(get_language_emojis_database_key(language_code, text),
                                              implode(keyword->emoticons_, '$'), mpas.get_promise());
        }
        break;
      }
      case telegram_api::emojiKeywordDeleted::ID:
        LOG(ERROR) << "Receive emojiKeywordDeleted in keywords for " << language_code;
        break;
      default:
        UNREACHABLE();
    }
  }
  if (!G()->close_flag()) {
    CHECK(G()->parameters().use_file_db);
    G()->td_db()->get_sqlite_pmc()->set(get_emoji_language_code_version_database_key(language_code), to_string(version),
                                        mpas.get_promise());
    G()->td_db()->get_sqlite_pmc()->set(get_emoji_language_code_last_difference_time_database_key(language_code),
                                        to_string(G()->unix_time()), mpas.get_promise());
  }
  emoji_language_code_versions_[language_code] = version;
  emoji_language_code_last_difference_times_[language_code] = static_cast<int32>(Time::now_cached());

  lock.set_value(Unit());
}

void StickersManager::load_emoji_keywords_difference(const string &language_code) {
  LOG(INFO) << "Load emoji keywords difference for language " << language_code;
  emoji_language_code_last_difference_times_[language_code] =
      Time::now_cached() + 1e9;  // prevent simultaneous requests
  int32 from_version = get_emoji_language_code_version(language_code);
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), language_code,
       from_version](Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result) mutable {
        send_closure(actor_id, &StickersManager::on_get_emoji_keywords_difference, language_code, from_version,
                     std::move(result));
      });
  td_->create_handler<GetEmojiKeywordsDifferenceQuery>(std::move(query_promise))->send(language_code, from_version);
}

void StickersManager::on_get_emoji_keywords_difference(
    const string &language_code, int32 from_version,
    Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result) {
  if (result.is_error()) {
    if (!G()->is_expected_error(result.error())) {
      LOG(ERROR) << "Receive " << result.error() << " from GetEmojiKeywordsDifferenceQuery";
    }
    emoji_language_code_last_difference_times_[language_code] = Time::now_cached() - EMOJI_KEYWORDS_UPDATE_DELAY - 2;
    return;
  }

  auto version = get_emoji_language_code_version(language_code);
  CHECK(version == from_version);

  auto keywords = result.move_as_ok();
  LOG(INFO) << "Receive " << keywords->keywords_.size() << " emoji keywords difference for language " << language_code;
  LOG_IF(ERROR, language_code != keywords->lang_code_)
      << "Receive keywords for " << keywords->lang_code_ << " instead of " << language_code;
  LOG_IF(ERROR, keywords->from_version_ != from_version)
      << "Receive keywords from version " << keywords->from_version_ << " instead of " << from_version;
  if (keywords->version_ < version) {
    LOG(ERROR) << "Receive keywords of version " << keywords->version_ << ", but have of version " << version;
    keywords->version_ = version;
  }
  version = keywords->version_;
  auto *pmc = G()->td_db()->get_sqlite_sync_pmc();
  pmc->begin_transaction().ensure();
  for (auto &keyword_ptr : keywords->keywords_) {
    switch (keyword_ptr->get_id()) {
      case telegram_api::emojiKeyword::ID: {
        auto keyword = telegram_api::move_object_as<telegram_api::emojiKeyword>(keyword_ptr);
        auto text = utf8_to_lower(keyword->keyword_);
        bool is_good = true;
        for (auto &emoji : keyword->emoticons_) {
          if (emoji.find('$') != string::npos) {
            LOG(ERROR) << "Receive emoji \"" << emoji << "\" from server for " << text;
            is_good = false;
          }
        }
        if (is_good) {
          vector<string> emojis = search_language_emojis(language_code, text, true);
          bool is_changed = false;
          for (auto &emoji : keyword->emoticons_) {
            if (!td::contains(emojis, emoji)) {
              emojis.push_back(emoji);
              is_changed = true;
            }
          }
          if (is_changed) {
            pmc->set(get_language_emojis_database_key(language_code, text), implode(emojis, '$'));
          } else {
            LOG(ERROR) << "Emoji keywords not changed for \"" << text << "\" from version " << from_version
                       << " to version " << version;
          }
        }
        break;
      }
      case telegram_api::emojiKeywordDeleted::ID: {
        auto keyword = telegram_api::move_object_as<telegram_api::emojiKeywordDeleted>(keyword_ptr);
        auto text = utf8_to_lower(keyword->keyword_);
        vector<string> emojis = search_language_emojis(language_code, text, true);
        bool is_changed = false;
        for (auto &emoji : keyword->emoticons_) {
          if (td::remove(emojis, emoji)) {
            is_changed = true;
          }
        }
        if (is_changed) {
          pmc->set(get_language_emojis_database_key(language_code, text), implode(emojis, '$'));
        } else {
          LOG(ERROR) << "Emoji keywords not changed for \"" << text << "\" from version " << from_version
                     << " to version " << version;
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  pmc->set(get_emoji_language_code_version_database_key(language_code), to_string(version));
  pmc->set(get_emoji_language_code_last_difference_time_database_key(language_code), to_string(G()->unix_time()));
  pmc->commit_transaction().ensure();
  emoji_language_code_versions_[language_code] = version;
  emoji_language_code_last_difference_times_[language_code] = static_cast<int32>(Time::now_cached());
}

vector<string> StickersManager::search_emojis(const string &text, bool exact_match,
                                              const vector<string> &input_language_codes, bool force,
                                              Promise<Unit> &&promise) {
  if (text.empty() || !G()->parameters().use_file_db /* have SQLite PMC */) {
    promise.set_value(Unit());
    return {};
  }

  auto language_codes = get_emoji_language_codes(input_language_codes, text, promise);
  if (language_codes.empty()) {
    // promise was consumed
    return {};
  }

  vector<string> languages_to_load;
  for (auto &language_code : language_codes) {
    auto version = get_emoji_language_code_version(language_code);
    if (version == 0) {
      languages_to_load.push_back(language_code);
    } else {
      LOG(DEBUG) << "Found language " << language_code << " with version " << version;
    }
  }

  if (!languages_to_load.empty()) {
    if (!force) {
      MultiPromiseActorSafe mpas{"LoadEmojiLanguagesMultiPromiseActor"};
      mpas.add_promise(std::move(promise));

      auto lock = mpas.get_promise();
      for (auto &language_code : languages_to_load) {
        load_emoji_keywords(language_code, mpas.get_promise());
      }
      lock.set_value(Unit());
      return {};
    } else {
      LOG(ERROR) << "Have no " << languages_to_load << " emoji keywords";
    }
  }

  auto text_lowered = utf8_to_lower(text);
  vector<string> result;
  for (auto &language_code : language_codes) {
    combine(result, search_language_emojis(language_code, text_lowered, exact_match));
  }

  td::unique(result);

  promise.set_value(Unit());
  return result;
}

int64 StickersManager::get_emoji_suggestions_url(const string &language_code, Promise<Unit> &&promise) {
  int64 random_id = 0;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || emoji_suggestions_urls_.find(random_id) != emoji_suggestions_urls_.end());
  emoji_suggestions_urls_[random_id];  // reserve place for result

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), random_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::emojiURL>> &&result) mutable {
        send_closure(actor_id, &StickersManager::on_get_emoji_suggestions_url, random_id, std::move(promise),
                     std::move(result));
      });
  td_->create_handler<GetEmojiUrlQuery>(std::move(query_promise))->send(language_code);
  return random_id;
}

void StickersManager::on_get_emoji_suggestions_url(
    int64 random_id, Promise<Unit> &&promise, Result<telegram_api::object_ptr<telegram_api::emojiURL>> &&r_emoji_url) {
  auto it = emoji_suggestions_urls_.find(random_id);
  CHECK(it != emoji_suggestions_urls_.end());
  auto &result = it->second;
  CHECK(result.empty());

  if (r_emoji_url.is_error()) {
    emoji_suggestions_urls_.erase(it);
    return promise.set_error(r_emoji_url.move_as_error());
  }

  auto emoji_url = r_emoji_url.move_as_ok();
  result = std::move(emoji_url->url_);
  promise.set_value(Unit());
}

td_api::object_ptr<td_api::httpUrl> StickersManager::get_emoji_suggestions_url_result(int64 random_id) {
  auto it = emoji_suggestions_urls_.find(random_id);
  CHECK(it != emoji_suggestions_urls_.end());
  auto result = td_api::make_object<td_api::httpUrl>(it->second);
  emoji_suggestions_urls_.erase(it);
  return result;
}

void StickersManager::after_get_difference() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  if (td_->is_online()) {
    get_installed_sticker_sets(false, Auto());
    get_installed_sticker_sets(true, Auto());
    get_featured_sticker_sets(0, 1000, Auto());
    get_recent_stickers(false, Auto());
    get_recent_stickers(true, Auto());
    get_favorite_stickers(Auto());

    reload_special_sticker_set(add_special_sticker_set(SpecialStickerSetType::animated_emoji()));
  }
}

void StickersManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  for (int is_masks = 0; is_masks < 2; is_masks++) {
    if (are_installed_sticker_sets_loaded_[is_masks]) {
      updates.push_back(get_update_installed_sticker_sets_object(is_masks));
    }
  }
  if (are_featured_sticker_sets_loaded_) {
    updates.push_back(get_update_trending_sticker_sets_object());
  }
  for (int is_attached = 0; is_attached < 2; is_attached++) {
    if (are_recent_stickers_loaded_[is_attached]) {
      updates.push_back(get_update_recent_stickers_object(is_attached));
    }
  }
  if (are_favorite_stickers_loaded_) {
    updates.push_back(get_update_favorite_stickers_object());
  }
  if (!dice_emojis_.empty()) {
    updates.push_back(get_update_dice_emojis_object());
  }
}

}  // namespace td
