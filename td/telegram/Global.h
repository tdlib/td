//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DhConfig.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/TdParameters.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SchedulerLocalStorage.h"

#include "td/net/NetStats.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace td {
class AnimationsManager;
class BackgroundManager;
class CallManager;
class ConfigManager;
class ConfigShared;
class ConnectionCreator;
class ContactsManager;
class FileManager;
class FileReferenceManager;
class GroupCallManager;
class LanguagePackManager;
class MessagesManager;
class MtprotoHeader;
class NetQueryDispatcher;
class NotificationManager;
class PasswordManager;
class SecretChatsManager;
class StateManager;
class StickersManager;
class StorageManager;
class Td;
class TdDb;
class TempAuthKeyWatchdog;
class TopDialogManager;
class UpdatesManager;
class WebPagesManager;
}  // namespace td

namespace td {

class Global : public ActorContext {
 public:
  Global();
  ~Global() override;
  Global(const Global &) = delete;
  Global &operator=(const Global &) = delete;
  Global(Global &&other) = delete;
  Global &operator=(Global &&other) = delete;

  static constexpr int32 ID = -572104940;
  int32 get_id() const override {
    return ID;
  }

#define td_db() get_td_db_impl(__FILE__, __LINE__)
  TdDb *get_td_db_impl(const char *file, int line) {
    LOG_CHECK(td_db_) << close_flag() << " " << file << " " << line;
    return td_db_.get();
  }

  void close_all(Promise<> on_finished);
  void close_and_destroy_all(Promise<> on_finished);

  Status init(const TdParameters &parameters, ActorId<Td> td, unique_ptr<TdDb> td_db_ptr) TD_WARN_UNUSED_RESULT;

  Slice get_dir() const {
    return parameters_.database_directory;
  }
  Slice get_secure_files_dir() const {
    if (store_all_files_in_files_directory_) {
      return get_files_dir();
    }
    return get_dir();
  }
  Slice get_files_dir() const {
    return parameters_.files_directory;
  }
  bool is_test_dc() const {
    return parameters_.use_test_dc;
  }

  bool ignore_background_updates() const;

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

  void set_shared_config(unique_ptr<ConfigShared> shared_config);

  ConfigShared &shared_config() {
    CHECK(shared_config_.get() != nullptr);
    return *shared_config_;
  }

  bool is_server_time_reliable() const {
    return server_time_difference_was_updated_;
  }
  double to_server_time(double now) const {
    return now + get_server_time_difference();
  }
  double server_time() const {
    return to_server_time(Time::now());
  }
  double server_time_cached() const {
    return to_server_time(Time::now_cached());
  }
  int32 unix_time() const {
    return to_unix_time(server_time());
  }
  int32 unix_time_cached() const {
    return to_unix_time(server_time_cached());
  }

  void update_server_time_difference(double diff);

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

  ActorId<AnimationsManager> animations_manager() const {
    return animations_manager_;
  }
  void set_animations_manager(ActorId<AnimationsManager> animations_manager) {
    animations_manager_ = animations_manager;
  }

  ActorId<BackgroundManager> background_manager() const {
    return background_manager_;
  }
  void set_background_manager(ActorId<BackgroundManager> background_manager) {
    background_manager_ = background_manager;
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

  ActorId<PasswordManager> password_manager() const {
    return password_manager_;
  }
  void set_password_manager(ActorId<PasswordManager> password_manager) {
    password_manager_ = password_manager;
  }

  ActorId<SecretChatsManager> secret_chats_manager() const {
    return secret_chats_manager_;
  }
  void set_secret_chats_manager(ActorId<SecretChatsManager> secret_chats_manager) {
    secret_chats_manager_ = secret_chats_manager;
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

  const TdParameters &parameters() const {
    return parameters_;
  }

  int32 get_my_id() const {
    return my_id_;
  }
  void set_my_id(int32 my_id) {
    my_id_ = my_id;
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
    dh_config_ = new_dh_config;
#else
    atomic_store(&dh_config_, std::move(new_dh_config));
#endif
  }

  void set_close_flag() {
    close_flag_ = true;
  }
  bool close_flag() const {
    return close_flag_.load();
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
  ActorId<AnimationsManager> animations_manager_;
  ActorId<BackgroundManager> background_manager_;
  ActorId<CallManager> call_manager_;
  ActorId<ConfigManager> config_manager_;
  ActorId<ContactsManager> contacts_manager_;
  ActorId<FileManager> file_manager_;
  ActorId<FileReferenceManager> file_reference_manager_;
  ActorId<GroupCallManager> group_call_manager_;
  ActorId<LanguagePackManager> language_pack_manager_;
  ActorId<MessagesManager> messages_manager_;
  ActorId<NotificationManager> notification_manager_;
  ActorId<PasswordManager> password_manager_;
  ActorId<SecretChatsManager> secret_chats_manager_;
  ActorId<StickersManager> stickers_manager_;
  ActorId<StorageManager> storage_manager_;
  ActorId<TopDialogManager> top_dialog_manager_;
  ActorId<UpdatesManager> updates_manager_;
  ActorId<WebPagesManager> web_pages_manager_;
  ActorOwn<ConnectionCreator> connection_creator_;
  ActorOwn<TempAuthKeyWatchdog> temp_auth_key_watchdog_;

  unique_ptr<MtprotoHeader> mtproto_header_;

  TdParameters parameters_;
  int32 gc_scheduler_id_;
  int32 slow_net_scheduler_id_;

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

  unique_ptr<ConfigShared> shared_config_;

  int32 my_id_ = 0;  // hack

  static int64 get_location_key(double latitude, double longitude);

  std::unordered_map<int64, int64> location_access_hashes_;

  int32 to_unix_time(double server_time) const;

  void do_save_server_time_difference();

  void do_close(Promise<> on_finish, bool destroy_flag);
};

#define G() G_impl(__FILE__, __LINE__)

inline Global *G_impl(const char *file, int line) {
  ActorContext *context = Scheduler::context();
  CHECK(context);
  LOG_CHECK(context->get_id() == Global::ID) << "In " << file << " at " << line;
  return static_cast<Global *>(context);
}

double get_global_server_time();

}  // namespace td
