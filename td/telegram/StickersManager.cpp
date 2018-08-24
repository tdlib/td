//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"

#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <type_traits>
#include <unordered_set>

namespace td {

class GetAllStickersQuery : public Td::ResultHandler {
  bool is_masks_;

 public:
  void send(bool is_masks, int32 hash) {
    is_masks_ = is_masks;
    if (is_masks) {
      send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getMaskStickers(hash))));
    } else {
      send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getAllStickers(hash))));
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
    LOG(ERROR) << "Receive error for get all stickers: " << status;
    td->stickers_manager_->on_get_installed_sticker_sets_failed(is_masks_, std::move(status));
  }
};

class SearchStickersQuery : public Td::ResultHandler {
  string emoji_;

 public:
  void send(string emoji) {
    emoji_ = std::move(emoji);
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getStickers(emoji_, 0))));
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
    LOG(ERROR) << "Receive error for search stickers: " << status;
    td->stickers_manager_->on_find_stickers_fail(emoji_, std::move(status));
  }
};

class GetArchivedStickerSetsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  bool is_masks_;

 public:
  explicit GetArchivedStickerSetsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_masks, int64 offset_sticker_set_id, int32 limit) {
    is_masks_ = is_masks;
    LOG(INFO) << "Get archived " << (is_masks ? "mask" : "sticker") << " sets from " << offset_sticker_set_id
              << " with limit " << limit;

    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::messages_getArchivedStickers::MASKS_MASK;
    }
    is_masks_ = is_masks;

    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::messages_getArchivedStickers(flags, is_masks /*ignored*/, offset_sticker_set_id, limit))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getArchivedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetArchivedStickerSetsQuery " << to_string(ptr);
    td->stickers_manager_->on_get_archived_sticker_sets(is_masks_, std::move(ptr->sets_), ptr->count_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetFeaturedStickerSetsQuery : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    LOG(INFO) << "Get featured sticker sets with hash " << hash;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getFeaturedStickers(hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getFeaturedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetFeaturedStickerSetsQuery " << to_string(ptr);
    td->stickers_manager_->on_get_featured_sticker_sets(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    td->stickers_manager_->on_get_featured_sticker_sets_failed(std::move(status));
  }
};

class GetAttachedStickerSetsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;

 public:
  explicit GetAttachedStickerSetsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, tl_object_ptr<telegram_api::InputStickeredMedia> &&input_stickered_media) {
    file_id_ = file_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_getAttachedStickers(std::move(input_stickered_media)))));
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
    promise_.set_error(std::move(status));
  }
};

class GetRecentStickersQuery : public Td::ResultHandler {
  bool is_attached_;

 public:
  void send(bool is_attached, int32 hash) {
    is_attached_ = is_attached;
    int32 flags = 0;
    if (is_attached) {
      flags |= telegram_api::messages_getRecentStickers::ATTACHED_MASK;
    }

    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_getRecentStickers(flags, is_attached /*ignored*/, hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getRecentStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for get recent " << (is_attached_ ? "attached " : "")
               << "stickers: " << to_string(ptr);
    td->stickers_manager_->on_get_recent_stickers(is_attached_, std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for get recent stickers: " << status;
    td->stickers_manager_->on_get_recent_stickers_failed(is_attached_, std::move(status));
  }
};

class SaveRecentStickerQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  bool is_attached_;

 public:
  explicit SaveRecentStickerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_attached, tl_object_ptr<telegram_api::InputDocument> &&input_document, bool unsave) {
    is_attached_ = is_attached;

    int32 flags = 0;
    if (is_attached) {
      flags |= telegram_api::messages_saveRecentSticker::ATTACHED_MASK;
    }

    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::messages_saveRecentSticker(flags, is_attached /*ignored*/, std::move(input_document), unsave))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_saveRecentSticker>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for save recent sticker: " << result;
    if (!result) {
      td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for save recent sticker: " << status;
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

    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_clearRecentStickers(flags, is_attached /*ignored*/))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_clearRecentStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for clear recent stickers: " << result;
    if (!result) {
      td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for clear recent stickers: " << status;
    td->stickers_manager_->reload_recent_stickers(is_attached_, true);
    promise_.set_error(std::move(status));
  }
};

class GetFavedStickersQuery : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    LOG(INFO) << "Send get favorite stickers request with hash = " << hash;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getFavedStickers(hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getFavedStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td->stickers_manager_->on_get_favorite_stickers(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for get favorite stickers: " << status;
    td->stickers_manager_->on_get_favorite_stickers_failed(std::move(status));
  }
};

class FaveStickerQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit FaveStickerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputDocument> &&input_document, bool unsave) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_faveSticker(std::move(input_document), unsave))));
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
    LOG(ERROR) << "Receive error for fave sticker: " << status;
    td->stickers_manager_->reload_favorite_stickers(true);
    promise_.set_error(std::move(status));
  }
};

class ReorderStickerSetsQuery : public Td::ResultHandler {
  bool is_masks_;

 public:
  void send(bool is_masks, vector<int64> sticker_set_ids) {
    is_masks_ = is_masks;
    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::messages_reorderStickerSets::MASKS_MASK;
    }
    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::messages_reorderStickerSets(flags, is_masks /*ignored*/, std::move(sticker_set_ids)))));
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
    LOG(ERROR) << "Receive error for ReorderStickerSetsQuery: " << status;
    td->stickers_manager_->reload_installed_sticker_sets(is_masks_, true);
  }
};

class GetStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 sticker_set_id_;

 public:
  explicit GetStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 sticker_set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set) {
    sticker_set_id_ = sticker_set_id;
    LOG(INFO) << "Load sticker set " << sticker_set_id << " from server: " << to_string(input_sticker_set);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_getStickerSet(std::move(input_sticker_set)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    //    LOG(DEBUG) << "Receive result for get sticker set " << to_string(ptr);
    td->stickers_manager_->on_get_messages_sticker_set(sticker_set_id_, std::move(ptr), true);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    LOG(INFO) << "Receive error for getStickerSet: " << status;
    td->stickers_manager_->on_load_sticker_set_fail(sticker_set_id_, status);
    promise_.set_error(std::move(status));
  }
};

class SearchStickerSetsQuery : public Td::ResultHandler {
  string query_;

 public:
  void send(string query) {
    query_ = std::move(query);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_searchStickerSets(0, false /*ignored*/, query_, 0))));
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
    LOG(ERROR) << "Receive error for search sticker sets: " << status;
    td->stickers_manager_->on_find_sticker_sets_fail(query_, std::move(status));
  }
};

class InstallStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 set_id_;
  bool is_archived_;

 public:
  explicit InstallStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_set, bool is_archived) {
    set_id_ = set_id;
    is_archived_ = is_archived;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_installStickerSet(std::move(input_set), is_archived))));
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
  int64 set_id_;

 public:
  explicit UninstallStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 set_id, tl_object_ptr<telegram_api::InputStickerSet> &&input_set) {
    set_id_ = set_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_uninstallStickerSet(std::move(input_set)))));
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
  void send(vector<int64> sticker_set_ids) {
    LOG(INFO) << "Read featured sticker sets " << format::as_array(sticker_set_ids);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_readFeaturedStickers(std::move(sticker_set_ids)))));
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
    LOG(ERROR) << "Receive error for ReadFeaturedStickerSetsQuery: " << status;
    td->stickers_manager_->reload_featured_sticker_sets(true);
  }
};

class UploadStickerFileQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;

 public:
  explicit UploadStickerFileQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputPeer> &&input_peer, FileId file_id,
            tl_object_ptr<telegram_api::InputMedia> &&input_media) {
    CHECK(input_peer != nullptr);
    CHECK(input_media != nullptr);
    file_id_ = file_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_uploadMedia(std::move(input_peer), std::move(input_media)))));
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
    promise_.set_error(std::move(status));
  }
};

class CreateNewStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CreateNewStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> &&input_user, const string &title, const string &short_name,
            bool is_masks, vector<tl_object_ptr<telegram_api::inputStickerSetItem>> &&input_stickers) {
    CHECK(input_user != nullptr);

    int32 flags = 0;
    if (is_masks) {
      flags |= telegram_api::stickers_createStickerSet::MASKS_MASK;
    }

    send_query(G()->net_query_creator().create(create_storer(telegram_api::stickers_createStickerSet(
        flags, false /*ignored*/, std::move(input_user), title, short_name, std::move(input_stickers)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_createStickerSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(0, result_ptr.move_as_ok(), true);

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
    send_query(G()->net_query_creator().create(create_storer(telegram_api::stickers_addStickerToSet(
        make_tl_object<telegram_api::inputStickerSetShortName>(short_name), std::move(input_sticker)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_addStickerToSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(0, result_ptr.move_as_ok(), true);

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

  void send(tl_object_ptr<telegram_api::InputDocument> &&input_document, int32 position) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::stickers_changeStickerPosition(std::move(input_document), position))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_changeStickerPosition>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(0, result_ptr.move_as_ok(), true);

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

  void send(tl_object_ptr<telegram_api::InputDocument> &&input_document) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::stickers_removeStickerFromSet(std::move(input_document)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::stickers_removeStickerFromSet>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->stickers_manager_->on_get_messages_sticker_set(0, result_ptr.move_as_ok(), true);

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
  vector<int64> sticker_set_ids;

  StickerSetListLogEvent() = default;

  explicit StickerSetListLogEvent(vector<int64> sticker_set_ids) : sticker_set_ids(std::move(sticker_set_ids)) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    StickersManager *stickers_manager = storer.context()->td().get_actor_unsafe()->stickers_manager_.get();
    td::store(narrow_cast<int32>(sticker_set_ids.size()), storer);
    for (auto sticker_set_id : sticker_set_ids) {
      stickers_manager->store_sticker_set_id(sticker_set_id, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    StickersManager *stickers_manager = parser.context()->td().get_actor_unsafe()->stickers_manager_.get();
    int32 size = parser.fetch_int();
    sticker_set_ids.resize(size);
    for (auto &sticker_set_id : sticker_set_ids) {
      stickers_manager->parse_sticker_set_id(sticker_set_id, parser);
    }
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

  on_update_recent_stickers_limit(G()->shared_config().get_option_integer("recent_stickers_limit", 200));
  on_update_favorite_stickers_limit(G()->shared_config().get_option_integer("favorite_stickers_limit", 5));
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

tl_object_ptr<td_api::sticker> StickersManager::get_sticker_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &sticker = stickers_[file_id];
  CHECK(sticker != nullptr);
  sticker->is_changed = false;

  auto mask_position = sticker->point >= 0
                           ? make_tl_object<td_api::maskPosition>(get_mask_point_object(sticker->point),
                                                                  sticker->x_shift, sticker->y_shift, sticker->scale)
                           : nullptr;

  const PhotoSize &thumbnail =
      sticker->sticker_thumbnail.file_id.is_valid() ? sticker->sticker_thumbnail : sticker->message_thumbnail;
  return make_tl_object<td_api::sticker>(sticker->set_id, sticker->dimensions.width, sticker->dimensions.height,
                                         sticker->alt, sticker->is_mask, std::move(mask_position),
                                         get_photo_size_object(td_->file_manager_.get(), &thumbnail),
                                         td_->file_manager_->get_file_object(file_id));
}

tl_object_ptr<td_api::stickers> StickersManager::get_stickers_object(const vector<FileId> &sticker_ids) {
  auto result = make_tl_object<td_api::stickers>();
  result->stickers_.reserve(sticker_ids.size());
  for (auto sticker_id : sticker_ids) {
    result->stickers_.push_back(get_sticker_object(sticker_id));
  }
  return result;
}

tl_object_ptr<td_api::stickerSet> StickersManager::get_sticker_set_object(int64 sticker_set_id) {
  const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  CHECK(sticker_set->was_loaded);

  std::vector<tl_object_ptr<td_api::sticker>> stickers;
  std::vector<tl_object_ptr<td_api::stickerEmojis>> emojis;
  for (auto sticker_id : sticker_set->sticker_ids) {
    stickers.push_back(get_sticker_object(sticker_id));

    auto it = sticker_set->sticker_emojis_map_.find(sticker_id);
    if (it == sticker_set->sticker_emojis_map_.end()) {
      emojis.push_back(Auto());
    } else {
      emojis.push_back(make_tl_object<td_api::stickerEmojis>(vector<string>(it->second)));
    }
  }
  return make_tl_object<td_api::stickerSet>(sticker_set->id, sticker_set->title, sticker_set->short_name,
                                            sticker_set->is_installed && !sticker_set->is_archived,
                                            sticker_set->is_archived, sticker_set->is_official, sticker_set->is_masks,
                                            sticker_set->is_viewed, std::move(stickers), std::move(emojis));
}

tl_object_ptr<td_api::stickerSets> StickersManager::get_sticker_sets_object(int32 total_count,
                                                                            const vector<int64> &sticker_set_ids,
                                                                            size_t covers_limit) {
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

tl_object_ptr<td_api::stickerSetInfo> StickersManager::get_sticker_set_info_object(int64 sticker_set_id,
                                                                                   size_t covers_limit) {
  const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  CHECK(sticker_set->is_inited);

  std::vector<tl_object_ptr<td_api::sticker>> stickers;
  for (auto sticker_id : sticker_set->sticker_ids) {
    stickers.push_back(get_sticker_object(sticker_id));
    if (stickers.size() >= covers_limit) {
      break;
    }
  }

  return make_tl_object<td_api::stickerSetInfo>(
      sticker_set->id, sticker_set->title, sticker_set->short_name,
      sticker_set->is_installed && !sticker_set->is_archived, sticker_set->is_archived, sticker_set->is_official,
      sticker_set->is_masks, sticker_set->is_viewed,
      sticker_set->was_loaded ? narrow_cast<int32>(sticker_set->sticker_ids.size()) : sticker_set->sticker_count,
      std::move(stickers));
}

tl_object_ptr<telegram_api::InputStickerSet> StickersManager::get_input_sticker_set(int64 sticker_set_id) const {
  auto sticker_set = get_sticker_set(sticker_set_id);
  if (sticker_set == nullptr) {
    return nullptr;
  }

  return get_input_sticker_set(sticker_set);
}

FileId StickersManager::on_get_sticker(std::unique_ptr<Sticker> new_sticker, bool replace) {
  auto file_id = new_sticker->file_id;
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
    if (s->set_id != new_sticker->set_id && new_sticker->set_id != 0) {
      LOG_IF(ERROR, s->set_id != 0) << "Sticker " << file_id << " set_id has changed";
      s->set_id = new_sticker->set_id;
      s->is_changed = true;
    }
    if (s->alt != new_sticker->alt && !new_sticker->alt.empty()) {
      LOG(DEBUG) << "Sticker " << file_id << " emoji has changed";
      s->alt = new_sticker->alt;
      s->is_changed = true;
    }
    if (s->message_thumbnail != new_sticker->message_thumbnail && new_sticker->message_thumbnail.file_id.is_valid()) {
      LOG_IF(INFO, s->message_thumbnail.file_id.is_valid())
          << "Sticker " << file_id << " message thumbnail has changed from " << s->message_thumbnail << " to "
          << new_sticker->message_thumbnail;
      s->message_thumbnail = new_sticker->message_thumbnail;
      s->is_changed = true;
    }
    if (s->sticker_thumbnail != new_sticker->sticker_thumbnail && new_sticker->sticker_thumbnail.file_id.is_valid()) {
      LOG_IF(INFO, s->sticker_thumbnail.file_id.is_valid())
          << "Sticker " << file_id << " thumbnail has changed from " << s->sticker_thumbnail << " to "
          << new_sticker->sticker_thumbnail;
      s->sticker_thumbnail = new_sticker->sticker_thumbnail;
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

bool StickersManager::has_webp_thumbnail(const tl_object_ptr<telegram_api::documentAttributeSticker> &sticker) {
  if (sticker == nullptr) {
    return false;
  }

  return get_sticker_set_id(sticker->stickerset_) != 0;
}

std::pair<int64, FileId> StickersManager::on_get_sticker_document(tl_object_ptr<telegram_api::Document> &&document_ptr,
                                                                  bool from_message) {
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

  Dimensions dimensions;
  tl_object_ptr<telegram_api::documentAttributeSticker> sticker;
  for (auto &attribute : document->attributes_) {
    switch (attribute->get_id()) {
      case telegram_api::documentAttributeImageSize::ID: {
        auto image_size = move_tl_object_as<telegram_api::documentAttributeImageSize>(attribute);
        dimensions = get_dimensions(image_size->w_, image_size->h_);
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
    LOG(ERROR) << "Have no attributeSticker in sticker " << to_string(document);
    return {};
  }

  int64 document_id = document->id_;
  FileId sticker_id = td_->file_manager_->register_remote(
      FullRemoteFileLocation(FileType::Sticker, document_id, document->access_hash_, DcId::internal(document->dc_id_)),
      FileLocationSource::FromServer, DialogId(), document->size_, 0, PSTRING() << document_id << ".webp");

  PhotoSize thumbnail = get_photo_size(td_->file_manager_.get(), FileType::Thumbnail, 0, 0, DialogId(),
                                       std::move(document->thumb_), has_webp_thumbnail(sticker));

  create_sticker(sticker_id, std::move(thumbnail), dimensions, from_message, std::move(sticker), nullptr);
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

StickersManager::StickerSet *StickersManager::get_sticker_set(int64 sticker_set_id) {
  auto sticker_set = sticker_sets_.find(sticker_set_id);
  if (sticker_set == sticker_sets_.end()) {
    return nullptr;
  }

  return sticker_set->second.get();
}

const StickersManager::StickerSet *StickersManager::get_sticker_set(int64 sticker_set_id) const {
  auto sticker_set = sticker_sets_.find(sticker_set_id);
  if (sticker_set == sticker_sets_.end()) {
    return nullptr;
  }

  return sticker_set->second.get();
}

int64 StickersManager::get_sticker_set_id(const tl_object_ptr<telegram_api::InputStickerSet> &set_ptr) {
  CHECK(set_ptr != nullptr);
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return 0;
    case telegram_api::inputStickerSetID::ID:
      return static_cast<const telegram_api::inputStickerSetID *>(set_ptr.get())->id_;
    case telegram_api::inputStickerSetShortName::ID:
      LOG(ERROR) << "Receive sticker set by its short name";
      return search_sticker_set(static_cast<const telegram_api::inputStickerSetShortName *>(set_ptr.get())->short_name_,
                                Auto());
    default:
      UNREACHABLE();
      return 0;
  }
}

int64 StickersManager::add_sticker_set(tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr) {
  CHECK(set_ptr != nullptr);
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return 0;
    case telegram_api::inputStickerSetID::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetID>(set_ptr);
      int64 set_id = set->id_;
      add_sticker_set(set_id, set->access_hash_);
      return set_id;
    }
    case telegram_api::inputStickerSetShortName::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetShortName>(set_ptr);
      LOG(ERROR) << "Receive sticker set by its short name";
      return search_sticker_set(set->short_name_, Auto());
    }
    default:
      UNREACHABLE();
      return 0;
  }
}

StickersManager::StickerSet *StickersManager::add_sticker_set(int64 sticker_set_id, int64 access_hash) {
  auto &s = sticker_sets_[sticker_set_id];
  if (s == nullptr) {
    s = make_unique<StickerSet>();

    s->id = sticker_set_id;
    s->access_hash = access_hash;
    s->is_changed = false;
  } else {
    CHECK(s->id == sticker_set_id);
    if (s->access_hash != access_hash) {
      s->access_hash = access_hash;
      s->is_changed = true;
    }
  }
  return s.get();
}

FileId StickersManager::get_sticker_thumbnail_file_id(FileId file_id) const {
  auto sticker = get_sticker(file_id);
  CHECK(sticker != nullptr);
  return sticker->message_thumbnail.file_id;
}

void StickersManager::delete_sticker_thumbnail(FileId file_id) {
  auto &sticker = stickers_[file_id];
  CHECK(sticker != nullptr);
  sticker->message_thumbnail = PhotoSize();
}

FileId StickersManager::dup_sticker(FileId new_id, FileId old_id) {
  const Sticker *old_sticker = get_sticker(old_id);
  CHECK(old_sticker != nullptr);
  auto &new_sticker = stickers_[new_id];
  CHECK(!new_sticker);
  new_sticker = std::make_unique<Sticker>(*old_sticker);
  new_sticker->file_id = new_id;
  // there is no reason to dup sticker_thumb
  new_sticker->message_thumbnail.file_id = td_->file_manager_->dup_file_id(new_sticker->message_thumbnail.file_id);
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

    if (old_->alt != new_->alt || old_->set_id != new_->set_id ||
        (old_->dimensions.width != 0 && old_->dimensions.height != 0 && old_->dimensions != new_->dimensions)) {
      LOG(ERROR) << "Sticker has changed: alt = (" << old_->alt << ", " << new_->alt << "), set_id = (" << old_->set_id
                 << ", " << new_->set_id << "), dimensions = (" << old_->dimensions << ", " << new_->dimensions << ")";
    }

    new_->is_changed = true;

    if (old_->message_thumbnail != new_->message_thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->message_thumbnail.file_id, old_->message_thumbnail.file_id));
    }
    if (old_->sticker_thumbnail != new_->sticker_thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->sticker_thumbnail.file_id, old_->sticker_thumbnail.file_id));
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
  return make_tl_object<telegram_api::inputStickerSetID>(set->id, set->access_hash);
}

void StickersManager::reload_installed_sticker_sets(bool is_masks, bool force) {
  auto &next_load_time = next_installed_sticker_sets_load_time_[is_masks];
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload sticker sets";
    next_load_time = -1;
    td_->create_handler<GetAllStickersQuery>()->send(is_masks, installed_sticker_sets_hash_[is_masks]);
  }
}

void StickersManager::reload_featured_sticker_sets(bool force) {
  if (!td_->auth_manager_->is_bot() && next_featured_sticker_sets_load_time_ >= 0 &&
      (next_featured_sticker_sets_load_time_ < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload featured sticker sets";
    next_featured_sticker_sets_load_time_ = -1;
    td_->create_handler<GetFeaturedStickerSetsQuery>()->send(featured_sticker_sets_hash_);
  }
}

int64 StickersManager::on_get_input_sticker_set(FileId sticker_file_id,
                                                tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr,
                                                MultiPromiseActor *load_data_multipromise_ptr) {
  if (set_ptr == nullptr) {
    return 0;
  }
  switch (set_ptr->get_id()) {
    case telegram_api::inputStickerSetEmpty::ID:
      return 0;
    case telegram_api::inputStickerSetID::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetID>(set_ptr);
      int64 set_id = set->id_;
      add_sticker_set(set_id, set->access_hash_);
      return set_id;
    }
    case telegram_api::inputStickerSetShortName::ID: {
      auto set = move_tl_object_as<telegram_api::inputStickerSetShortName>(set_ptr);
      if (load_data_multipromise_ptr == nullptr) {
        LOG(ERROR) << "Receive sticker set by its short name";
        return search_sticker_set(set->short_name_, Auto());
      }
      auto set_id = search_sticker_set(set->short_name_, load_data_multipromise_ptr->get_promise());
      if (set_id == 0) {
        load_data_multipromise_ptr->add_promise(
            PromiseCreator::lambda([td = td_, sticker_file_id, short_name = set->short_name_](Result<Unit> result) {
              if (result.is_ok()) {
                // just in case
                td->stickers_manager_->on_resolve_sticker_set_short_name(sticker_file_id, short_name);
              }
            }));
      }
      return set_id;
    }
    default:
      UNREACHABLE();
      return 0;
  }
}

void StickersManager::on_resolve_sticker_set_short_name(FileId sticker_file_id, const string &short_name) {
  LOG(INFO) << "Resolve sticker " << sticker_file_id << " set to " << short_name;
  int64 set_id = search_sticker_set(short_name, Auto());
  if (set_id != 0) {
    auto &s = stickers_[sticker_file_id];
    if (s == nullptr) {
      LOG(ERROR) << "Can't find sticker " << sticker_file_id;
    }
    CHECK(s->file_id == sticker_file_id);
    if (s->set_id != set_id) {
      s->set_id = set_id;
      s->is_changed = true;
    }
  }
}

void StickersManager::create_sticker(FileId file_id, PhotoSize thumbnail, Dimensions dimensions, bool from_message,
                                     tl_object_ptr<telegram_api::documentAttributeSticker> sticker,
                                     MultiPromiseActor *load_data_multipromise_ptr) {
  auto s = make_unique<Sticker>();
  s->file_id = file_id;
  s->dimensions = dimensions;
  if (from_message) {
    s->message_thumbnail = std::move(thumbnail);
  } else {
    s->sticker_thumbnail = std::move(thumbnail);
  }
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
  on_get_sticker(std::move(s), sticker != nullptr);
}

bool StickersManager::has_input_media(FileId sticker_file_id, bool is_secret) const {
  const Sticker *sticker = get_sticker(sticker_file_id);
  CHECK(sticker != nullptr);
  auto file_view = td_->file_manager_->get_file_view(sticker_file_id);
  if (is_secret) {
    if (file_view.is_encrypted_secret()) {
      if (file_view.has_remote_location() && !sticker->message_thumbnail.file_id.is_valid()) {
        return true;
      }
    } else if (!file_view.is_encrypted()) {
      if (sticker->set_id != 0) {
        // stickers within a set can be sent by id and access_hash
        return true;
      }
    }
  } else {
    if (file_view.is_encrypted()) {
      return false;
    }
    if (file_view.has_remote_location() || file_view.has_url()) {
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
      input_file = file_view.remote_location().as_input_encrypted_file();
    }
    if (!input_file) {
      return {};
    }
    if (sticker->message_thumbnail.file_id.is_valid() && thumbnail.empty()) {
      return {};
    }
  } else if (!file_view.is_encrypted()) {
    if (sticker->set_id == 0) {
      // stickers without set can't be sent by id and access_hash
      return {};
    }
  } else {
    return {};
  }

  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  tl_object_ptr<secret_api::InputStickerSet> input_sticker_set = make_tl_object<secret_api::inputStickerSetEmpty>();
  if (sticker->set_id) {
    const StickerSet *sticker_set = get_sticker_set(sticker->set_id);
    CHECK(sticker_set != nullptr);
    if (sticker_set->is_inited) {
      input_sticker_set = make_tl_object<secret_api::inputStickerSetShortName>(sticker_set->short_name);
    } else {
      // TODO load sticker set
    }
  }
  attributes.push_back(
      make_tl_object<secret_api::documentAttributeSticker>(sticker->alt, std::move(input_sticker_set)));

  if (sticker->dimensions.width != 0 && sticker->dimensions.height != 0) {
    attributes.push_back(
        make_tl_object<secret_api::documentAttributeImageSize>(sticker->dimensions.width, sticker->dimensions.height));
  }

  if (file_view.is_encrypted_secret()) {
    auto &encryption_key = file_view.encryption_key();
    return SecretInputMedia{std::move(input_file),
                            make_tl_object<secret_api::decryptedMessageMediaDocument>(
                                std::move(thumbnail), sticker->message_thumbnail.dimensions.width,
                                sticker->message_thumbnail.dimensions.height, "image/webp",
                                narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
                                BufferSlice(encryption_key.iv_slice()), std::move(attributes), "")};
  } else {
    CHECK(!file_view.is_encrypted());
    auto &remote_location = file_view.remote_location();
    CHECK(!remote_location.is_web());  // web stickers shouldn't have set_id
    return SecretInputMedia{nullptr,
                            make_tl_object<secret_api::decryptedMessageMediaExternalDocument>(
                                remote_location.get_id(), remote_location.get_access_hash(), 0 /*date*/, "image/webp",
                                narrow_cast<int32>(file_view.size()), make_tl_object<secret_api::photoSizeEmpty>(),
                                remote_location.get_dc_id().get_raw_id(), std::move(attributes))};
  }
}

tl_object_ptr<telegram_api::InputMedia> StickersManager::get_input_media(
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
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), "image/webp",
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  }

  return nullptr;
}

int64 StickersManager::on_get_sticker_set(tl_object_ptr<telegram_api::stickerSet> &&set, bool is_changed) {
  int64 set_id = set->id_;
  StickerSet *s = add_sticker_set(set_id, set->access_hash_);

  bool is_installed = (set->flags_ & telegram_api::stickerSet::INSTALLED_DATE_MASK) != 0;
  bool is_archived = (set->flags_ & telegram_api::stickerSet::ARCHIVED_MASK) != 0;
  bool is_official = (set->flags_ & telegram_api::stickerSet::OFFICIAL_MASK) != 0;
  bool is_masks = (set->flags_ & telegram_api::stickerSet::MASKS_MASK) != 0;

  if (!s->is_inited) {
    s->is_inited = true;
    s->title = std::move(set->title_);
    s->short_name = std::move(set->short_name_);
    s->sticker_count = set->count_;
    s->hash = set->hash_;
    s->is_official = is_official;
    s->is_masks = is_masks;
    s->is_changed = true;
  } else {
    CHECK(s->id == set_id);
    if (s->access_hash != set->access_hash_) {
      LOG(INFO) << "Sticker set " << set_id << " access hash has changed";
      s->access_hash = set->access_hash_;
      s->is_changed = true;
    }
    if (s->title != set->title_) {
      LOG(INFO) << "Sticker set " << set_id << " title has changed";
      s->title = std::move(set->title_);
      s->is_changed = true;

      if (installed_sticker_sets_hints_[s->is_masks].has_key(set_id)) {
        installed_sticker_sets_hints_[s->is_masks].add(set_id, PSLICE() << s->title << ' ' << s->short_name);
      }
    }
    if (s->short_name != set->short_name_) {
      LOG(ERROR) << "Sticker set " << set_id << " short name has changed from \"" << s->short_name << "\" to \""
                 << set->short_name_ << "\"";
      short_name_to_sticker_set_id_.erase(clean_username(s->short_name));
      s->short_name = std::move(set->short_name_);
      s->is_changed = true;

      if (installed_sticker_sets_hints_[s->is_masks].has_key(set_id)) {
        installed_sticker_sets_hints_[s->is_masks].add(set_id, PSLICE() << s->title << ' ' << s->short_name);
      }
    }

    if (s->sticker_count != set->count_ || s->hash != set->hash_) {
      s->is_loaded = false;

      s->sticker_count = set->count_;
      s->hash = set->hash_;
      s->is_changed = true;
    }

    if (s->is_official != is_official) {
      s->is_official = is_official;
      s->is_changed = true;
    }
    LOG_IF(ERROR, s->is_masks != is_masks) << "Type of the sticker set " << set_id << " has changed";
  }
  short_name_to_sticker_set_id_.emplace(clean_username(s->short_name), set_id);

  on_update_sticker_set(s, is_installed, is_archived, is_changed);

  return set_id;
}

int64 StickersManager::on_get_sticker_set_covered(tl_object_ptr<telegram_api::StickerSetCovered> &&set_ptr,
                                                  bool is_changed) {
  int64 set_id = 0;
  switch (set_ptr->get_id()) {
    case telegram_api::stickerSetCovered::ID: {
      auto covered_set = move_tl_object_as<telegram_api::stickerSetCovered>(set_ptr);
      set_id = on_get_sticker_set(std::move(covered_set->set_), is_changed);
      if (set_id == 0) {
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

      auto sticker_id = on_get_sticker_document(std::move(covered_set->cover_), true).second;
      if (sticker_id.is_valid() && std::find(sticker_ids.begin(), sticker_ids.end(), sticker_id) == sticker_ids.end()) {
        sticker_ids.push_back(sticker_id);
        sticker_set->is_changed = true;
      }

      break;
    }
    case telegram_api::stickerSetMultiCovered::ID: {
      auto multicovered_set = move_tl_object_as<telegram_api::stickerSetMultiCovered>(set_ptr);
      set_id = on_get_sticker_set(std::move(multicovered_set->set_), is_changed);
      if (set_id == 0) {
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
        auto sticker_id = on_get_sticker_document(std::move(cover), true).second;
        if (sticker_id.is_valid() &&
            std::find(sticker_ids.begin(), sticker_ids.end(), sticker_id) == sticker_ids.end()) {
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

void StickersManager::on_get_messages_sticker_set(int64 sticker_set_id,
                                                  tl_object_ptr<telegram_api::messages_stickerSet> &&set,
                                                  bool is_changed) {
  LOG(INFO) << "Receive sticker set " << to_string(set);

  auto set_id = on_get_sticker_set(std::move(set->set_), is_changed);
  if (set_id == 0) {
    return;
  }
  if (sticker_set_id != 0 && sticker_set_id != set_id) {
    LOG(ERROR) << "Expected sticker set " << sticker_set_id << ", but receive sticker set " << set_id;
    on_load_sticker_set_fail(sticker_set_id, Status::Error(500, "Internal server error"));
    return;
  }

  auto s = get_sticker_set(set_id);
  CHECK(s != nullptr);
  CHECK(s->is_inited);

  s->expires_at = G()->unix_time() + (td_->auth_manager_->is_bot() ? Random::fast(10 * 60, 15 * 60)
                                                                   : Random::fast(20 * 60 * 60, 28 * 60 * 60));

  if (s->is_loaded) {
    update_sticker_set(s);
    send_update_installed_sticker_sets();
    return;
  }
  s->was_loaded = true;
  s->is_loaded = true;
  s->is_changed = true;

  vector<tl_object_ptr<telegram_api::stickerPack>> packs = std::move(set->packs_);
  vector<tl_object_ptr<telegram_api::Document>> documents = std::move(set->documents_);

  std::unordered_map<int64, FileId> document_id_to_sticker_id;

  s->sticker_ids.clear();
  for (auto &document_ptr : documents) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr), false);
    if (!sticker_id.second.is_valid()) {
      continue;
    }

    s->sticker_ids.push_back(sticker_id.second);
    document_id_to_sticker_id.insert(sticker_id);
  }
  if (static_cast<int>(s->sticker_ids.size()) != s->sticker_count) {
    LOG(ERROR) << "Wrong sticker set size specified";
    s->sticker_count = static_cast<int>(s->sticker_ids.size());
  }

  s->emoji_stickers_map_.clear();
  s->sticker_emojis_map_.clear();
  for (auto &pack : packs) {
    vector<FileId> stickers;
    stickers.reserve(pack->documents_.size());
    for (int64 document_id : pack->documents_) {
      auto it = document_id_to_sticker_id.find(document_id);
      if (it == document_id_to_sticker_id.end()) {
        LOG(ERROR) << "Can't find document with id " << document_id;
        continue;
      }

      stickers.push_back(it->second);
      s->sticker_emojis_map_[it->second].push_back(pack->emoticon_);
    }
    auto &sticker_ids = s->emoji_stickers_map_[remove_emoji_modifiers(pack->emoticon_)];
    for (auto sticker_id : stickers) {
      if (std::find(sticker_ids.begin(), sticker_ids.end(), sticker_id) == sticker_ids.end()) {
        sticker_ids.push_back(sticker_id);
      }
    }
  }

  update_sticker_set(s);
  update_load_requests(s, true, Status::OK());
  send_update_installed_sticker_sets();
}

void StickersManager::on_load_sticker_set_fail(int64 sticker_set_id, const Status &error) {
  if (sticker_set_id == 0) {
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

  std::unordered_set<int64> uninstalled_sticker_sets(installed_sticker_set_ids_[is_masks].begin(),
                                                     installed_sticker_set_ids_[is_masks].end());

  vector<int64> sets_to_load;
  vector<int64> installed_sticker_set_ids;
  vector<int32> hashes;
  vector<int64> sticker_set_ids;
  std::reverse(stickers->sets_.begin(), stickers->sets_.end());  // apply installed sticker sets in reverse order
  for (auto &set : stickers->sets_) {
    hashes.push_back(set->hash_);
    sticker_set_ids.push_back(set->id_);
    int64 set_id = on_get_sticker_set(std::move(set), false);
    if (set_id == 0) {
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
  std::reverse(hashes.begin(), hashes.end());
  std::reverse(installed_sticker_set_ids.begin(), installed_sticker_set_ids.end());
  std::reverse(sticker_set_ids.begin(), sticker_set_ids.end());

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
    LOG(ERROR) << "Sticker sets hash mismatch: server hash list = " << format::as_array(hashes)
               << ", client hash list = "
               << format::as_array(
                      transform(installed_sticker_set_ids_[is_masks],
                                [this](int64 sticker_set_id) { return get_sticker_set(sticker_set_id)->hash; }))
               << ", server sticker set list = " << format::as_array(sticker_set_ids)
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

  vector<int64> sets_to_load;
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
      if (std::find(prepend_sticker_ids.begin(), prepend_sticker_ids.end(), sticker_id) == prepend_sticker_ids.end()) {
        prepend_sticker_ids.push_back(sticker_id);
      }
    }

    LOG(INFO) << "Have " << recent_sticker_ids_[0] << " recent and " << favorite_sticker_ids_ << " favorite stickers";
    for (const auto &sticker_id : prepend_sticker_ids) {
      const Sticker *s = get_sticker(sticker_id);
      LOG(INFO) << "Have prepend sticker " << sticker_id << " from set " << s->set_id;
      if (s->set_id != 0 && std::find(sets_to_load.begin(), sets_to_load.end(), s->set_id) == sets_to_load.end()) {
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
    vector<int64> examined_sticker_set_ids;  // = featured_sticker_set_ids_;
    for (const auto &sticker_set_id : installed_sticker_set_ids_[0]) {
      if (std::find(examined_sticker_set_ids.begin(), examined_sticker_set_ids.end(), sticker_set_id) ==
          examined_sticker_set_ids.end()) {
        examined_sticker_set_ids.push_back(sticker_set_id);
      }
    }
    for (const auto &sticker_set_id : examined_sticker_set_ids) {
      const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
      if (sticker_set == nullptr || !sticker_set->was_loaded) {
        continue;
      }

      auto it = sticker_set->emoji_stickers_map_.find(emoji);
      if (it != sticker_set->emoji_stickers_map_.end()) {
        LOG(INFO) << "Add " << it->second << " stickers from set " << sticker_set_id;
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
        } else if (s->set_id != 0) {
          const StickerSet *sticker_set = get_sticker_set(s->set_id);
          if (sticker_set != nullptr && sticker_set->was_loaded) {
            auto map_it = sticker_set->emoji_stickers_map_.find(emoji);
            if (map_it != sticker_set->emoji_stickers_map_.end()) {
              if (std::find(map_it->second.begin(), map_it->second.end(), sticker_id) != map_it->second.end()) {
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
  if (it != found_stickers_.end()) {
    promise.set_value(Unit());
    auto result_size = min(static_cast<size_t>(limit), it->second.size());
    return vector<FileId>(it->second.begin(), it->second.begin() + result_size);
  }

  auto &promises = search_stickers_queries_[emoji];
  promises.push_back(std::move(promise));
  if (promises.size() == 1u) {
    td_->create_handler<SearchStickersQuery>()->send(std::move(emoji));
  }

  return {};
}

void StickersManager::on_find_stickers_success(const string &emoji,
                                               tl_object_ptr<telegram_api::messages_Stickers> &&stickers) {
  CHECK(stickers != nullptr);
  switch (stickers->get_id()) {
    case telegram_api::messages_stickersNotModified::ID:
      return on_find_stickers_fail(emoji, Status::Error(500, "Receive messages.stickerNotModified"));
    case telegram_api::messages_stickers::ID: {
      auto found_stickers = move_tl_object_as<telegram_api::messages_stickers>(stickers);
      vector<FileId> &sticker_ids = found_stickers_[emoji];
      CHECK(sticker_ids.empty());

      for (auto &sticker : found_stickers->stickers_) {
        FileId sticker_id = on_get_sticker_document(std::move(sticker), false).second;
        if (sticker_id.is_valid()) {
          sticker_ids.push_back(sticker_id);
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
  CHECK(found_stickers_.count(emoji) == 0);

  auto it = search_stickers_queries_.find(emoji);
  CHECK(it != search_stickers_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  search_stickers_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

vector<int64> StickersManager::get_installed_sticker_sets(bool is_masks, Promise<Unit> &&promise) {
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
        reload_sticker_set(set_id, get_input_sticker_set(sticker_set), std::move(promise));
        return true;
      } else {
        reload_sticker_set(set_id, get_input_sticker_set(sticker_set), Auto());
      }
    }
  }

  return false;
}

int64 StickersManager::get_sticker_set(int64 set_id, Promise<Unit> &&promise) {
  const StickerSet *sticker_set = get_sticker_set(set_id);
  if (sticker_set == nullptr) {
    if (set_id == GREAT_MINDS_SET_ID) {
      reload_sticker_set(set_id, make_tl_object<telegram_api::inputStickerSetID>(set_id, 0), std::move(promise));
      return 0;
    }

    promise.set_error(Status::Error(400, "Sticker set not found"));
    return 0;
  }

  if (update_sticker_set_cache(sticker_set, promise)) {
    return 0;
  }

  promise.set_value(Unit());
  return set_id;
}

int64 StickersManager::search_sticker_set(const string &short_name_to_search, Promise<Unit> &&promise) {
  string short_name = clean_username(short_name_to_search);
  auto it = short_name_to_sticker_set_id_.find(short_name);
  const StickerSet *sticker_set = it == short_name_to_sticker_set_id_.end() ? nullptr : get_sticker_set(it->second);

  if (sticker_set == nullptr) {
    auto set_to_load = make_tl_object<telegram_api::inputStickerSetShortName>(short_name);
    reload_sticker_set(0, std::move(set_to_load), std::move(promise));
    return 0;
  }

  if (update_sticker_set_cache(sticker_set, promise)) {
    return 0;
  }

  promise.set_value(Unit());
  return sticker_set->id;
}

std::pair<int32, vector<int64>> StickersManager::search_installed_sticker_sets(bool is_masks, const string &query,
                                                                               int32 limit, Promise<Unit> &&promise) {
  LOG(INFO) << "Search installed " << (is_masks ? "masks " : "") << "sticker sets with query = \"" << query
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
  return {narrow_cast<int32>(result.first), std::move(result.second)};
}

vector<int64> StickersManager::search_sticker_sets(const string &query, Promise<Unit> &&promise) {
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
      vector<int64> &sticker_set_ids = found_sticker_sets_[query];
      CHECK(sticker_set_ids.empty());

      for (auto &sticker_set : found_stickers_sets->sets_) {
        int64 set_id = on_get_sticker_set_covered(std::move(sticker_set), true);
        if (set_id == 0) {
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

void StickersManager::change_sticker_set(int64 set_id, bool is_installed, bool is_archived, Promise<Unit> &&promise) {
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
  LOG(INFO) << "Update sticker set " << sticker_set->id << ": installed = " << is_installed
            << ", archived = " << is_archived << ", changed = " << is_changed;
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
    vector<int64> &sticker_set_ids = installed_sticker_set_ids_[sticker_set->is_masks];
    need_update_installed_sticker_sets_[sticker_set->is_masks] = true;

    if (is_added) {
      installed_sticker_sets_hints_[sticker_set->is_masks].add(
          sticker_set->id, PSLICE() << sticker_set->title << ' ' << sticker_set->short_name);
      sticker_set_ids.insert(sticker_set_ids.begin(), sticker_set->id);
    } else {
      installed_sticker_sets_hints_[sticker_set->is_masks].remove(sticker_set->id);
      sticker_set_ids.erase(std::remove(sticker_set_ids.begin(), sticker_set_ids.end(), sticker_set->id),
                            sticker_set_ids.end());
    }
  }
  if (was_archived != is_archived && is_changed) {
    int32 &total_count = total_archived_sticker_set_count_[sticker_set->is_masks];
    vector<int64> &sticker_set_ids = archived_sticker_set_ids_[sticker_set->is_masks];
    if (total_count < 0) {
      return;
    }

    if (is_archived) {
      total_count++;
      sticker_set_ids.insert(sticker_set_ids.begin(), sticker_set->id);
    } else {
      total_count--;
      sticker_set_ids.erase(std::remove(sticker_set_ids.begin(), sticker_set_ids.end(), sticker_set->id),
                            sticker_set_ids.end());
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
      LOG(INFO) << "Trying to load installed " << (is_masks ? "masks " : "") << "sticker sets from database";
      G()->td_db()->get_sqlite_pmc()->get(is_masks ? "sss1" : "sss0", PromiseCreator::lambda([is_masks](string value) {
                                            send_closure(G()->stickers_manager(),
                                                         &StickersManager::on_load_installed_sticker_sets_from_database,
                                                         is_masks, std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load installed " << (is_masks ? "masks " : "") << "sticker sets from server";
      reload_installed_sticker_sets(is_masks, true);
    }
  }
}

void StickersManager::on_load_installed_sticker_sets_from_database(bool is_masks, string value) {
  if (value.empty()) {
    LOG(INFO) << "Installed " << (is_masks ? "mask " : "") << "sticker sets aren't found in database";
    reload_installed_sticker_sets(is_masks, true);
    return;
  }

  LOG(INFO) << "Successfully loaded installed " << (is_masks ? "mask " : "") << "sticker sets list of size "
            << value.size() << " from database";

  StickerSetListLogEvent log_event;
  log_event_parse(log_event, value).ensure();

  vector<int64> sets_to_load;
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
            }
          }));
}

void StickersManager::on_load_installed_sticker_sets_finished(bool is_masks, vector<int64> &&installed_sticker_set_ids,
                                                              bool from_database) {
  bool need_reload = false;
  vector<int64> old_installed_sticker_set_ids;
  if (!are_installed_sticker_sets_loaded_[is_masks] && !installed_sticker_set_ids_[is_masks].empty()) {
    old_installed_sticker_set_ids = std::move(installed_sticker_set_ids_[is_masks]);
  }
  installed_sticker_set_ids_[is_masks].clear();
  for (auto set_id : installed_sticker_set_ids) {
    CHECK(set_id != 0);

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
    LOG(ERROR) << "Reload installed " << (is_masks ? "masks " : "") << "sticker sets, because only "
               << installed_sticker_set_ids_[is_masks].size() << " of " << installed_sticker_set_ids.size()
               << " are really installed";
    reload_installed_sticker_sets(is_masks, true);
  } else if (!old_installed_sticker_set_ids.empty() &&
             old_installed_sticker_set_ids != installed_sticker_set_ids_[is_masks]) {
    LOG(ERROR) << "Reload installed " << (is_masks ? "masks " : "") << "sticker sets, because they has changed from "
               << old_installed_sticker_set_ids << " to " << installed_sticker_set_ids_[is_masks];
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

string StickersManager::get_sticker_set_database_key(int64 set_id) {
  return PSTRING() << "ss" << set_id;
}

string StickersManager::get_full_sticker_set_database_key(int64 set_id) {
  return PSTRING() << "ssf" << set_id;
}

string StickersManager::get_sticker_set_database_value(const StickerSet *s, bool with_stickers) {
  LogEventStorerCalcLength storer_calc_length;
  store_sticker_set(s, with_stickers, storer_calc_length);

  BufferSlice value_buffer{storer_calc_length.get_length()};
  auto value = value_buffer.as_slice();

  LOG(DEBUG) << "Sticker set " << s->id << " serialized size is " << value.size();

  LogEventStorerUnsafe storer_unsafe(value.ubegin());
  store_sticker_set(s, with_stickers, storer_unsafe);

  return value.str();
}

void StickersManager::update_sticker_set(StickerSet *sticker_set) {
  CHECK(sticker_set != nullptr);
  if (sticker_set->is_changed) {
    sticker_set->is_changed = false;
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Save sticker set " << sticker_set->id << " to database";
      if (sticker_set->is_inited) {
        G()->td_db()->get_sqlite_pmc()->set(get_sticker_set_database_key(sticker_set->id),
                                            get_sticker_set_database_value(sticker_set, false), Auto());
      }
      if (sticker_set->was_loaded) {
        G()->td_db()->get_sqlite_pmc()->set(get_full_sticker_set_database_key(sticker_set->id),
                                            get_sticker_set_database_value(sticker_set, true), Auto());
      }
    }
    if (sticker_set->is_inited) {
      update_load_requests(sticker_set, false, Status::OK());
    }
  }
}

void StickersManager::load_sticker_sets(vector<int64> &&sticker_set_ids, Promise<Unit> &&promise) {
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
        LOG(INFO) << "Trying to load sticker set " << sticker_set_id << " with stickers from database";
        G()->td_db()->get_sqlite_pmc()->get(
            get_full_sticker_set_database_key(sticker_set_id), PromiseCreator::lambda([sticker_set_id](string value) {
              send_closure(G()->stickers_manager(), &StickersManager::on_load_sticker_set_from_database, sticker_set_id,
                           true, std::move(value));
            }));
      } else {
        LOG(INFO) << "Trying to load sticker set " << sticker_set_id << " with stickers from server";
        reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
      }
    }
  }
}

void StickersManager::load_sticker_sets_without_stickers(vector<int64> &&sticker_set_ids, Promise<Unit> &&promise) {
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
          LOG(INFO) << "Trying to load sticker set " << sticker_set_id << " from database";
          G()->td_db()->get_sqlite_pmc()->get(
              get_sticker_set_database_key(sticker_set_id), PromiseCreator::lambda([sticker_set_id](string value) {
                send_closure(G()->stickers_manager(), &StickersManager::on_load_sticker_set_from_database,
                             sticker_set_id, false, std::move(value));
              }));
        } else {
          LOG(INFO) << "Trying to load sticker set " << sticker_set_id << " from server";
          reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
        }
      }
    }
  }
}

void StickersManager::on_load_sticker_set_from_database(int64 sticker_set_id, bool with_stickers, string value) {
  StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  if (sticker_set->was_loaded) {
    LOG(INFO) << "Sticker set " << sticker_set_id << " was loaded";
    return;
  }
  if (!with_stickers && sticker_set->is_inited) {
    LOG(INFO) << "Sticker set " << sticker_set_id << " was inited";
    return;
  }

  if (with_stickers) {
    CHECK(!sticker_set->load_requests.empty());
  } else {
    CHECK(!sticker_set->load_without_stickers_requests.empty());
  }
  if (value.empty()) {
    reload_sticker_set(sticker_set_id, get_input_sticker_set(sticker_set), Auto());
    return;
  }

  LOG(INFO) << "Successfully loaded sticker set " << sticker_set_id << " with" << (with_stickers ? "" : "out")
            << " stickers of size " << value.size() << " from database";

  auto old_sticker_count = sticker_set->sticker_ids.size();

  {
    LOG_IF(ERROR, sticker_set->is_changed) << "Sticker set with" << (with_stickers ? "" : "out") << " stickers "
                                           << sticker_set_id << " was changed before it is loaded from database";
    LogEventParser parser(value);
    parse_sticker_set(sticker_set, parser);
    LOG_IF(ERROR, sticker_set->is_changed)
        << "Sticker set with" << (with_stickers ? "" : "out") << " stickers " << sticker_set_id << " is changed";
    parser.fetch_end();
    parser.get_status().ensure();
  }

  if (with_stickers && old_sticker_count < 5 && old_sticker_count < sticker_set->sticker_ids.size()) {
    sticker_set->is_changed = true;
    update_sticker_set(sticker_set);
  }

  update_load_requests(sticker_set, with_stickers, Status::OK());
}

void StickersManager::reload_sticker_set(int64 sticker_set_id,
                                         tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set,
                                         Promise<Unit> &&promise) const {
  td_->create_handler<GetStickerSetQuery>(std::move(promise))->send(sticker_set_id, std::move(input_sticker_set));
}

void StickersManager::on_install_sticker_set(int64 set_id, bool is_archived,
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
        int64 archived_sticker_set_id = on_get_sticker_set_covered(std::move(archived_set_ptr), true);
        if (archived_sticker_set_id != 0) {
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

void StickersManager::on_uninstall_sticker_set(int64 set_id) {
  StickerSet *sticker_set = get_sticker_set(set_id);
  CHECK(sticker_set != nullptr);
  on_update_sticker_set(sticker_set, false, false, true);
  update_sticker_set(sticker_set);
  send_update_installed_sticker_sets();
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

void StickersManager::view_featured_sticker_sets(const vector<int64> &sticker_set_ids) {
  for (auto sticker_set_id : sticker_set_ids) {
    auto set = get_sticker_set(sticker_set_id);
    if (set != nullptr && !set->is_viewed) {
      need_update_featured_sticker_sets_ = true;
      set->is_viewed = true;
      pending_viewed_featured_sticker_set_ids_.insert(sticker_set_id);
      update_sticker_set(set);
    }
  }

  send_update_featured_sticker_sets();

  if (!pending_viewed_featured_sticker_set_ids_.empty() && !pending_featured_sticker_set_views_timeout_.has_timeout()) {
    LOG(INFO) << "Have pending viewed featured sticker sets";
    pending_featured_sticker_set_views_timeout_.set_callback(read_featured_sticker_sets);
    pending_featured_sticker_set_views_timeout_.set_callback_data(static_cast<void *>(td_));
    pending_featured_sticker_set_views_timeout_.set_timeout_in(MAX_FEATURED_STICKER_SET_VIEW_DELAY);
  }
}

void StickersManager::read_featured_sticker_sets(void *td_void) {
  CHECK(td_void != nullptr);
  auto td = static_cast<Td *>(td_void);

  auto &set_ids = td->stickers_manager_->pending_viewed_featured_sticker_set_ids_;
  td->create_handler<ReadFeaturedStickerSetsQuery>()->send(vector<int64>(set_ids.begin(), set_ids.end()));
  set_ids.clear();
}

std::pair<int32, vector<int64>> StickersManager::get_archived_sticker_sets(bool is_masks, int64 offset_sticker_set_id,
                                                                           int32 limit, bool force,
                                                                           Promise<Unit> &&promise) {
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return {};
  }

  vector<int64> &sticker_set_ids = archived_sticker_set_ids_[is_masks];
  int32 total_count = total_archived_sticker_set_count_[is_masks];
  if (total_count < 0) {
    total_count = 0;
  }

  if (!sticker_set_ids.empty()) {
    auto offset_it = sticker_set_ids.begin();
    if (offset_sticker_set_id != 0) {
      offset_it = std::find(sticker_set_ids.begin(), sticker_set_ids.end(), offset_sticker_set_id);
      if (offset_it == sticker_set_ids.end()) {
        offset_it = sticker_set_ids.begin();
      } else {
        ++offset_it;
      }
    }
    vector<int64> result;
    while (result.size() < static_cast<size_t>(limit)) {
      if (offset_it == sticker_set_ids.end()) {
        break;
      }
      auto sticker_set_id = *offset_it++;
      if (sticker_set_id == 0) {  // end of the list
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
    bool is_masks, vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets, int32 total_count) {
  vector<int64> &sticker_set_ids = archived_sticker_set_ids_[is_masks];
  if (!sticker_set_ids.empty() && sticker_set_ids.back() == 0) {
    return;
  }

  total_archived_sticker_set_count_[is_masks] = total_count;
  for (auto &sticker_set_covered : sticker_sets) {
    auto sticker_set_id = on_get_sticker_set_covered(std::move(sticker_set_covered), false);
    if (sticker_set_id != 0) {
      auto sticker_set = get_sticker_set(sticker_set_id);
      CHECK(sticker_set != nullptr);
      update_sticker_set(sticker_set);

      if (std::find(sticker_set_ids.begin(), sticker_set_ids.end(), sticker_set_id) == sticker_set_ids.end()) {
        sticker_set_ids.push_back(sticker_set_id);
      }
    }
  }
  if (sticker_set_ids.size() >= static_cast<size_t>(total_count)) {
    if (sticker_set_ids.size() > static_cast<size_t>(total_count)) {
      LOG(ERROR) << "Expected total of " << total_count << " archived sticker sets, but only " << sticker_set_ids.size()
                 << " found";
      total_archived_sticker_set_count_[is_masks] = static_cast<int32>(sticker_set_ids.size());
    }
    sticker_set_ids.push_back(0);
  }
  send_update_installed_sticker_sets();
}

vector<int64> StickersManager::get_featured_sticker_sets(Promise<Unit> &&promise) {
  if (!are_featured_sticker_sets_loaded_) {
    load_featured_sticker_sets(std::move(promise));
    return {};
  }
  reload_featured_sticker_sets(false);

  promise.set_value(Unit());
  return featured_sticker_set_ids_;
}

void StickersManager::on_get_featured_sticker_sets(
    tl_object_ptr<telegram_api::messages_FeaturedStickers> &&sticker_sets_ptr) {
  next_featured_sticker_sets_load_time_ = Time::now_cached() + Random::fast(30 * 60, 50 * 60);

  int32 constructor_id = sticker_sets_ptr->get_id();
  if (constructor_id == telegram_api::messages_featuredStickersNotModified::ID) {
    LOG(INFO) << "Featured stickers are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_featuredStickers::ID);
  auto featured_stickers = move_tl_object_as<telegram_api::messages_featuredStickers>(sticker_sets_ptr);

  vector<int64> featured_sticker_set_ids;
  std::unordered_set<int64> unread_sticker_set_ids(featured_stickers->unread_.begin(),
                                                   featured_stickers->unread_.end());
  for (auto &sticker_set : featured_stickers->sets_) {
    int64 set_id = on_get_sticker_set_covered(std::move(sticker_set), true);
    if (set_id == 0) {
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

  on_load_featured_sticker_sets_finished(std::move(featured_sticker_set_ids));

  LOG_IF(ERROR, featured_sticker_sets_hash_ != featured_stickers->hash_) << "Featured sticker sets hash mismatch";

  if (!G()->parameters().use_file_db) {
    return;
  }

  LOG(INFO) << "Save featured sticker sets to database";
  StickerSetListLogEvent log_event(featured_sticker_set_ids_);
  G()->td_db()->get_sqlite_pmc()->set("sssfeatured", log_event_store(log_event).as_slice().str(), Auto());
}

void StickersManager::on_get_featured_sticker_sets_failed(Status error) {
  CHECK(error.is_error());
  next_featured_sticker_sets_load_time_ = Time::now_cached() + Random::fast(5, 10);
  auto promises = std::move(load_featured_sticker_sets_queries_);
  load_featured_sticker_sets_queries_.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

void StickersManager::load_featured_sticker_sets(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_featured_sticker_sets_loaded_ = true;
  }
  if (are_featured_sticker_sets_loaded_) {
    promise.set_value(Unit());
    return;
  }
  load_featured_sticker_sets_queries_.push_back(std::move(promise));
  if (load_featured_sticker_sets_queries_.size() == 1u) {
    if (G()->parameters().use_file_db) {
      LOG(INFO) << "Trying to load featured sticker sets from database";
      G()->td_db()->get_sqlite_pmc()->get("sssfeatured", PromiseCreator::lambda([](string value) {
                                            send_closure(G()->stickers_manager(),
                                                         &StickersManager::on_load_featured_sticker_sets_from_database,
                                                         std::move(value));
                                          }));
    } else {
      LOG(INFO) << "Trying to load featured sticker sets from server";
      reload_featured_sticker_sets(true);
    }
  }
}

void StickersManager::on_load_featured_sticker_sets_from_database(string value) {
  if (value.empty()) {
    LOG(INFO) << "Featured sticker sets aren't found in database";
    reload_featured_sticker_sets(true);
    return;
  }

  LOG(INFO) << "Successfully loaded featured sticker sets list of size " << value.size() << " from database";

  StickerSetListLogEvent log_event;
  log_event_parse(log_event, value).ensure();

  vector<int64> sets_to_load;
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
        }
      }));
}

void StickersManager::on_load_featured_sticker_sets_finished(vector<int64> &&featured_sticker_set_ids) {
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

vector<int64> StickersManager::get_attached_sticker_sets(FileId file_id, Promise<Unit> &&promise) {
  if (!file_id.is_valid()) {
    promise.set_error(Status::Error(5, "Wrong file_id specified"));
    return {};
  }

  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.empty()) {
    promise.set_error(Status::Error(5, "File not found"));
    return {};
  }
  if (!file_view.has_remote_location() || !file_view.remote_location().is_document() ||
      file_view.remote_location().is_web()) {
    promise.set_value(Unit());
    return {};
  }

  auto it = attached_sticker_sets_.find(file_id);
  if (it != attached_sticker_sets_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  tl_object_ptr<telegram_api::InputStickeredMedia> input_stickered_media;
  if (file_view.remote_location().is_photo()) {
    input_stickered_media =
        make_tl_object<telegram_api::inputStickeredMediaPhoto>(file_view.remote_location().as_input_photo());
  } else {
    input_stickered_media =
        make_tl_object<telegram_api::inputStickeredMediaDocument>(file_view.remote_location().as_input_document());
  }

  td_->create_handler<GetAttachedStickerSetsQuery>(std::move(promise))->send(file_id, std::move(input_stickered_media));
  return {};
}

void StickersManager::on_get_attached_sticker_sets(
    FileId file_id, vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets) {
  vector<int64> &sticker_set_ids = attached_sticker_sets_[file_id];
  sticker_set_ids.clear();
  for (auto &sticker_set_covered : sticker_sets) {
    auto sticker_set_id = on_get_sticker_set_covered(std::move(sticker_set_covered), true);
    if (sticker_set_id != 0) {
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
int StickersManager::apply_installed_sticker_sets_order(bool is_masks, const vector<int64> &sticker_set_ids) {
  if (!are_installed_sticker_sets_loaded_[is_masks]) {
    return -1;
  }

  vector<int64> &current_sticker_set_ids = installed_sticker_set_ids_[is_masks];
  if (sticker_set_ids == current_sticker_set_ids) {
    return 0;
  }

  std::unordered_set<int64> valid_set_ids(current_sticker_set_ids.begin(), current_sticker_set_ids.end());
  vector<int64> new_sticker_set_ids;
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
    vector<int64> missed_sticker_set_ids;
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

void StickersManager::on_update_sticker_sets_order(bool is_masks, const vector<int64> &sticker_set_ids) {
  int result = apply_installed_sticker_sets_order(is_masks, sticker_set_ids);
  if (result < 0) {
    return reload_installed_sticker_sets(is_masks, true);
  }
  if (result > 0) {
    send_update_installed_sticker_sets();
  }
}

void StickersManager::reorder_installed_sticker_sets(bool is_masks, const vector<int64> &sticker_set_ids,
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

Result<std::tuple<FileId, bool, bool>> StickersManager::prepare_input_sticker(td_api::inputSticker *sticker) {
  if (sticker == nullptr) {
    return Status::Error(3, "Input sticker shouldn't be empty");
  }

  if (!clean_input_string(sticker->emojis_)) {
    return Status::Error(400, "Emojis must be encoded in UTF-8");
  }

  return prepare_input_file(sticker->png_sticker_);
}

Result<std::tuple<FileId, bool, bool>> StickersManager::prepare_input_file(
    const tl_object_ptr<td_api::InputFile> &input_file) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Document, input_file, {}, false, false, false);
  if (r_file_id.is_error()) {
    return Status::Error(7, r_file_id.error().message());
  }
  auto file_id = r_file_id.move_as_ok();

  td_->documents_manager_->create_document(file_id, PhotoSize(), "sticker.png", "image/png", false);

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return Status::Error(400, "Can't use encrypted file");
  }

  if (file_view.has_remote_location() && file_view.remote_location().is_web()) {
    return Status::Error(400, "Can't use web file to create a sticker");
  }
  bool is_url = false;
  bool is_local = false;
  if (file_view.has_remote_location()) {
    CHECK(file_view.remote_location().is_document());
  } else {
    if (file_view.has_url()) {
      is_url = true;
    } else {
      if (file_view.has_local_location() && file_view.local_size() > MAX_STICKER_FILE_SIZE) {
        return Status::Error(400, "File is too big");
      }
      is_local = true;
    }
  }
  return std::make_tuple(file_id, is_url, is_local);
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

  auto r_file_id = prepare_input_file(sticker);
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

tl_object_ptr<telegram_api::inputStickerSetItem> StickersManager::get_input_sticker(td_api::inputSticker *sticker,
                                                                                    FileId file_id) const {
  FileView file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(file_view.has_remote_location());
  auto input_document = file_view.remote_location().as_input_document();

  tl_object_ptr<telegram_api::maskCoords> mask_coords;
  if (sticker->mask_position_ != nullptr && sticker->mask_position_->point_ != nullptr) {
    auto point = [mask_point = std::move(sticker->mask_position_->point_)]() {
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
    mask_coords = make_tl_object<telegram_api::maskCoords>(
        point, sticker->mask_position_->x_shift_, sticker->mask_position_->y_shift_, sticker->mask_position_->scale_);
  }

  int32 flags = 0;
  if (mask_coords != nullptr) {
    flags |= telegram_api::inputStickerSetItem::MASK_COORDS_MASK;
  }

  return make_tl_object<telegram_api::inputStickerSetItem>(flags, std::move(input_document), sticker->emojis_,
                                                           std::move(mask_coords));
}

void StickersManager::create_new_sticker_set(UserId user_id, string &title, string &short_name, bool is_masks,
                                             vector<tl_object_ptr<td_api::inputSticker>> &&stickers,
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

  vector<FileId> file_ids;
  file_ids.reserve(stickers.size());
  vector<FileId> local_file_ids;
  vector<FileId> url_file_ids;
  for (auto &sticker : stickers) {
    auto r_file_id = prepare_input_sticker(sticker.get());
    if (r_file_id.is_error()) {
      return promise.set_error(r_file_id.move_as_error());
    }
    auto file_id = std::get<0>(r_file_id.ok());
    auto is_url = std::get<1>(r_file_id.ok());
    auto is_local = std::get<2>(r_file_id.ok());

    file_ids.push_back(file_id);
    if (is_url) {
      url_file_ids.push_back(file_id);
    } else if (is_local) {
      local_file_ids.push_back(file_id);
    }
  }

  auto pending_new_sticker_set = make_unique<PendingNewStickerSet>();
  pending_new_sticker_set->user_id = user_id;
  pending_new_sticker_set->title = std::move(title);
  pending_new_sticker_set->short_name = short_name;
  pending_new_sticker_set->is_masks = is_masks;
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
  CHECK(td_->documents_manager_->get_input_media(file_id, nullptr, nullptr) == nullptr);

  auto upload_file_id = td_->documents_manager_->dup_document(td_->file_manager_->dup_file_id(file_id), file_id);

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

  auto input_media = td_->documents_manager_->get_input_media(file_id, std::move(input_file), nullptr);
  CHECK(input_media != nullptr);

  td_->create_handler<UploadStickerFileQuery>(std::move(promise))
      ->send(std::move(input_peer), file_id, std::move(input_media));
}

void StickersManager::on_uploaded_sticker_file(FileId file_id, tl_object_ptr<telegram_api::MessageMedia> media,
                                               Promise<Unit> &&promise) {
  CHECK(media != nullptr);
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

  auto parsed_document = td_->documents_manager_->on_get_document(
      move_tl_object_as<telegram_api::document>(document_ptr), DialogId(), nullptr);
  if (parsed_document.first != DocumentsManager::DocumentType::General) {
    return promise.set_error(Status::Error(400, "Wrong file type"));
  }

  td_->documents_manager_->merge_documents(parsed_document.second, file_id, true);
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

  auto sticker_count = pending_new_sticker_set->stickers.size();
  vector<tl_object_ptr<telegram_api::inputStickerSetItem>> input_stickers;
  input_stickers.reserve(sticker_count);
  for (size_t i = 0; i < sticker_count; i++) {
    input_stickers.push_back(
        get_input_sticker(pending_new_sticker_set->stickers[i].get(), pending_new_sticker_set->file_ids[i]));
  }

  td_->create_handler<CreateNewStickerSetQuery>(std::move(pending_new_sticker_set->promise))
      ->send(std::move(input_user), pending_new_sticker_set->title, pending_new_sticker_set->short_name, is_masks,
             std::move(input_stickers));
}

void StickersManager::add_sticker_to_set(UserId user_id, string &short_name,
                                         tl_object_ptr<td_api::inputSticker> &&sticker, Promise<Unit> &&promise) {
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
  if (!file_view.has_remote_location() || !file_view.remote_location().is_document() ||
      file_view.remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Wrong sticker file specified"));
  }

  td_->create_handler<SetStickerPositionQuery>(std::move(promise))
      ->send(file_view.remote_location().as_input_document(), position);
}

void StickersManager::remove_sticker_from_set(const tl_object_ptr<td_api::InputFile> &sticker,
                                              Promise<Unit> &&promise) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Sticker, sticker, DialogId(), false, false);
  if (r_file_id.is_error()) {
    return promise.set_error(Status::Error(7, r_file_id.error().message()));  // TODO do not drop error code
  }

  auto file_id = r_file_id.move_as_ok();
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (!file_view.has_remote_location() || !file_view.remote_location().is_document() ||
      file_view.remote_location().is_web()) {
    return promise.set_error(Status::Error(7, "Wrong sticker file specified"));
  }

  td_->create_handler<DeleteStickerFromSetQuery>(std::move(promise))
      ->send(file_view.remote_location().as_input_document());
}

vector<FileId> StickersManager::get_attached_sticker_file_ids(const vector<int32> &int_file_ids) {
  vector<FileId> result;

  result.reserve(int_file_ids.size());
  for (auto int_file_id : int_file_ids) {
    FileId file_id(int_file_id, 0);
    if (get_sticker(file_id) == nullptr) {
      LOG(WARNING) << "Can't find sticker " << file_id;
      continue;
    }
    auto file_view = td_->file_manager_->get_file_view(file_id);
    CHECK(!file_view.empty());
    if (!file_view.has_remote_location()) {
      LOG(WARNING) << "Sticker " << file_id << " has no remote location";
      continue;
    }
    if (file_view.remote_location().is_web()) {
      LOG(WARNING) << "Sticker " << file_id << " is web";
      continue;
    }
    if (!file_view.remote_location().is_document()) {
      LOG(WARNING) << "Sticker " << file_id << " is encrypted";
      continue;
    }
    result.push_back(file_id);

    if (!td_->auth_manager_->is_bot()) {
      add_recent_sticker_by_id(true, file_id);
    }
  }

  return result;
}

int32 StickersManager::get_sticker_sets_hash(const vector<int64> &sticker_set_ids) const {
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

    uint64 pack_id = static_cast<uint64>(sticker_set_id);
    numbers.push_back(static_cast<uint32>(pack_id >> 32));
    numbers.push_back(static_cast<uint32>(pack_id & 0xFFFFFFFF));

    if (!sticker_set->is_viewed) {
      numbers.push_back(1);
    }
  }
  return get_vector_hash(numbers);
}

void StickersManager::send_update_installed_sticker_sets(bool from_database) {
  for (int is_masks = 0; is_masks < 2; is_masks++) {
    if (need_update_installed_sticker_sets_[is_masks]) {
      need_update_installed_sticker_sets_[is_masks] = false;
      if (are_installed_sticker_sets_loaded_[is_masks]) {
        installed_sticker_sets_hash_[is_masks] = get_sticker_sets_hash(installed_sticker_set_ids_[is_masks]);
        send_closure(G()->td(), &Td::send_update,
                     make_tl_object<td_api::updateInstalledStickerSets>(
                         is_masks != 0, vector<int64>(installed_sticker_set_ids_[is_masks])));

        if (G()->parameters().use_file_db && !from_database) {
          LOG(INFO) << "Save installed " << (is_masks ? "mask " : "") << "sticker sets to database";
          StickerSetListLogEvent log_event(installed_sticker_set_ids_[is_masks]);
          G()->td_db()->get_sqlite_pmc()->set(is_masks ? "sss1" : "sss0", log_event_store(log_event).as_slice().str(),
                                              Auto());
        }
      }
    }
  }
}

void StickersManager::send_update_featured_sticker_sets() {
  if (need_update_featured_sticker_sets_) {
    need_update_featured_sticker_sets_ = false;
    featured_sticker_sets_hash_ = get_featured_sticker_sets_hash();

    send_closure(
        G()->td(), &Td::send_update,
        make_tl_object<td_api::updateTrendingStickerSets>(get_sticker_sets_object(-1, featured_sticker_set_ids_, 5)));
  }
}

void StickersManager::reload_recent_stickers(bool is_attached, bool force) {
  auto &next_load_time = next_recent_stickers_load_time_[is_attached];
  if (!td_->auth_manager_->is_bot() && next_load_time >= 0 && (next_load_time < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload recent stickers";
    next_load_time = -1;
    td_->create_handler<GetRecentStickersQuery>()->send(is_attached, recent_stickers_hash_[is_attached]);
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
  if (value.empty()) {
    LOG(INFO) << "Recent " << (is_attached ? "attached " : "") << "stickers aren't found in database";
    reload_recent_stickers(is_attached, true);
    return;
  }

  LOG(INFO) << "Successfully loaded recent " << (is_attached ? "attached " : "") << "stickers list of size "
            << value.size() << " from database";

  StickerListLogEvent log_event;
  log_event_parse(log_event, value).ensure();

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

void StickersManager::on_get_recent_stickers(bool is_attached,
                                             tl_object_ptr<telegram_api::messages_RecentStickers> &&stickers_ptr) {
  CHECK(!td_->auth_manager_->is_bot());
  next_recent_stickers_load_time_[is_attached] = Time::now_cached() + Random::fast(30 * 60, 50 * 60);

  CHECK(stickers_ptr != nullptr);
  int32 constructor_id = stickers_ptr->get_id();
  if (constructor_id == telegram_api::messages_recentStickersNotModified::ID) {
    LOG(INFO) << (is_attached ? "Attached r" : "r") << "ecent stickers are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_recentStickers::ID);
  auto stickers = move_tl_object_as<telegram_api::messages_recentStickers>(stickers_ptr);

  vector<FileId> recent_sticker_ids;
  recent_sticker_ids.reserve(stickers->stickers_.size());
  for (auto &document_ptr : stickers->stickers_) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr), true).second;
    if (!sticker_id.is_valid()) {
      continue;
    }
    recent_sticker_ids.push_back(sticker_id);
  }

  on_load_recent_stickers_finished(is_attached, std::move(recent_sticker_ids));

  LOG_IF(ERROR, recent_stickers_hash_[is_attached] != stickers->hash_) << "Stickers hash mismatch";
}

void StickersManager::on_get_recent_stickers_failed(bool is_attached, Status error) {
  CHECK(error.is_error());
  next_recent_stickers_load_time_[is_attached] = Time::now_cached() + Random::fast(5, 10);
  auto promises = std::move(load_recent_stickers_queries_[is_attached]);
  load_recent_stickers_queries_[is_attached].clear();
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
    CHECK(file_view.remote_location().is_document());
    CHECK(!file_view.remote_location().is_web());
    auto id = static_cast<uint64>(file_view.remote_location().get_id());
    numbers.push_back(static_cast<uint32>(id >> 32));
    numbers.push_back(static_cast<uint32>(id & 0xFFFFFFFF));
  }
  return get_vector_hash(numbers);
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

  add_recent_sticker_inner(is_attached, r_file_id.ok(), std::move(promise));
}

void StickersManager::add_recent_sticker_inner(bool is_attached, FileId sticker_id, Promise<Unit> &&promise) {
  if (add_recent_sticker_impl(is_attached, sticker_id, promise)) {
    // TODO invokeAfter and log event
    auto file_view = td_->file_manager_->get_file_view(sticker_id);
    td_->create_handler<SaveRecentStickerQuery>(std::move(promise))
        ->send(is_attached, file_view.remote_location().as_input_document(), false);
  }
}

void StickersManager::add_recent_sticker_by_id(bool is_attached, FileId sticker_id) {
  // TODO log event
  Promise<Unit> promise;
  add_recent_sticker_impl(is_attached, sticker_id, promise);
}

bool StickersManager::add_recent_sticker_impl(bool is_attached, FileId sticker_id, Promise<Unit> &promise) {
  CHECK(!td_->auth_manager_->is_bot());

  if (!are_recent_stickers_loaded_[is_attached]) {
    load_recent_stickers(is_attached, PromiseCreator::lambda([is_attached, sticker_id,
                                                              promise = std::move(promise)](Result<> result) mutable {
                           if (result.is_ok()) {
                             send_closure(G()->stickers_manager(), &StickersManager::add_recent_sticker_inner,
                                          is_attached, sticker_id, std::move(promise));
                           } else {
                             promise.set_error(result.move_as_error());
                           }
                         }));
    return false;
  }

  vector<FileId> &sticker_ids = recent_sticker_ids_[is_attached];
  if (!sticker_ids.empty() && sticker_ids[0] == sticker_id) {
    if (sticker_ids[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
      sticker_ids[0] = sticker_id;
      save_recent_stickers_to_database(is_attached);
    }

    promise.set_value(Unit());
    return false;
  }

  auto sticker = get_sticker(sticker_id);
  if (sticker == nullptr) {
    promise.set_error(Status::Error(7, "Sticker not found"));
    return false;
  }
  if (sticker->set_id == 0) {
    promise.set_error(Status::Error(7, "Stickers without sticker set can't be added to recent"));
    return false;
  }

  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  if (!file_view.has_remote_location()) {
    promise.set_error(Status::Error(7, "Can save only sent stickers"));
    return false;
  }
  if (file_view.remote_location().is_web()) {
    promise.set_error(Status::Error(7, "Can't save web stickers"));
    return false;
  }
  if (!file_view.remote_location().is_document()) {
    promise.set_error(Status::Error(7, "Can't save encrypted stickers"));
    return false;
  }

  need_update_recent_stickers_[is_attached] = true;

  auto it = std::find(sticker_ids.begin(), sticker_ids.end(), sticker_id);
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
  return true;
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
  auto it = std::find(sticker_ids.begin(), sticker_ids.end(), file_id);
  if (it == sticker_ids.end()) {
    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(file_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }

  // TODO invokeAfter
  auto file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(file_view.has_remote_location());
  CHECK(file_view.remote_location().is_document());
  CHECK(!file_view.remote_location().is_web());
  td_->create_handler<SaveRecentStickerQuery>(std::move(promise))
      ->send(is_attached, file_view.remote_location().as_input_document(), true);

  sticker_ids.erase(it);

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

void StickersManager::send_update_recent_stickers(bool from_database) {
  for (int is_attached = 0; is_attached < 2; is_attached++) {
    if (need_update_recent_stickers_[is_attached]) {
      need_update_recent_stickers_[is_attached] = false;
      if (are_recent_stickers_loaded_[is_attached]) {
        recent_stickers_hash_[is_attached] = get_recent_stickers_hash(recent_sticker_ids_[is_attached]);
        vector<int32> stickers;
        stickers.reserve(recent_sticker_ids_[is_attached].size());
        for (auto sticker_id : recent_sticker_ids_[is_attached]) {
          stickers.push_back(sticker_id.get());
        }
        send_closure(G()->td(), &Td::send_update,
                     make_tl_object<td_api::updateRecentStickers>(is_attached != 0, std::move(stickers)));

        if (!from_database) {
          save_recent_stickers_to_database(is_attached != 0);
        }
      }
    }
  }
}

void StickersManager::save_recent_stickers_to_database(bool is_attached) {
  if (G()->parameters().use_file_db) {
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
  if (!td_->auth_manager_->is_bot() && next_favorite_stickers_load_time_ >= 0 &&
      (next_favorite_stickers_load_time_ < Time::now() || force)) {
    LOG_IF(INFO, force) << "Reload favorite stickers";
    next_favorite_stickers_load_time_ = -1;
    td_->create_handler<GetFavedStickersQuery>()->send(get_favorite_stickers_hash());
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
  if (value.empty()) {
    LOG(INFO) << "Favorite stickers aren't found in database";
    reload_favorite_stickers(true);
    return;
  }

  LOG(INFO) << "Successfully loaded favorite stickers list of size " << value.size() << " from database";

  StickerListLogEvent log_event;
  log_event_parse(log_event, value).ensure();

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
    tl_object_ptr<telegram_api::messages_FavedStickers> &&favorite_stickers_ptr) {
  CHECK(!td_->auth_manager_->is_bot());
  next_favorite_stickers_load_time_ = Time::now_cached() + Random::fast(30 * 60, 50 * 60);

  CHECK(favorite_stickers_ptr != nullptr);
  int32 constructor_id = favorite_stickers_ptr->get_id();
  if (constructor_id == telegram_api::messages_favedStickersNotModified::ID) {
    LOG(INFO) << "Favorite stickers are not modified";
    return;
  }
  CHECK(constructor_id == telegram_api::messages_favedStickers::ID);
  auto favorite_stickers = move_tl_object_as<telegram_api::messages_favedStickers>(favorite_stickers_ptr);

  // TODO use favorite_stickers->packs_

  vector<FileId> favorite_sticker_ids;
  favorite_sticker_ids.reserve(favorite_stickers->stickers_.size());
  for (auto &document_ptr : favorite_stickers->stickers_) {
    auto sticker_id = on_get_sticker_document(std::move(document_ptr), true).second;
    if (!sticker_id.is_valid()) {
      continue;
    }

    favorite_sticker_ids.push_back(sticker_id);
  }

  on_load_favorite_stickers_finished(std::move(favorite_sticker_ids));

  LOG_IF(ERROR, get_favorite_stickers_hash() != favorite_stickers->hash_) << "Favorite stickers hash mismatch";
}

void StickersManager::on_get_favorite_stickers_failed(Status error) {
  CHECK(error.is_error());
  next_favorite_stickers_load_time_ = Time::now_cached() + Random::fast(5, 10);
  auto promises = std::move(load_favorite_stickers_queries_);
  load_favorite_stickers_queries_.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

int32 StickersManager::get_favorite_stickers_hash() const {
  return get_recent_stickers_hash(favorite_sticker_ids_);
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

  add_favorite_sticker_inner(r_file_id.ok(), std::move(promise));
}

void StickersManager::add_favorite_sticker_inner(FileId sticker_id, Promise<Unit> &&promise) {
  if (add_favorite_sticker_impl(sticker_id, promise)) {
    // TODO invokeAfter and log event
    auto file_view = td_->file_manager_->get_file_view(sticker_id);
    td_->create_handler<FaveStickerQuery>(std::move(promise))
        ->send(file_view.remote_location().as_input_document(), false);
  }
}

void StickersManager::add_favorite_sticker_by_id(FileId sticker_id) {
  // TODO log event
  Promise<Unit> promise;
  add_favorite_sticker_impl(sticker_id, promise);
}

bool StickersManager::add_favorite_sticker_impl(FileId sticker_id, Promise<Unit> &promise) {
  CHECK(!td_->auth_manager_->is_bot());

  if (!are_favorite_stickers_loaded_) {
    load_favorite_stickers(PromiseCreator::lambda([sticker_id, promise = std::move(promise)](Result<> result) mutable {
      if (result.is_ok()) {
        send_closure(G()->stickers_manager(), &StickersManager::add_favorite_sticker_inner, sticker_id,
                     std::move(promise));
      } else {
        promise.set_error(result.move_as_error());
      }
    }));
    return false;
  }

  if (!favorite_sticker_ids_.empty() && favorite_sticker_ids_[0] == sticker_id) {
    if (favorite_sticker_ids_[0].get_remote() == 0 && sticker_id.get_remote() != 0) {
      favorite_sticker_ids_[0] = sticker_id;
      save_favorite_stickers_to_database();
    }

    promise.set_value(Unit());
    return false;
  }

  auto sticker = get_sticker(sticker_id);
  if (sticker == nullptr) {
    promise.set_error(Status::Error(7, "Sticker not found"));
    return false;
  }
  if (sticker->set_id == 0) {
    promise.set_error(Status::Error(7, "Stickers without sticker set can't be favorite"));
    return false;
  }

  auto file_view = td_->file_manager_->get_file_view(sticker_id);
  if (!file_view.has_remote_location()) {
    promise.set_error(Status::Error(7, "Can add to favorites only sent stickers"));
    return false;
  }
  if (file_view.remote_location().is_web()) {
    promise.set_error(Status::Error(7, "Can't add to favorites web stickers"));
    return false;
  }
  if (!file_view.remote_location().is_document()) {
    promise.set_error(Status::Error(7, "Can't add to favorites encrypted stickers"));
    return false;
  }

  auto it = std::find(favorite_sticker_ids_.begin(), favorite_sticker_ids_.end(), sticker_id);
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
  return true;
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
  auto it = std::find(favorite_sticker_ids_.begin(), favorite_sticker_ids_.end(), file_id);
  if (it == favorite_sticker_ids_.end()) {
    return promise.set_value(Unit());
  }

  auto sticker = get_sticker(file_id);
  if (sticker == nullptr) {
    return promise.set_error(Status::Error(7, "Sticker not found"));
  }

  // TODO invokeAfter
  auto file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(file_view.has_remote_location());
  CHECK(file_view.remote_location().is_document());
  CHECK(!file_view.remote_location().is_web());
  td_->create_handler<FaveStickerQuery>(std::move(promise))
      ->send(file_view.remote_location().as_input_document(), true);

  favorite_sticker_ids_.erase(it);

  send_update_favorite_stickers();
}

void StickersManager::send_update_favorite_stickers(bool from_database) {
  if (are_favorite_stickers_loaded_) {
    vector<int32> stickers;
    stickers.reserve(favorite_sticker_ids_.size());
    for (auto sticker_id : favorite_sticker_ids_) {
      stickers.push_back(sticker_id.get());
    }
    send_closure(G()->td(), &Td::send_update, make_tl_object<td_api::updateFavoriteStickers>(std::move(stickers)));

    if (!from_database) {
      save_favorite_stickers_to_database();
    }
  }
}

void StickersManager::save_favorite_stickers_to_database() {
  if (G()->parameters().use_file_db) {
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
  if (sticker->set_id == 0) {
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

string StickersManager::remove_emoji_modifiers(string emoji) {
  static const Slice modifiers[] = {u8"\uFE0E" /* variation selector-15 */,
                                    u8"\uFE0F" /* variation selector-16 */,
                                    u8"\u200D\u2640" /* zero width joiner + female sign */,
                                    u8"\u200D\u2642" /* zero width joiner + male sign */,
                                    u8"\U0001F3FB" /* emoji modifier fitzpatrick type-1-2 */,
                                    u8"\U0001F3FC" /* emoji modifier fitzpatrick type-3 */,
                                    u8"\U0001F3FD" /* emoji modifier fitzpatrick type-4 */,
                                    u8"\U0001F3FE" /* emoji modifier fitzpatrick type-5 */,
                                    u8"\U0001F3FF" /* emoji modifier fitzpatrick type-6 */};
  bool found = true;
  while (found) {
    found = false;
    for (auto &modifier : modifiers) {
      if (ends_with(emoji, modifier) && emoji.size() > modifier.size()) {
        emoji.resize(emoji.size() - modifier.size());
        found = true;
      }
    }
  }
  return emoji;
}

}  // namespace td
