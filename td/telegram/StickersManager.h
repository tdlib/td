//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/SpecialStickerSetType.h"
#include "td/telegram/StickerSetId.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Hints.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

class Td;

class StickersManager : public Actor {
 public:
  static constexpr int64 GREAT_MINDS_SET_ID = 1842540969984001;

  static vector<StickerSetId> convert_sticker_set_ids(const vector<int64> &sticker_set_ids);
  static vector<int64> convert_sticker_set_ids(const vector<StickerSetId> &sticker_set_ids);

  StickersManager(Td *td, ActorShared<> parent);

  void init();

  tl_object_ptr<td_api::sticker> get_sticker_object(FileId file_id) const;

  tl_object_ptr<td_api::stickers> get_stickers_object(const vector<FileId> &sticker_ids) const;

  tl_object_ptr<td_api::DiceStickers> get_dice_stickers_object(const string &emoji, int32 value) const;

  int32 get_dice_success_animation_frame_number(const string &emoji, int32 value) const;

  tl_object_ptr<td_api::stickerSet> get_sticker_set_object(StickerSetId sticker_set_id) const;

  tl_object_ptr<td_api::stickerSets> get_sticker_sets_object(int32 total_count,
                                                             const vector<StickerSetId> &sticker_set_ids,
                                                             size_t covers_limit) const;

  tl_object_ptr<telegram_api::InputStickerSet> get_input_sticker_set(StickerSetId sticker_set_id) const;

  void register_dice(const string &emoji, int32 value, FullMessageId full_message_id, const char *source);

  void unregister_dice(const string &emoji, int32 value, FullMessageId full_message_id, const char *source);

  void create_sticker(FileId file_id, string minithumbnail, PhotoSize thumbnail, Dimensions dimensions,
                      tl_object_ptr<telegram_api::documentAttributeSticker> sticker, bool is_animated,
                      MultiPromiseActor *load_data_multipromise_ptr);

  bool has_input_media(FileId sticker_file_id, bool is_secret) const;

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file,
                                                          tl_object_ptr<telegram_api::InputFile> input_thumbnail,
                                                          const string &emoji) const;

  SecretInputMedia get_secret_input_media(FileId sticker_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          BufferSlice thumbnail) const;

  vector<FileId> get_stickers(string emoji, int32 limit, bool force, Promise<Unit> &&promise);

  vector<FileId> search_stickers(string emoji, int32 limit, Promise<Unit> &&promise);

  vector<StickerSetId> get_installed_sticker_sets(bool is_masks, Promise<Unit> &&promise);

  static bool has_webp_thumbnail(const vector<tl_object_ptr<telegram_api::PhotoSize>> &thumbnails);

  StickerSetId get_sticker_set_id(const tl_object_ptr<telegram_api::InputStickerSet> &set_ptr);

  StickerSetId add_sticker_set(tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr);

  StickerSetId get_sticker_set(StickerSetId set_id, Promise<Unit> &&promise);

  StickerSetId search_sticker_set(const string &short_name_to_search, Promise<Unit> &&promise);

  std::pair<int32, vector<StickerSetId>> search_installed_sticker_sets(bool is_masks, const string &query, int32 limit,
                                                                       Promise<Unit> &&promise);

  vector<StickerSetId> search_sticker_sets(const string &query, Promise<Unit> &&promise);

  void change_sticker_set(StickerSetId set_id, bool is_installed, bool is_archived, Promise<Unit> &&promise);

  void view_featured_sticker_sets(const vector<StickerSetId> &sticker_set_ids);

  void on_get_installed_sticker_sets(bool is_masks, tl_object_ptr<telegram_api::messages_AllStickers> &&stickers_ptr);

  void on_get_installed_sticker_sets_failed(bool is_masks, Status error);

  StickerSetId on_get_messages_sticker_set(StickerSetId sticker_set_id,
                                           tl_object_ptr<telegram_api::messages_stickerSet> &&set, bool is_changed,
                                           const char *source);

  StickerSetId on_get_sticker_set(tl_object_ptr<telegram_api::stickerSet> &&set, bool is_changed, const char *source);

  StickerSetId on_get_sticker_set_covered(tl_object_ptr<telegram_api::StickerSetCovered> &&set_ptr, bool is_changed,
                                          const char *source);

  void on_get_special_sticker_set(const SpecialStickerSetType &type, StickerSetId sticker_set_id);

  void on_load_special_sticker_set(const SpecialStickerSetType &type, Status result);

  void on_load_sticker_set_fail(StickerSetId sticker_set_id, const Status &error);

  void on_install_sticker_set(StickerSetId set_id, bool is_archived,
                              tl_object_ptr<telegram_api::messages_StickerSetInstallResult> &&result);

  void on_uninstall_sticker_set(StickerSetId set_id);

  void on_update_dice_emojis();

  void on_update_dice_success_values();

  void on_update_sticker_sets();

  void on_update_sticker_sets_order(bool is_masks, const vector<StickerSetId> &sticker_set_ids);

  std::pair<int32, vector<StickerSetId>> get_archived_sticker_sets(bool is_masks, StickerSetId offset_sticker_set_id,
                                                                   int32 limit, bool force, Promise<Unit> &&promise);

  void on_get_archived_sticker_sets(bool is_masks, StickerSetId offset_sticker_set_id,
                                    vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets,
                                    int32 total_count);

  std::pair<int32, vector<StickerSetId>> get_featured_sticker_sets(int32 offset, int32 limit, Promise<Unit> &&promise);

  void on_get_featured_sticker_sets(int32 offset, int32 limit, uint32 generation,
                                    tl_object_ptr<telegram_api::messages_FeaturedStickers> &&sticker_sets_ptr);

  void on_get_featured_sticker_sets_failed(int32 offset, int32 limit, uint32 generation, Status error);

  vector<StickerSetId> get_attached_sticker_sets(FileId file_id, Promise<Unit> &&promise);

  void on_get_attached_sticker_sets(FileId file_id,
                                    vector<tl_object_ptr<telegram_api::StickerSetCovered>> &&sticker_sets);

  void reorder_installed_sticker_sets(bool is_masks, const vector<StickerSetId> &sticker_set_ids,
                                      Promise<Unit> &&promise);

  FileId upload_sticker_file(UserId user_id, const tl_object_ptr<td_api::InputFile> &sticker, Promise<Unit> &&promise);

  void create_new_sticker_set(UserId user_id, string &title, string &short_name, bool is_masks,
                              vector<tl_object_ptr<td_api::InputSticker>> &&stickers, Promise<Unit> &&promise);

  void add_sticker_to_set(UserId user_id, string &short_name, tl_object_ptr<td_api::InputSticker> &&sticker,
                          Promise<Unit> &&promise);

  void set_sticker_set_thumbnail(UserId user_id, string &short_name, tl_object_ptr<td_api::InputFile> &&thumbnail,
                                 Promise<Unit> &&promise);

  void set_sticker_position_in_set(const tl_object_ptr<td_api::InputFile> &sticker, int32 position,
                                   Promise<Unit> &&promise);

  void remove_sticker_from_set(const tl_object_ptr<td_api::InputFile> &sticker, Promise<Unit> &&promise);

  vector<FileId> get_recent_stickers(bool is_attached, Promise<Unit> &&promise);

  void on_get_recent_stickers(bool is_repair, bool is_attached,
                              tl_object_ptr<telegram_api::messages_RecentStickers> &&stickers_ptr);

  void on_get_recent_stickers_failed(bool is_repair, bool is_attached, Status error);

  FileSourceId get_recent_stickers_file_source_id(int is_attached);

  void add_recent_sticker(bool is_attached, const tl_object_ptr<td_api::InputFile> &input_file,
                          Promise<Unit> &&promise);

  void add_recent_sticker_by_id(bool is_attached, FileId sticker_id);

  void remove_recent_sticker(bool is_attached, const tl_object_ptr<td_api::InputFile> &input_file,
                             Promise<Unit> &&promise);

  void send_save_recent_sticker_query(bool is_attached, FileId sticker_id, bool unsave, Promise<Unit> &&promise);

  void clear_recent_stickers(bool is_attached, Promise<Unit> &&promise);

  void on_update_recent_stickers_limit(int32 recent_stickers_limit);

  void on_update_favorite_stickers_limit(int32 favorite_stickers_limit);

  void reload_favorite_stickers(bool force);

  void repair_favorite_stickers(Promise<Unit> &&promise);

  void on_get_favorite_stickers(bool is_repair,
                                tl_object_ptr<telegram_api::messages_FavedStickers> &&favorite_stickers_ptr);

  void on_get_favorite_stickers_failed(bool is_repair, Status error);

  FileSourceId get_favorite_stickers_file_source_id();

  vector<FileId> get_favorite_stickers(Promise<Unit> &&promise);

  void add_favorite_sticker(const tl_object_ptr<td_api::InputFile> &input_file, Promise<Unit> &&promise);

  void add_favorite_sticker_by_id(FileId sticker_id);

  void remove_favorite_sticker(const tl_object_ptr<td_api::InputFile> &input_file, Promise<Unit> &&promise);

  void send_fave_sticker_query(FileId sticker_id, bool unsave, Promise<Unit> &&promise);

  vector<FileId> get_attached_sticker_file_ids(const vector<int32> &int_file_ids);

  vector<string> get_sticker_emojis(const tl_object_ptr<td_api::InputFile> &input_file, Promise<Unit> &&promise);

  vector<string> search_emojis(const string &text, bool exact_match, const vector<string> &input_language_codes,
                               bool force, Promise<Unit> &&promise);

  int64 get_emoji_suggestions_url(const string &language_code, Promise<Unit> &&promise);

  td_api::object_ptr<td_api::httpUrl> get_emoji_suggestions_url_result(int64 random_id);

  void reload_sticker_set(StickerSetId sticker_set_id, int64 access_hash, Promise<Unit> &&promise);

  void reload_installed_sticker_sets(bool is_masks, bool force);

  void reload_featured_sticker_sets(bool force);

  void reload_recent_stickers(bool is_attached, bool force);

  void repair_recent_stickers(bool is_attached, Promise<Unit> &&promise);

  FileId get_sticker_thumbnail_file_id(FileId file_id) const;

  vector<FileId> get_sticker_file_ids(FileId file_id) const;

  void delete_sticker_thumbnail(FileId file_id);

  FileId dup_sticker(FileId new_id, FileId old_id);

  bool merge_stickers(FileId new_id, FileId old_id, bool can_delete_old);

  template <class StorerT>
  void store_sticker(FileId file_id, bool in_sticker_set, StorerT &storer) const;

  template <class ParserT>
  FileId parse_sticker(bool in_sticker_set, ParserT &parser);

  void on_uploaded_sticker_file(FileId file_id, tl_object_ptr<telegram_api::MessageMedia> media,
                                Promise<Unit> &&promise);

  void on_find_stickers_success(const string &emoji, tl_object_ptr<telegram_api::messages_Stickers> &&sticker_sets);

  void on_find_stickers_fail(const string &emoji, Status &&error);

  void on_find_sticker_sets_success(const string &query,
                                    tl_object_ptr<telegram_api::messages_FoundStickerSets> &&sticker_sets);

  void on_find_sticker_sets_fail(const string &query, Status &&error);

  void send_get_attached_stickers_query(FileId file_id, Promise<Unit> &&promise);

  void after_get_difference();

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

  template <class StorerT>
  void store_sticker_set_id(StickerSetId sticker_set_id, StorerT &storer) const;

  template <class ParserT>
  void parse_sticker_set_id(StickerSetId &sticker_set_id, ParserT &parser);

 private:
  static constexpr int32 MAX_FEATURED_STICKER_SET_VIEW_DELAY = 5;
  static constexpr int32 OLD_FEATURED_STICKER_SET_SLICE_SIZE = 20;

  static constexpr int32 MAX_FOUND_STICKERS = 100;                    // server side limit
  static constexpr int64 MAX_STICKER_FILE_SIZE = 1 << 19;             // server side limit
  static constexpr int64 MAX_THUMBNAIL_FILE_SIZE = 1 << 17;           // server side limit
  static constexpr int64 MAX_ANIMATED_STICKER_FILE_SIZE = 1 << 16;    // server side limit
  static constexpr int64 MAX_ANIMATED_THUMBNAIL_FILE_SIZE = 1 << 15;  // server side limit
  static constexpr size_t MAX_STICKER_SET_TITLE_LENGTH = 64;          // server side limit
  static constexpr size_t MAX_STICKER_SET_SHORT_NAME_LENGTH = 64;     // server side limit

  static constexpr int32 EMOJI_KEYWORDS_UPDATE_DELAY = 3600;

  class Sticker {
   public:
    StickerSetId set_id;
    string alt;
    Dimensions dimensions;
    string minithumbnail;
    PhotoSize s_thumbnail;
    PhotoSize m_thumbnail;
    FileId file_id;
    bool is_animated = false;
    bool is_mask = false;
    int32 point = -1;
    double x_shift = 0;
    double y_shift = 0;
    double scale = 0;

    mutable bool is_changed = true;
  };

  class StickerSet {
   public:
    bool is_inited = false;  // basic information about the set
    bool was_loaded = false;
    bool is_loaded = false;

    StickerSetId id;
    int64 access_hash = 0;
    string title;
    string short_name;
    int32 sticker_count = 0;
    int32 hash = 0;
    int32 expires_at = 0;

    string minithumbnail;
    PhotoSize thumbnail;

    vector<FileId> sticker_ids;
    std::unordered_map<string, vector<FileId>> emoji_stickers_map_;              // emoji -> stickers
    std::unordered_map<FileId, vector<string>, FileIdHash> sticker_emojis_map_;  // sticker -> emojis

    bool is_installed = false;
    bool is_archived = false;
    bool is_official = false;
    bool is_animated = false;
    bool is_masks = false;
    bool is_viewed = true;
    bool is_thumbnail_reloaded = false;
    bool are_legacy_sticker_thumbnails_reloaded = false;
    mutable bool was_update_sent = false;  // does the sticker set is known to the client
    bool is_changed = true;                // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;     // have new changes that need only to be saved to the database

    vector<uint32> load_requests;
    vector<uint32> load_without_stickers_requests;
  };

  struct PendingNewStickerSet {
    MultiPromiseActor upload_files_multipromise{"UploadNewStickerSetFilesMultiPromiseActor"};
    UserId user_id;
    string title;
    string short_name;
    bool is_masks;
    bool is_animated;
    vector<FileId> file_ids;
    vector<tl_object_ptr<td_api::InputSticker>> stickers;
    Promise<> promise;
  };

  struct PendingAddStickerToSet {
    string short_name;
    FileId file_id;
    tl_object_ptr<td_api::InputSticker> sticker;
    Promise<> promise;
  };

  struct PendingSetStickerSetThumbnail {
    string short_name;
    FileId file_id;
    Promise<> promise;
  };

  struct SpecialStickerSet {
    StickerSetId id_;
    int64 access_hash_ = 0;
    string short_name_;
    SpecialStickerSetType type_;
    bool is_being_loaded_ = false;
  };

  class StickerListLogEvent;
  class StickerSetListLogEvent;

  class UploadStickerFileCallback;

  static vector<td_api::object_ptr<td_api::closedVectorPath>> get_sticker_minithumbnail(CSlice path,
                                                                                        StickerSetId sticker_set_id,
                                                                                        int64 document_id);

  static tl_object_ptr<td_api::MaskPoint> get_mask_point_object(int32 point);

  tl_object_ptr<td_api::stickerSetInfo> get_sticker_set_info_object(StickerSetId sticker_set_id,
                                                                    size_t covers_limit) const;

  Sticker *get_sticker(FileId file_id);
  const Sticker *get_sticker(FileId file_id) const;

  FileId on_get_sticker(unique_ptr<Sticker> new_sticker, bool replace);

  StickerSet *get_sticker_set(StickerSetId sticker_set_id);
  const StickerSet *get_sticker_set(StickerSetId sticker_set_id) const;

  StickerSet *add_sticker_set(StickerSetId sticker_set_id, int64 access_hash);

  std::pair<int64, FileId> on_get_sticker_document(tl_object_ptr<telegram_api::Document> &&document_ptr);

  static tl_object_ptr<telegram_api::InputStickerSet> get_input_sticker_set(const StickerSet *set);

  StickerSetId on_get_input_sticker_set(FileId sticker_file_id, tl_object_ptr<telegram_api::InputStickerSet> &&set_ptr,
                                        MultiPromiseActor *load_data_multipromise_ptr = nullptr);

  void on_resolve_sticker_set_short_name(FileId sticker_file_id, const string &short_name);

  int apply_installed_sticker_sets_order(bool is_masks, const vector<StickerSetId> &sticker_set_ids);

  void on_update_sticker_set(StickerSet *sticker_set, bool is_installed, bool is_archived, bool is_changed,
                             bool from_database = false);

  static string get_sticker_set_database_key(StickerSetId set_id);

  static string get_full_sticker_set_database_key(StickerSetId set_id);

  string get_sticker_set_database_value(const StickerSet *s, bool with_stickers);

  void update_sticker_set(StickerSet *sticker_set);

  void load_sticker_sets(vector<StickerSetId> &&sticker_set_ids, Promise<Unit> &&promise);

  void load_sticker_sets_without_stickers(vector<StickerSetId> &&sticker_set_ids, Promise<Unit> &&promise);

  void on_load_sticker_set_from_database(StickerSetId sticker_set_id, bool with_stickers, string value);

  void update_load_requests(StickerSet *sticker_set, bool with_stickers, const Status &status);

  void update_load_request(uint32 load_request_id, const Status &status);

  void do_reload_sticker_set(StickerSetId sticker_set_id,
                             tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set,
                             Promise<Unit> &&promise) const;

  static void read_featured_sticker_sets(void *td_void);

  int32 get_sticker_sets_hash(const vector<StickerSetId> &sticker_set_ids) const;

  int32 get_featured_sticker_sets_hash() const;

  int32 get_recent_stickers_hash(const vector<FileId> &sticker_ids) const;

  void load_installed_sticker_sets(bool is_masks, Promise<Unit> &&promise);

  void load_featured_sticker_sets(Promise<Unit> &&promise);

  void load_old_featured_sticker_sets(Promise<Unit> &&promise);

  void load_recent_stickers(bool is_attached, Promise<Unit> &&promise);

  void on_load_installed_sticker_sets_from_database(bool is_masks, string value);

  void on_load_installed_sticker_sets_finished(bool is_masks, vector<StickerSetId> &&installed_sticker_set_ids,
                                               bool from_database = false);

  void on_load_featured_sticker_sets_from_database(string value);

  void on_load_featured_sticker_sets_finished(vector<StickerSetId> &&featured_sticker_set_ids);

  void on_load_old_featured_sticker_sets_from_database(uint32 generation, string value);

  void on_load_old_featured_sticker_sets_finished(uint32 generation,
                                                  vector<StickerSetId> &&old_featured_sticker_set_ids);

  void on_load_recent_stickers_from_database(bool is_attached, string value);

  void on_load_recent_stickers_finished(bool is_attached, vector<FileId> &&recent_sticker_ids,
                                        bool from_database = false);

  td_api::object_ptr<td_api::updateInstalledStickerSets> get_update_installed_sticker_sets_object(int is_mask) const;

  void send_update_installed_sticker_sets(bool from_database = false);

  void reload_old_featured_sticker_sets(uint32 generation = 0);

  void on_old_featured_sticker_sets_invalidated();

  void invalidate_old_featured_sticker_sets();

  void set_old_featured_sticker_set_count(int32 count);

  // must be called after every call to set_old_featured_sticker_set_count or
  // any change of old_featured_sticker_set_ids_ size
  void fix_old_featured_sticker_set_count();

  td_api::object_ptr<td_api::updateTrendingStickerSets> get_update_trending_sticker_sets_object() const;

  void send_update_featured_sticker_sets();

  td_api::object_ptr<td_api::updateRecentStickers> get_update_recent_stickers_object(int is_attached) const;

  void send_update_recent_stickers(bool from_database = false);

  void save_recent_stickers_to_database(bool is_attached);

  void add_recent_sticker_impl(bool is_attached, FileId sticker_id, bool add_on_server, Promise<Unit> &&promise);

  int32 get_favorite_stickers_hash() const;

  void add_favorite_sticker_impl(FileId sticker_id, bool add_on_server, Promise<Unit> &&promise);

  void load_favorite_stickers(Promise<Unit> &&promise);

  void on_load_favorite_stickers_from_database(const string &value);

  void on_load_favorite_stickers_finished(vector<FileId> &&favorite_sticker_ids, bool from_database = false);

  td_api::object_ptr<td_api::updateFavoriteStickers> get_update_favorite_stickers_object() const;

  void send_update_favorite_stickers(bool from_database = false);

  void save_favorite_stickers_to_database();

  template <class StorerT>
  void store_sticker_set(const StickerSet *sticker_set, bool with_stickers, StorerT &storer) const;

  template <class ParserT>
  void parse_sticker_set(StickerSet *sticker_set, ParserT &parser);

  static string &get_input_sticker_emojis(td_api::InputSticker *sticker);

  Result<std::tuple<FileId, bool, bool, bool>> prepare_input_file(const tl_object_ptr<td_api::InputFile> &input_file,
                                                                  bool is_animated, bool for_thumbnail);

  Result<std::tuple<FileId, bool, bool, bool>> prepare_input_sticker(td_api::InputSticker *sticker);

  tl_object_ptr<telegram_api::inputStickerSetItem> get_input_sticker(td_api::InputSticker *sticker,
                                                                     FileId file_id) const;

  void upload_sticker_file(UserId user_id, FileId file_id, Promise<Unit> &&promise);

  void on_upload_sticker_file(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);

  void on_upload_sticker_file_error(FileId file_id, Status status);

  void do_upload_sticker_file(UserId user_id, FileId file_id, tl_object_ptr<telegram_api::InputFile> &&input_file,
                              Promise<Unit> &&promise);

  void on_new_stickers_uploaded(int64 random_id, Result<Unit> result);

  void on_added_sticker_uploaded(int64 random_id, Result<Unit> result);

  void on_sticker_set_thumbnail_uploaded(int64 random_id, Result<Unit> result);

  void do_set_sticker_set_thumbnail(UserId user_id, string short_name, tl_object_ptr<td_api::InputFile> &&thumbnail,
                                    Promise<Unit> &&promise);

  bool update_sticker_set_cache(const StickerSet *sticker_set, Promise<Unit> &promise);

  td_api::object_ptr<td_api::updateDiceEmojis> get_update_dice_emojis_object() const;

  void start_up() override;

  void tear_down() override;

  SpecialStickerSet &add_special_sticker_set(const string &type);

  static void init_special_sticker_set(SpecialStickerSet &sticker_set, int64 sticker_set_id, int64 access_hash,
                                       string name);

  void load_special_sticker_set_info_from_binlog(SpecialStickerSet &sticker_set);

  void load_special_sticker_set_by_type(const SpecialStickerSetType &type);

  void load_special_sticker_set(SpecialStickerSet &sticker_set);

  void reload_special_sticker_set(SpecialStickerSet &sticker_set);

  static void add_sticker_thumbnail(Sticker *s, PhotoSize thumbnail);

  static string get_sticker_mime_type(const Sticker *s);

  static string get_emoji_language_code_version_database_key(const string &language_code);

  static string get_emoji_language_code_last_difference_time_database_key(const string &language_code);

  static string get_language_emojis_database_key(const string &language_code, const string &text);

  static string get_emoji_language_codes_database_key(const vector<string> &language_codes);

  int32 get_emoji_language_code_version(const string &language_code);

  double get_emoji_language_code_last_difference_time(const string &language_code);

  vector<string> get_emoji_language_codes(const vector<string> &input_language_codes, Slice text,
                                          Promise<Unit> &promise);

  void load_language_codes(vector<string> language_codes, string key, Promise<Unit> &&promise);

  void on_get_language_codes(const string &key, Result<vector<string>> &&result);

  vector<string> search_language_emojis(const string &language_code, const string &text, bool exact_match) const;

  void load_emoji_keywords(const string &language_code, Promise<Unit> &&promise);

  void on_get_emoji_keywords(const string &language_code,
                             Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result);

  void load_emoji_keywords_difference(const string &language_code);

  void on_get_emoji_keywords_difference(
      const string &language_code, int32 from_version,
      Result<telegram_api::object_ptr<telegram_api::emojiKeywordsDifference>> &&result);

  void on_get_emoji_suggestions_url(int64 random_id, Promise<Unit> &&promise,
                                    Result<telegram_api::object_ptr<telegram_api::emojiURL>> &&r_emoji_url);

  Td *td_;
  ActorShared<> parent_;

  bool is_inited_ = false;

  std::unordered_map<FileId, unique_ptr<Sticker>, FileIdHash> stickers_;                     // file_id -> Sticker
  std::unordered_map<StickerSetId, unique_ptr<StickerSet>, StickerSetIdHash> sticker_sets_;  // id -> StickerSet
  std::unordered_map<string, StickerSetId> short_name_to_sticker_set_id_;

  vector<StickerSetId> installed_sticker_set_ids_[2];
  vector<StickerSetId> featured_sticker_set_ids_;
  vector<StickerSetId> old_featured_sticker_set_ids_;
  vector<FileId> recent_sticker_ids_[2];
  vector<FileId> favorite_sticker_ids_;

  double next_installed_sticker_sets_load_time_[2] = {0, 0};
  double next_featured_sticker_sets_load_time_ = 0;
  double next_recent_stickers_load_time_[2] = {0, 0};
  double next_favorite_stickers_load_time_ = 0;

  int32 installed_sticker_sets_hash_[2] = {0, 0};
  int32 featured_sticker_sets_hash_ = 0;
  int32 recent_stickers_hash_[2] = {0, 0};

  int32 old_featured_sticker_set_count_ = -1;
  uint32 old_featured_sticker_set_generation_ = 1;

  bool need_update_installed_sticker_sets_[2] = {false, false};
  bool need_update_featured_sticker_sets_ = false;
  bool need_update_recent_stickers_[2] = {false, false};

  bool are_installed_sticker_sets_loaded_[2] = {false, false};
  bool are_featured_sticker_sets_loaded_ = false;
  bool are_recent_stickers_loaded_[2] = {false, false};
  bool are_favorite_stickers_loaded_ = false;

  bool are_old_featured_sticker_sets_invalidated_ = false;

  vector<Promise<Unit>> load_installed_sticker_sets_queries_[2];
  vector<Promise<Unit>> load_featured_sticker_sets_queries_;
  vector<Promise<Unit>> load_old_featured_sticker_sets_queries_;
  vector<Promise<Unit>> load_recent_stickers_queries_[2];
  vector<Promise<Unit>> repair_recent_stickers_queries_[2];
  vector<Promise<Unit>> load_favorite_stickers_queries_;
  vector<Promise<Unit>> repair_favorite_stickers_queries_;

  vector<FileId> recent_sticker_file_ids_[2];
  FileSourceId recent_stickers_file_source_id_[2];
  vector<FileId> favorite_sticker_file_ids_;
  FileSourceId favorite_stickers_file_source_id_;

  vector<StickerSetId> archived_sticker_set_ids_[2];
  int32 total_archived_sticker_set_count_[2] = {-1, -1};

  std::unordered_map<FileId, vector<StickerSetId>, FileIdHash> attached_sticker_sets_;

  Hints installed_sticker_sets_hints_[2];  // search installed sticker sets by their title and name

  struct FoundStickers {
    vector<FileId> sticker_ids_;
    int32 cache_time_ = 300;
    double next_reload_time_ = 0;
  };
  std::unordered_map<string, FoundStickers> found_stickers_;
  std::unordered_map<string, vector<Promise<Unit>>> search_stickers_queries_;

  std::unordered_map<string, vector<StickerSetId>> found_sticker_sets_;
  std::unordered_map<string, vector<Promise<Unit>>> search_sticker_sets_queries_;

  std::unordered_set<StickerSetId, StickerSetIdHash> pending_viewed_featured_sticker_set_ids_;
  Timeout pending_featured_sticker_set_views_timeout_;

  int32 recent_stickers_limit_ = 200;
  int32 favorite_stickers_limit_ = 5;

  std::unordered_map<string, SpecialStickerSet> special_sticker_sets_;

  struct StickerSetLoadRequest {
    Promise<Unit> promise;
    Status error;
    size_t left_queries;
  };

  std::unordered_map<uint32, StickerSetLoadRequest> sticker_set_load_requests_;
  uint32 current_sticker_set_load_request_ = 0;

  std::unordered_map<int64, unique_ptr<PendingNewStickerSet>> pending_new_sticker_sets_;

  std::unordered_map<int64, unique_ptr<PendingAddStickerToSet>> pending_add_sticker_to_sets_;

  std::unordered_map<int64, unique_ptr<PendingSetStickerSetThumbnail>> pending_set_sticker_set_thumbnails_;

  std::shared_ptr<UploadStickerFileCallback> upload_sticker_file_callback_;

  std::unordered_map<FileId, std::pair<UserId, Promise<Unit>>, FileIdHash> being_uploaded_files_;

  std::unordered_map<string, vector<string>> emoji_language_codes_;
  std::unordered_map<string, int32> emoji_language_code_versions_;
  std::unordered_map<string, double> emoji_language_code_last_difference_times_;
  std::unordered_set<string> reloaded_emoji_keywords_;
  std::unordered_map<string, vector<Promise<Unit>>> load_emoji_keywords_queries_;
  std::unordered_map<string, vector<Promise<Unit>>> load_language_codes_queries_;
  std::unordered_map<int64, string> emoji_suggestions_urls_;

  std::unordered_map<string, std::unordered_set<FullMessageId, FullMessageIdHash>> dice_messages_;

  string dice_emojis_str_;
  vector<string> dice_emojis_;

  string dice_success_values_str_;
  vector<std::pair<int32, int32>> dice_success_values_;
};

}  // namespace td
