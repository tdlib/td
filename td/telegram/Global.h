//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DhConfig.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryCreator.h"

#include "td/net/NetStats.h"

#include "td/actor/actor.h"
#include "td/actor/SchedulerLocalStorage.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace td {

class AccountManager;
class AnimationsManager;
class AttachMenuManager;
class AuthManager;
class AutosaveManager;
class BackgroundManager;
class BoostManager;
class CallManager;
class ConfigManager;
class ConnectionCreator;
class ContactsManager;
class DialogFilterManager;
class DownloadManager;
class FileManager;
class FileReferenceManager;
class ForumTopicManager;
class GameManager;
class GroupCallManager;
class LanguagePackManager;
class LinkManager;
class MessagesManager;
class NetQueryDispatcher;
class NotificationManager;
class NotificationSettingsManager;
class OptionManager;
class PasswordManager;
class ReactionManager;
class SecretChatsManager;
class SponsoredMessageManager;
class StateManager;
class StickersManager;
class StorageManager;
class StoryManager;
class Td;
class TdDb;
class TempAuthKeyWatchdog;
class ThemeManager;
class TopDialogManager;
class UpdatesManager;
class WebPagesManager;

class Global final : public ActorContext {
 public:
  Global();
  ~Global() final;
  Global(const Global &) = delete;
  Global &operator=(const Global &) = delete;
  Global(Global &&) = delete;
  Global &operator=(Global &&) = delete;

  static constexpr int32 ID = -572104940;
  int32 get_id() const final {
    return ID;
  }

#define td_db() get_td_db_impl(__FILE__, __LINE__)
  TdDb *get_td_db_impl(const char *file, int line) {
    LOG_CHECK(td_db_) << close_flag() << " " << file << " " << line;
    return td_db_.get();
  }

  void log_out(Slice reason);

  void close_all(bool destroy_flag, Promise<> on_finished);

  Status init(ActorId<Td> td, unique_ptr<TdDb> td_db_ptr) TD_WARN_UNUSED_RESULT;

  Slice get_dir() const;

  Slice get_secure_files_dir() const {
    if (store_all_files_in_files_directory_) {
      return get_files_dir();
    }
    return get_dir();
  }

  Slice get_files_dir() const;

  bool is_test_dc() const;

  NetQueryCreator &net_query_creator() {
    return *net_query_creator_.get();
  }

  void set_net_query_stats(std::shared_ptr<NetQueryStats> net_query_stats);

  void set_net_query_dispatcher(unique_ptr<NetQueryDispatcher> net_query_dispatcher);

  NetQueryDispatcher &net_query_dispatcher() {
    CHECK(have_net_query_dispatcher());
    return *net_query_dispatcher_;
  }

  bool have_net_query_dispatcher() const {
    return net_query_dispatcher_.get() != nullptr;
  }

  void set_option_empty(Slice name);

  void set_option_boolean(Slice name, bool value);

  void set_option_integer(Slice name, int64 value);

  void set_option_string(Slice name, Slice value);

  bool have_option(Slice name) const;

  bool get_option_boolean(Slice name, bool default_value = false) const;

  int64 get_option_integer(Slice name, int64 default_value = 0) const;

  string get_option_string(Slice name, string default_value = "") const;

  bool is_server_time_reliable() const {
    return server_time_difference_was_updated_.load(std::memory_order_relaxed);
  }
  double server_time() const {
    return Time::now() + get_server_time_difference();
  }
  int32 unix_time() const {
    return to_unix_time(server_time());
  }

  void update_server_time_difference(double diff, bool force);

  void save_server_time();

  double get_server_time_difference() const {
    return server_time_difference_.load(std::memory_order_relaxed);
  }

  void update_dns_time_difference(double diff);

  double get_dns_time_difference() const;

  ActorId<StateManager> state_manager() const {
    return state_manager_;
  }
  void set_state_manager(ActorId<StateManager> state_manager) {
    state_manager_ = state_manager;
  }

  ActorId<Td> td() const {
    return td_;
  }

  ActorId<AccountManager> account_manager() const {
    return account_manager_;
  }
  void set_account_manager(ActorId<AccountManager> account_manager) {
    account_manager_ = account_manager;
  }

  ActorId<AnimationsManager> animations_manager() const {
    return animations_manager_;
  }
  void set_animations_manager(ActorId<AnimationsManager> animations_manager) {
    animations_manager_ = animations_manager;
  }

  ActorId<AttachMenuManager> attach_menu_manager() const {
    return attach_menu_manager_;
  }
  void set_attach_menu_manager(ActorId<AttachMenuManager> attach_menu_manager) {
    attach_menu_manager_ = attach_menu_manager;
  }

  void set_auth_manager(ActorId<AuthManager> auth_manager) {
    auth_manager_ = auth_manager;
  }

  ActorId<AutosaveManager> autosave_manager() const {
    return autosave_manager_;
  }
  void set_autosave_manager(ActorId<AutosaveManager> autosave_manager) {
    autosave_manager_ = autosave_manager;
  }

  ActorId<BackgroundManager> background_manager() const {
    return background_manager_;
  }
  void set_background_manager(ActorId<BackgroundManager> background_manager) {
    background_manager_ = background_manager;
  }

  ActorId<BoostManager> boost_manager() const {
    return boost_manager_;
  }
  void set_boost_manager(ActorId<BoostManager> boost_manager) {
    boost_manager_ = boost_manager;
  }

  ActorId<CallManager> call_manager() const {
    return call_manager_;
  }
  void set_call_manager(ActorId<CallManager> call_manager) {
    call_manager_ = call_manager;
  }

  ActorId<ConfigManager> config_manager() const {
    return config_manager_;
  }
  void set_config_manager(ActorId<ConfigManager> config_manager) {
    config_manager_ = config_manager;
  }

  ActorId<ContactsManager> contacts_manager() const {
    return contacts_manager_;
  }
  void set_contacts_manager(ActorId<ContactsManager> contacts_manager) {
    contacts_manager_ = contacts_manager;
  }

  ActorId<DialogFilterManager> dialog_filter_manager() const {
    return dialog_filter_manager_;
  }
  void set_dialog_filter_manager(ActorId<DialogFilterManager> dialog_filter_manager) {
    dialog_filter_manager_ = std::move(dialog_filter_manager);
  }

  ActorId<DownloadManager> download_manager() const {
    return download_manager_;
  }
  void set_download_manager(ActorId<DownloadManager> download_manager) {
    download_manager_ = std::move(download_manager);
  }

  ActorId<FileManager> file_manager() const {
    return file_manager_;
  }
  void set_file_manager(ActorId<FileManager> file_manager) {
    file_manager_ = std::move(file_manager);
  }

  ActorId<FileReferenceManager> file_reference_manager() const {
    return file_reference_manager_;
  }
  void set_file_reference_manager(ActorId<FileReferenceManager> file_reference_manager) {
    file_reference_manager_ = std::move(file_reference_manager);
  }

  ActorId<ForumTopicManager> forum_topic_manager() const {
    return forum_topic_manager_;
  }
  void set_forum_topic_manager(ActorId<ForumTopicManager> forum_topic_manager) {
    forum_topic_manager_ = forum_topic_manager;
  }

  ActorId<GameManager> game_manager() const {
    return game_manager_;
  }
  void set_game_manager(ActorId<GameManager> game_manager) {
    game_manager_ = game_manager;
  }

  ActorId<GroupCallManager> group_call_manager() const {
    return group_call_manager_;
  }
  void set_group_call_manager(ActorId<GroupCallManager> group_call_manager) {
    group_call_manager_ = group_call_manager;
  }

  ActorId<LanguagePackManager> language_pack_manager() const {
    return language_pack_manager_;
  }
  void set_language_pack_manager(ActorId<LanguagePackManager> language_pack_manager) {
    language_pack_manager_ = language_pack_manager;
  }

  ActorId<LinkManager> link_manager() const {
    return link_manager_;
  }
  void set_link_manager(ActorId<LinkManager> link_manager) {
    link_manager_ = link_manager;
  }

  ActorId<MessagesManager> messages_manager() const {
    return messages_manager_;
  }
  void set_messages_manager(ActorId<MessagesManager> messages_manager) {
    messages_manager_ = messages_manager;
  }

  ActorId<NotificationManager> notification_manager() const {
    return notification_manager_;
  }
  void set_notification_manager(ActorId<NotificationManager> notification_manager) {
    notification_manager_ = notification_manager;
  }

  ActorId<NotificationSettingsManager> notification_settings_manager() const {
    return notification_settings_manager_;
  }
  void set_notification_settings_manager(ActorId<NotificationSettingsManager> notification_settings_manager) {
    notification_settings_manager_ = notification_settings_manager;
  }

  void set_option_manager(OptionManager *option_manager) {
    option_manager_ = option_manager;
  }

  ActorId<PasswordManager> password_manager() const {
    return password_manager_;
  }
  void set_password_manager(ActorId<PasswordManager> password_manager) {
    password_manager_ = password_manager;
  }

  ActorId<ReactionManager> reaction_manager() const {
    return reaction_manager_;
  }
  void set_reaction_manager(ActorId<ReactionManager> reaction_manager) {
    reaction_manager_ = reaction_manager;
  }

  ActorId<SecretChatsManager> secret_chats_manager() const {
    return secret_chats_manager_;
  }
  void set_secret_chats_manager(ActorId<SecretChatsManager> secret_chats_manager) {
    secret_chats_manager_ = secret_chats_manager;
  }

  ActorId<SponsoredMessageManager> sponsored_message_manager() const {
    return sponsored_message_manager_;
  }
  void set_sponsored_message_manager(ActorId<SponsoredMessageManager> sponsored_message_manager) {
    sponsored_message_manager_ = sponsored_message_manager;
  }

  ActorId<StickersManager> stickers_manager() const {
    return stickers_manager_;
  }
  void set_stickers_manager(ActorId<StickersManager> stickers_manager) {
    stickers_manager_ = stickers_manager;
  }

  ActorId<StorageManager> storage_manager() const {
    return storage_manager_;
  }
  void set_storage_manager(ActorId<StorageManager> storage_manager) {
    storage_manager_ = storage_manager;
  }

  ActorId<StoryManager> story_manager() const {
    return story_manager_;
  }
  void set_story_manager(ActorId<StoryManager> story_manager) {
    story_manager_ = story_manager;
  }

  ActorId<ThemeManager> theme_manager() const {
    return theme_manager_;
  }
  void set_theme_manager(ActorId<ThemeManager> theme_manager) {
    theme_manager_ = theme_manager;
  }

  ActorId<TopDialogManager> top_dialog_manager() const {
    return top_dialog_manager_;
  }
  void set_top_dialog_manager(ActorId<TopDialogManager> top_dialog_manager) {
    top_dialog_manager_ = top_dialog_manager;
  }

  ActorId<UpdatesManager> updates_manager() const {
    return updates_manager_;
  }
  void set_updates_manager(ActorId<UpdatesManager> updates_manager) {
    updates_manager_ = updates_manager;
  }

  ActorId<WebPagesManager> web_pages_manager() const {
    return web_pages_manager_;
  }
  void set_web_pages_manager(ActorId<WebPagesManager> web_pages_manager) {
    web_pages_manager_ = web_pages_manager;
  }

  ActorId<ConnectionCreator> connection_creator() const;
  void set_connection_creator(ActorOwn<ConnectionCreator> connection_creator);

  ActorId<TempAuthKeyWatchdog> temp_auth_key_watchdog() const;
  void set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog> actor);

  MtprotoHeader &mtproto_header();
  void set_mtproto_header(unique_ptr<MtprotoHeader> mtproto_header);
  bool have_mtproto_header() const {
    return mtproto_header_ != nullptr;
  }

  bool use_file_database() const;

  bool use_sqlite_pmc() const;

  bool use_chat_info_database() const;

  bool use_message_database() const;

  bool keep_media_order() const {
    return use_file_database();
  }

  int32 get_database_scheduler_id() {
    return database_scheduler_id_;
  }

  int32 get_gc_scheduler_id() const {
    return gc_scheduler_id_;
  }

  int32 get_slow_net_scheduler_id() const {
    return slow_net_scheduler_id_;
  }

  DcId get_webfile_dc_id() const;

  std::shared_ptr<DhConfig> get_dh_config() {
#if !TD_HAVE_ATOMIC_SHARED_PTR
    std::lock_guard<std::mutex> guard(dh_config_mutex_);
    auto res = dh_config_;
    return res;
#else
    return atomic_load(&dh_config_);
#endif
  }

  void set_dh_config(std::shared_ptr<DhConfig> new_dh_config) {
#if !TD_HAVE_ATOMIC_SHARED_PTR
    std::lock_guard<std::mutex> guard(dh_config_mutex_);
    dh_config_ = std::move(new_dh_config);
#else
    atomic_store(&dh_config_, std::move(new_dh_config));
#endif
  }

  static Status request_aborted_error() {
    return Status::Error(500, "Request aborted");
  }

  template <class T>
  void ignore_result_if_closing(Result<T> &result) const {
    if (close_flag() && result.is_ok()) {
      result = request_aborted_error();
    }
  }

  void set_close_flag() {
    close_flag_ = true;
  }
  bool close_flag() const {
    return close_flag_.load();
  }

  Status close_status() const {
    return close_flag() ? request_aborted_error() : Status::OK();
  }

  bool is_expected_error(const Status &error) const {
    CHECK(error.is_error());
    if (error.code() == 401) {
      // authorization is lost
      return true;
    }
    if (error.code() == 420 || error.code() == 429) {
      // flood wait
      return true;
    }
    return close_flag();
  }

  static int32 get_retry_after(int32 error_code, Slice error_message);

  const std::vector<std::shared_ptr<NetStatsCallback>> &get_net_stats_file_callbacks() {
    return net_stats_file_callbacks_;
  }
  void set_net_stats_file_callbacks(std::vector<std::shared_ptr<NetStatsCallback>> callbacks) {
    net_stats_file_callbacks_ = std::move(callbacks);
  }

  int64 get_location_access_hash(double latitude, double longitude);

  void add_location_access_hash(double latitude, double longitude, int64 access_hash);

  void set_store_all_files_in_files_directory(bool flag) {
    store_all_files_in_files_directory_ = flag;
  }

 private:
  std::shared_ptr<DhConfig> dh_config_;

  unique_ptr<TdDb> td_db_;

  ActorId<Td> td_;
  ActorId<AccountManager> account_manager_;
  ActorId<AnimationsManager> animations_manager_;
  ActorId<AttachMenuManager> attach_menu_manager_;
  ActorId<AuthManager> auth_manager_;
  ActorId<AutosaveManager> autosave_manager_;
  ActorId<BackgroundManager> background_manager_;
  ActorId<BoostManager> boost_manager_;
  ActorId<CallManager> call_manager_;
  ActorId<ConfigManager> config_manager_;
  ActorId<ContactsManager> contacts_manager_;
  ActorId<DialogFilterManager> dialog_filter_manager_;
  ActorId<DownloadManager> download_manager_;
  ActorId<FileManager> file_manager_;
  ActorId<FileReferenceManager> file_reference_manager_;
  ActorId<ForumTopicManager> forum_topic_manager_;
  ActorId<GameManager> game_manager_;
  ActorId<GroupCallManager> group_call_manager_;
  ActorId<LanguagePackManager> language_pack_manager_;
  ActorId<LinkManager> link_manager_;
  ActorId<MessagesManager> messages_manager_;
  ActorId<NotificationManager> notification_manager_;
  ActorId<NotificationSettingsManager> notification_settings_manager_;
  ActorId<PasswordManager> password_manager_;
  ActorId<ReactionManager> reaction_manager_;
  ActorId<SecretChatsManager> secret_chats_manager_;
  ActorId<SponsoredMessageManager> sponsored_message_manager_;
  ActorId<StickersManager> stickers_manager_;
  ActorId<StorageManager> storage_manager_;
  ActorId<StoryManager> story_manager_;
  ActorId<ThemeManager> theme_manager_;
  ActorId<TopDialogManager> top_dialog_manager_;
  ActorId<UpdatesManager> updates_manager_;
  ActorId<WebPagesManager> web_pages_manager_;
  ActorOwn<ConnectionCreator> connection_creator_;
  ActorOwn<TempAuthKeyWatchdog> temp_auth_key_watchdog_;

  unique_ptr<MtprotoHeader> mtproto_header_;

  OptionManager *option_manager_ = nullptr;

  int32 database_scheduler_id_ = 0;
  int32 gc_scheduler_id_ = 0;
  int32 slow_net_scheduler_id_ = 0;

  std::atomic<bool> store_all_files_in_files_directory_{false};

  std::atomic<double> server_time_difference_{0.0};
  std::atomic<bool> server_time_difference_was_updated_{false};
  std::atomic<double> dns_time_difference_{0.0};
  std::atomic<bool> dns_time_difference_was_updated_{false};
  std::atomic<bool> close_flag_{false};
  std::atomic<double> system_time_saved_at_{-1e10};
  double saved_diff_ = 0.0;
  double saved_system_time_ = 0.0;

#if !TD_HAVE_ATOMIC_SHARED_PTR
  std::mutex dh_config_mutex_;
#endif

  std::vector<std::shared_ptr<NetStatsCallback>> net_stats_file_callbacks_;

  ActorId<StateManager> state_manager_;

  LazySchedulerLocalStorage<unique_ptr<NetQueryCreator>> net_query_creator_;
  unique_ptr<NetQueryDispatcher> net_query_dispatcher_;

  static int64 get_location_key(double latitude, double longitude);

  FlatHashMap<int64, int64> location_access_hashes_;

  int32 to_unix_time(double server_time) const;

  const OptionManager *get_option_manager() const;

  OptionManager *get_option_manager();

  void do_save_server_time_difference();

  void do_close(Promise<> on_finish, bool destroy_flag);
};

#define G() G_impl(__FILE__, __LINE__)

inline Global *G_impl(const char *file, int line) {
  ActorContext *context = Scheduler::context();
  LOG_CHECK(context != nullptr && context->get_id() == Global::ID)
      << "Context = " << context << " in " << file << " at " << line;
  return static_cast<Global *>(context);
}

double get_global_server_time();

}  // namespace td
