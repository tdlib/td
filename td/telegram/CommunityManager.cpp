//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CommunityManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelType.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DialogPhoto.hpp"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/misc.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/utf8.h"

namespace td {

class GetCommunitiesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetCommunitiesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputChannel> &&input_channel) {
    CHECK(input_channel != nullptr);
    vector<tl_object_ptr<telegram_api::InputChannel>> input_channels;
    input_channels.push_back(std::move(input_channel));
    send_query(G()->net_query_creator().create(telegram_api::channels_getChannels(std::move(input_channels))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getChannels>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto chats_ptr = result_ptr.move_as_ok();
    if (chats_ptr->get_id() == telegram_api::messages_chats::ID) {
      auto chats = telegram_api::move_object_as<telegram_api::messages_chats>(chats_ptr);
      td_->chat_manager_->on_get_chats(std::move(chats->chats_), "GetCommunitiesQuery");
    } else {
      LOG(ERROR) << "Receive " << to_string(chats_ptr);
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetFullCommunityQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  CommunityId community_id_;

 public:
  explicit GetFullCommunityQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(CommunityId community_id, telegram_api::object_ptr<telegram_api::InputChannel> &&input_channel) {
    community_id_ = community_id;
    send_query(G()->net_query_creator().create(telegram_api::channels_getFullChannel(std::move(input_channel))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getFullChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetFullCommunityQuery: " << to_string(ptr);
    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetFullCommunityQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetFullCommunityQuery");
    if (ptr->full_chat_->get_id() != telegram_api::communityFull::ID) {
      LOG(ERROR) << "Receive " << to_string(ptr);
      return on_error(Status::Error(500, "Receive invalid response"));
    }
    td_->community_manager_->on_get_community_full(
        telegram_api::move_object_as<telegram_api::communityFull>(ptr->full_chat_));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CreateCommunityQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::communityId>> promise_;
  DialogId dialog_id_;

 public:
  explicit CreateCommunityQuery(Promise<td_api::object_ptr<td_api::communityId>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &title, DialogId dialog_id, bool is_hidden) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::communities_create(0, is_hidden, title, string(), std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::communities_create>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateCommunityQuery: " << to_string(ptr);
    auto community_id = UpdatesManager::get_community_id(ptr.get());
    if (!community_id.is_valid()) {
      return promise_.set_value(nullptr);
    }
    auto promise = PromiseCreator::lambda([community_id, promise = std::move(promise_)](Result<Unit> result) mutable {
      send_closure(G()->community_manager(), &CommunityManager::finish_create_community, community_id,
                   std::move(promise));
    });
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "CreateCommunityQuery");
    promise_.set_error(std::move(status));
  }
};

class EditCommunityTitleQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditCommunityTitleQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(CommunityId community_id, const string &title) {
    auto input_community = td_->community_manager_->get_input_community(community_id);
    CHECK(input_community != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::channels_editTitle(std::move(input_community), title),
                                               {{community_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_editTitle>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditCommunityTitleQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "CHAT_NOT_MODIFIED" && !td_->auth_manager_->is_bot()) {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

template <class StorerT>
void CommunityManager::Community::store(StorerT &storer) const {
  using td::store;
  bool has_photo = photo.small_file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_photo);
  STORE_FLAG(collapsed_in_dialogs);
  END_STORE_FLAGS();
  store(access_hash, storer);
  store(title, storer);
  store(date, storer);
  store(status, storer);
  store(cache_version, storer);
  store(default_permissions, storer);
  if (has_photo) {
    store(photo, storer);
  }
}

template <class ParserT>
void CommunityManager::Community::parse(ParserT &parser) {
  using td::parse;
  bool has_photo;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_photo);
  PARSE_FLAG(collapsed_in_dialogs);
  END_PARSE_FLAGS();
  parse(access_hash, parser);
  parse(title, parser);
  parse(date, parser);
  parse(status, parser);
  parse(cache_version, parser);
  parse(default_permissions, parser);
  if (has_photo) {
    parse(photo, parser);
  }

  if (!check_utf8(title)) {
    LOG(ERROR) << "Have invalid title \"" << title << '"';
    title.clear();
    cache_version = 0;
  }
}

CommunityManager::CommunityDialog::CommunityDialog(const telegram_api::object_ptr<telegram_api::communityPeer> &peer)
    : dialog_id_(peer->peer_), can_view_history_(peer->can_view_history_), is_visible_(peer->visible_) {
}

void CommunityManager::CommunityDialog::add_dependencies(Dependencies &dependencies) const {
  dependencies.add_dialog_and_dependencies(dialog_id_);
}

td_api::object_ptr<td_api::communityChat> CommunityManager::CommunityDialog::get_community_chat_object(
    const Td *td) const {
  return td_api::make_object<td_api::communityChat>(
      td->dialog_manager_->get_chat_id_object(dialog_id_, "communityChat"), can_view_history_, !is_visible_);
}

bool operator==(const CommunityManager::CommunityDialog &lhs, const CommunityManager::CommunityDialog &rhs) {
  return lhs.dialog_id_ == rhs.dialog_id_ && lhs.can_view_history_ == rhs.can_view_history_ &&
         lhs.is_visible_ == rhs.is_visible_;
}

template <class StorerT>
void CommunityManager::CommunityDialog::store(StorerT &storer) const {
  using td::store;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(can_view_history_);
  STORE_FLAG(is_visible_);
  END_STORE_FLAGS();
  store(dialog_id_, storer);
}

template <class ParserT>
void CommunityManager::CommunityDialog::parse(ParserT &parser) {
  using td::parse;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(can_view_history_);
  PARSE_FLAG(is_visible_);
  END_PARSE_FLAGS();
  parse(dialog_id_, parser);
}

template <class StorerT>
void CommunityManager::CommunityFull::store(StorerT &storer) const {
  using td::store;
  bool has_about = !about.empty();
  bool has_photo = !photo.is_empty();
  bool has_administrator_count = administrator_count != 0;
  bool has_banned_count = banned_count != 0;
  bool has_peer_link_requests_pending = peer_link_requests_pending != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_about);
  STORE_FLAG(has_photo);
  STORE_FLAG(has_administrator_count);
  STORE_FLAG(has_banned_count);
  STORE_FLAG(has_peer_link_requests_pending);
  END_STORE_FLAGS();
  store(dialogs, storer);
  if (has_about) {
    store(about, storer);
  }
  if (has_photo) {
    store(photo, storer);
  }
  if (has_administrator_count) {
    store(administrator_count, storer);
  }
  if (has_banned_count) {
    store(banned_count, storer);
  }
  if (has_peer_link_requests_pending) {
    store(peer_link_requests_pending, storer);
  }
}

template <class ParserT>
void CommunityManager::CommunityFull::parse(ParserT &parser) {
  using td::parse;
  bool has_about;
  bool has_photo;
  bool has_administrator_count;
  bool has_banned_count;
  bool has_peer_link_requests_pending;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_about);
  PARSE_FLAG(has_photo);
  PARSE_FLAG(has_administrator_count);
  PARSE_FLAG(has_banned_count);
  PARSE_FLAG(has_peer_link_requests_pending);
  END_PARSE_FLAGS();
  parse(dialogs, parser);
  if (has_about) {
    parse(about, parser);
  }
  if (has_photo) {
    parse(photo, parser);
  }
  if (has_administrator_count) {
    parse(administrator_count, parser);
  }
  if (has_banned_count) {
    parse(banned_count, parser);
  }
  if (has_peer_link_requests_pending) {
    parse(peer_link_requests_pending, parser);
  }
}

CommunityManager::CommunityManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  get_community_queries_.set_merge_function([this](vector<int64> query_ids, Promise<Unit> &&promise) {
    TRY_STATUS_PROMISE(promise, G()->close_status());
    CHECK(query_ids.size() == 1u);
    auto input_community = get_input_community(CommunityId(query_ids[0]));
    if (input_community == nullptr) {
      return promise.set_error(400, "Community not found");
    }
    td_->create_handler<GetCommunitiesQuery>(std::move(promise))->send(std::move(input_community));
  });
}

CommunityManager::~CommunityManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), communities_, unknown_communities_,
                                              communities_full_, community_full_file_source_ids_,
                                              unavailable_community_fulls_);
}

void CommunityManager::tear_down() {
  parent_.reset();
}

class CommunityManager::CommunityLogEvent {
 public:
  CommunityId community_id;
  const Community *c_in = nullptr;
  unique_ptr<Community> c_out;

  CommunityLogEvent() = default;

  CommunityLogEvent(CommunityId community_id, const Community *c) : community_id(community_id), c_in(c) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(community_id, storer);
    td::store(*c_in, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(community_id, parser);
    td::parse(c_out, parser);
  }
};

void CommunityManager::save_community(Community *c, CommunityId community_id, bool from_binlog) {
  if (!G()->use_chat_info_database()) {
    return;
  }
  CHECK(c != nullptr);
  if (!c->is_saved) {
    if (!from_binlog) {
      auto log_event = CommunityLogEvent(community_id, c);
      auto storer = get_log_event_storer(log_event);
      if (c->log_event_id == 0) {
        c->log_event_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::Communities, storer);
      } else {
        binlog_rewrite(G()->td_db()->get_binlog(), c->log_event_id, LogEvent::HandlerType::Communities, storer);
      }
    }

    save_community_to_database(c, community_id);
    return;
  }
}

void CommunityManager::on_binlog_community_event(BinlogEvent &&event) {
  if (!G()->use_chat_info_database()) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  CommunityLogEvent log_event;
  if (log_event_parse(log_event, event.get_data()).is_error()) {
    LOG(ERROR) << "Failed to load a community from binlog";
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  auto community_id = log_event.community_id;
  if (have_community(community_id) || !community_id.is_valid()) {
    LOG(ERROR) << "Skip adding already added " << community_id;
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  LOG(INFO) << "Add " << community_id << " from binlog";
  communities_.set(community_id, std::move(log_event.c_out));

  Community *c = get_community(community_id);
  CHECK(c != nullptr);
  c->log_event_id = event.id_;

  update_community(c, community_id, true, false);
}

string CommunityManager::get_community_database_key(CommunityId community_id) {
  return PSTRING() << "community" << community_id.get();
}

string CommunityManager::get_community_database_value(const Community *c) {
  return log_event_store(*c).as_slice().str();
}

void CommunityManager::save_community_to_database(Community *c, CommunityId community_id) {
  CHECK(c != nullptr);
  if (c->is_being_saved) {
    return;
  }
  if (loaded_from_database_communities_.count(community_id)) {
    save_community_to_database_impl(c, community_id, get_community_database_value(c));
    return;
  }
  if (load_community_from_database_queries_.count(community_id) != 0) {
    return;
  }

  load_community_from_database_impl(community_id, Auto());
}

void CommunityManager::save_community_to_database_impl(Community *c, CommunityId community_id, string value) {
  CHECK(c != nullptr);
  CHECK(load_community_from_database_queries_.count(community_id) == 0);
  CHECK(!c->is_being_saved);
  c->is_being_saved = true;
  c->is_saved = true;
  LOG(INFO) << "Trying to save to database " << community_id;
  G()->td_db()->get_sqlite_pmc()->set(get_community_database_key(community_id), std::move(value),
                                      PromiseCreator::lambda([community_id](Result<> result) {
                                        send_closure(G()->community_manager(),
                                                     &CommunityManager::on_save_community_to_database, community_id,
                                                     result.is_ok());
                                      }));
}

void CommunityManager::on_save_community_to_database(CommunityId community_id, bool success) {
  if (G()->close_flag()) {
    return;
  }

  Community *c = get_community(community_id);
  CHECK(c != nullptr);
  CHECK(c->is_being_saved);
  CHECK(load_community_from_database_queries_.count(community_id) == 0);
  c->is_being_saved = false;

  if (!success) {
    LOG(ERROR) << "Failed to save " << community_id << " to database";
    c->is_saved = false;
  } else {
    LOG(INFO) << "Successfully saved " << community_id << " to database";
  }
  if (c->is_saved) {
    if (c->log_event_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->log_event_id);
      c->log_event_id = 0;
    }
  } else {
    save_community(c, community_id, c->log_event_id != 0);
  }
}

void CommunityManager::load_community_from_database(Community *c, CommunityId community_id, Promise<Unit> promise) {
  if (loaded_from_database_communities_.count(community_id)) {
    promise.set_value(Unit());
    return;
  }

  CHECK(c == nullptr || !c->is_being_saved);
  load_community_from_database_impl(community_id, std::move(promise));
}

void CommunityManager::load_community_from_database_impl(CommunityId community_id, Promise<Unit> promise) {
  LOG(INFO) << "Load " << community_id << " from database";
  auto &load_community_queries = load_community_from_database_queries_[community_id];
  load_community_queries.push_back(std::move(promise));
  if (load_community_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(
        get_community_database_key(community_id), PromiseCreator::lambda([community_id](string value) {
          send_closure(G()->community_manager(), &CommunityManager::on_load_community_from_database, community_id,
                       std::move(value), false);
        }));
  }
}

void CommunityManager::on_load_community_from_database(CommunityId community_id, string value, bool force) {
  if (G()->close_flag() && !force) {
    // the community is in Binlog and will be saved after restart
    return;
  }

  CHECK(community_id.is_valid());
  if (!loaded_from_database_communities_.insert(community_id).second) {
    return;
  }

  auto it = load_community_from_database_queries_.find(community_id);
  vector<Promise<Unit>> promises;
  if (it != load_community_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_community_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << community_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_community_database_key(community_id), Auto());
  //  return;

  Community *c = get_community(community_id);
  if (c == nullptr) {
    if (!value.empty()) {
      c = add_community(community_id, "on_load_community_from_database");

      if (log_event_parse(*c, value).is_error()) {
        LOG(ERROR) << "Failed to load " << community_id << " from database";
        communities_.erase(community_id);
      } else {
        c->is_saved = true;
        update_community(c, community_id, true, true);
      }
    }
  } else {
    CHECK(!c->is_saved);  // community can't be saved before load completes
    CHECK(!c->is_being_saved);
    auto new_value = get_community_database_value(c);
    if (value != new_value) {
      save_community_to_database_impl(c, community_id, std::move(new_value));
    } else if (c->log_event_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->log_event_id);
      c->log_event_id = 0;
    }
  }
  set_promises(promises);
}

bool CommunityManager::have_community(CommunityId community_id) const {
  return communities_.count(community_id) > 0;
}

bool CommunityManager::have_accessible_community(CommunityId community_id) const {
  const auto *c = get_community(community_id);
  return c != nullptr && c->access_hash != 0;
}

const CommunityManager::Community *CommunityManager::get_community(CommunityId community_id) const {
  return communities_.get_pointer(community_id);
}

CommunityManager::Community *CommunityManager::get_community(CommunityId community_id) {
  return communities_.get_pointer(community_id);
}

const CommunityManager::CommunityFull *CommunityManager::get_community_full_const(CommunityId community_id) const {
  return communities_full_.get_pointer(community_id);
}

CommunityManager::CommunityFull *CommunityManager::get_community_full(CommunityId community_id, bool only_local,
                                                                      const char *source) {
  auto community_full = communities_full_.get_pointer(community_id);
  if (community_full == nullptr) {
    return nullptr;
  }

  if (!only_local && !td_->auth_manager_->is_bot()) {
    reload_community_full(community_id, Auto(), source);
  }

  return community_full;
}

CommunityManager::Community *CommunityManager::add_community(CommunityId community_id, const char *source) {
  CHECK(community_id.is_valid());
  auto &community_ptr = communities_[community_id];
  if (community_ptr == nullptr) {
    community_ptr = make_unique<Community>();
  }
  return community_ptr.get();
}

DialogParticipantStatus CommunityManager::get_community_status(CommunityId community_id) const {
  auto c = get_community(community_id);
  if (c == nullptr) {
    return DialogParticipantStatus::Banned(0, string());
  }
  return get_community_status(c);
}

DialogParticipantStatus CommunityManager::get_community_status(const Community *c) {
  c->status.update_restrictions();
  return c->status;
}

DialogParticipantStatus CommunityManager::get_community_permissions(CommunityId community_id) const {
  auto c = get_community(community_id);
  if (c == nullptr) {
    return DialogParticipantStatus::Banned(0, string());
  }
  return get_community_permissions(c);
}

DialogParticipantStatus CommunityManager::get_community_permissions(const Community *c) const {
  c->status.update_restrictions();
  return c->status.apply_restrictions(c->default_permissions, false, td_->auth_manager_->is_bot());
}

void CommunityManager::reload_community(CommunityId community_id, Promise<Unit> &&promise, const char *source) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  if (!community_id.is_valid()) {
    return promise.set_error(400, "Invalid community identifier");
  }

  have_community_force(community_id, source);
  get_community_queries_.add_query(community_id.get(), std::move(promise), source);
}

bool CommunityManager::have_community_force(CommunityId community_id, const char *source) {
  return get_community_force(community_id, source) != nullptr;
}

CommunityManager::Community *CommunityManager::get_community_force(CommunityId community_id, const char *source) {
  if (!community_id.is_valid()) {
    return nullptr;
  }

  Community *c = get_community(community_id);
  if (c != nullptr) {
    return c;
  }
  if (!G()->use_chat_info_database()) {
    return nullptr;
  }
  if (loaded_from_database_communities_.count(community_id)) {
    return nullptr;
  }

  LOG(INFO) << "Trying to load " << community_id << " from database from " << source;
  on_load_community_from_database(
      community_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_community_database_key(community_id)), true);
  return get_community(community_id);
}

void CommunityManager::update_community(Community *c, CommunityId community_id, bool from_binlog, bool from_database) {
  CHECK(c != nullptr);

  if (c->is_being_updated) {
    LOG(ERROR) << "Detected recursive update of " << community_id;
  }
  c->is_being_updated = true;
  SCOPE_EXIT {
    c->is_being_updated = false;
  };

  LOG(DEBUG) << "Update " << community_id << ": need_save_to_database = " << c->need_save_to_database
             << ", is_changed = " << c->is_changed;
  c->need_save_to_database |= c->is_changed;
  if (c->need_save_to_database) {
    if (!from_database) {
      c->is_saved = false;
    }
    c->need_save_to_database = false;
  }
  if (c->is_changed) {
    send_closure(G()->td(), &Td::send_update, get_update_community_object(community_id, c));
    c->is_changed = false;
    c->is_update_community_sent = true;
  }

  if (!from_database) {
    save_community(c, community_id, from_binlog);
  }

  if (c->cache_version != Community::CACHE_VERSION && !c->is_repaired && !c->status.is_banned() &&
      c->access_hash != 0 && !G()->close_flag()) {
    c->is_repaired = true;

    LOG(INFO) << "Repairing cache of " << community_id;
    reload_community(community_id, Promise<Unit>(), "update_community");
  }
}

void CommunityManager::on_get_community(telegram_api::community &community, const char *source) {
  CommunityId community_id(community.id_);
  if (!community_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << community_id << " from " << source << ": " << to_string(community);
    return;
  }

  if (community.flags_ == 0 && community.access_hash_ == 0 && community.title_.empty()) {
    Community *c = get_community_force(community_id, source);
    if (c != nullptr) {
      LOG(ERROR) << "Receive from " << source << " empty " << community_id << ": " << to_string(community) << ", have "
                 << to_string(get_community_object(community_id, c));
    }
    return;
  }

  bool is_min = community.min_;
  auto access_hash = community.access_hash_;
  if (access_hash == 0 && !is_min) {
    LOG(ERROR) << "Receive non-min " << community_id << " without access_hash from " << source;
    return;
  }
  DialogParticipantStatus status = [&] {
    if (community.creator_) {
      return DialogParticipantStatus::Creator(!community.left_, false, string());
    } else if (community.admin_rights_ != nullptr) {
      return DialogParticipantStatus(false, std::move(community.admin_rights_), string(), ChannelType::Unknown);
    } else if (community.left_) {
      return DialogParticipantStatus::Left();
    } else {
      return DialogParticipantStatus::Member(0, string());
    }
  }();
  Community *c = add_community(community_id, "on_get_community");
  if (c->access_hash != access_hash && (!is_min || c->access_hash == 0)) {
    c->access_hash = access_hash;
    if (access_hash == 0 || c->access_hash == 0) {
      c->is_changed = true;
    } else {
      c->need_save_to_database = true;
    }
  }
  if (c->date != community.date_ && (!is_min || c->date == 0)) {
    c->date = community.date_;
    c->is_changed = true;
  }
  if (c->collapsed_in_dialogs != community.collapsed_in_dialogs_ && !is_min) {
    c->collapsed_in_dialogs = community.collapsed_in_dialogs_;
    c->need_save_to_database = true;
  }
  if (!is_min) {
    on_update_community_status(c, community_id, std::move(status));
  }
  on_update_community_title(c, community_id, std::move(community.title_));
  if (!c->status.is_banned()) {
    on_update_community_photo(c, community_id, std::move(community.photo_));
  }
  on_update_community_default_permissions(c, community_id,
                                          RestrictedRights(community.default_banned_rights_, ChannelType::Megagroup));

  if (c->cache_version != Community::CACHE_VERSION) {
    c->cache_version = Community::CACHE_VERSION;
    c->need_save_to_database = true;
  }
  c->is_received_from_server = true;
  update_community(c, community_id);
}

void CommunityManager::on_get_community_forbidden(telegram_api::communityForbidden &community, const char *source) {
  CommunityId community_id(community.id_);
  if (!community_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << community_id << " from " << source << ": " << to_string(community);
    return;
  }

  if (community.flags_ == 0 && community.access_hash_ == 0 && community.title_.empty()) {
    Community *c = get_community_force(community_id, source);
    if (c != nullptr) {
      LOG(ERROR) << "Receive from " << source << " empty " << community_id << ": " << to_string(community) << ", have "
                 << to_string(get_community_object(community_id, c));
    }
    return;
  }

  auto access_hash = community.access_hash_;
  Community *c = add_community(community_id, "on_get_community_forbidden");
  if (c->access_hash != access_hash) {
    c->access_hash = access_hash;
    if (access_hash == 0 || c->access_hash == 0) {
      c->is_changed = true;
    } else {
      c->need_save_to_database = true;
    }
  }
  if (c->date != 0) {
    c->date = 0;
    c->is_changed = true;
  }
  on_update_community_status(c, community_id, DialogParticipantStatus::Banned(0, string()));
  on_update_community_title(c, community_id, std::move(community.title_));
  on_update_community_photo(c, community_id, nullptr);

  telegram_api::object_ptr<telegram_api::chatBannedRights> banned_rights;  // == nullptr
  on_update_community_default_permissions(c, community_id, RestrictedRights(banned_rights, ChannelType::Megagroup));

  if (c->cache_version != Community::CACHE_VERSION) {
    c->cache_version = Community::CACHE_VERSION;
    c->need_save_to_database = true;
  }
  c->is_received_from_server = true;
  update_community(c, community_id);
}

void CommunityManager::on_update_community_photo(Community *c, CommunityId community_id,
                                                 telegram_api::object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  on_update_community_photo(c, community_id,
                            get_dialog_photo(td_->file_manager_.get(), community_id.get_fake_dialog_id(),
                                             c->access_hash, std::move(chat_photo_ptr)),
                            true);
}

void CommunityManager::on_update_community_photo(Community *c, CommunityId community_id, DialogPhoto &&photo,
                                                 bool invalidate_photo_cache) {
  if (td_->auth_manager_->is_bot()) {
    photo.minithumbnail.clear();
  }

  if (need_update_dialog_photo(c->photo, photo)) {
    LOG(DEBUG) << "Update photo of " << community_id << " from " << c->photo << " to " << photo;
    c->photo = std::move(photo);
    c->is_changed = true;

    if (invalidate_photo_cache) {
      auto community_full =
          get_community_full(community_id, true, "on_update_community_photo");  // must not load CommunityFull
      if (community_full != nullptr) {
        on_update_community_full_photo(community_full, community_id, Photo());
        if (c->photo.small_file_id.is_valid()) {
          reload_community_full(community_id, Auto(), "on_update_community_photo");
        }
        update_community_full(community_full, community_id, "on_update_community_photo");
      }
    }
  } else if (need_update_dialog_photo_minithumbnail(c->photo.minithumbnail, photo.minithumbnail)) {
    c->photo.minithumbnail = std::move(photo.minithumbnail);
    c->is_changed = true;
  }
}

void CommunityManager::on_update_community_title(Community *c, CommunityId community_id, string &&title) {
  if (c->title != title) {
    c->title = std::move(title);
    c->is_changed = true;
  }
}

void CommunityManager::on_update_community_status(Community *c, CommunityId community_id,
                                                  DialogParticipantStatus &&status) {
  if (c->status != status) {
    LOG(INFO) << "Update " << community_id << " status from " << c->status << " to " << status;
    c->status = status;
    c->is_changed = true;
  }
}

void CommunityManager::on_update_community_default_permissions(Community *c, CommunityId community_id,
                                                               RestrictedRights default_permissions) {
  if (c->default_permissions != default_permissions) {
    LOG(INFO) << "Update " << community_id << " default permissions from " << c->default_permissions << " to "
              << default_permissions;
    c->default_permissions = default_permissions;
    c->is_changed = true;
  }
}

CommunityManager::CommunityFull *CommunityManager::add_community_full(CommunityId community_id) {
  CHECK(community_id.is_valid());
  auto &community_full_ptr = communities_full_[community_id];
  if (community_full_ptr == nullptr) {
    community_full_ptr = make_unique<CommunityFull>();
  }
  return community_full_ptr.get();
}

string CommunityManager::get_community_full_database_key(CommunityId community_id) {
  return PSTRING() << "communityf" << community_id.get();
}

void CommunityManager::save_community_full(const CommunityFull *community_full, CommunityId community_id) {
  if (!G()->use_chat_info_database()) {
    return;
  }

  LOG(INFO) << "Trying to save to database full " << community_id;
  CHECK(community_full != nullptr);
  G()->td_db()->get_sqlite_pmc()->set(get_community_full_database_key(community_id),
                                      log_event_store(*community_full).as_slice().str(), Auto());
}

CommunityManager::CommunityFull *CommunityManager::get_community_full_force(CommunityId community_id, bool only_local,
                                                                            const char *source) {
  if (!have_community_force(community_id, source)) {
    return nullptr;
  }

  CommunityFull *community_full = get_community_full(community_id, only_local, source);
  if (community_full != nullptr) {
    return community_full;
  }
  if (!G()->use_chat_info_database()) {
    return nullptr;
  }
  if (!unavailable_community_fulls_.insert(community_id).second) {
    return nullptr;
  }

  LOG(INFO) << "Trying to load full " << community_id << " from database from " << source;
  on_load_community_full_from_database(
      community_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_community_full_database_key(community_id)), source);
  return get_community_full(community_id, only_local, source);
}

void CommunityManager::on_load_community_full_from_database(CommunityId community_id, string value,
                                                            const char *source) {
  LOG(INFO) << "Successfully loaded full " << community_id << " of size " << value.size() << " from database from "
            << source;
  //  G()->td_db()->get_sqlite_pmc()->erase(get_community_full_database_key(community_id), Auto());
  //  return;

  if (get_community_full_const(community_id) != nullptr || value.empty()) {
    return;
  }

  CommunityFull *community_full = add_community_full(community_id);
  auto status = log_event_parse(*community_full, value);
  if (status.is_error()) {
    // can't happen unless database is broken
    LOG(ERROR) << "Repair broken full " << community_id << ' ' << format::as_hex_dump<4>(Slice(value));

    // just clean all known data about the community and pretend that there was nothing in the database
    communities_full_.erase(community_id);
    G()->td_db()->get_sqlite_pmc()->erase(get_community_full_database_key(community_id), Auto());
    return;
  }

  Dependencies dependencies;
  dependencies.add(community_id);
  for (auto &dialog : community_full->dialogs) {
    dialog.add_dependencies(dependencies);
  }
  if (!dependencies.resolve_force(td_, source)) {
    communities_full_.erase(community_id);
    G()->td_db()->get_sqlite_pmc()->erase(get_community_full_database_key(community_id), Auto());
    return;
  }

  Community *c = get_community(community_id);
  CHECK(c != nullptr);

  bool need_reload_community_full = false;
  if (!is_same_dialog_photo(td_->file_manager_.get(), community_id.get_fake_dialog_id(), community_full->photo,
                            c->photo, false)) {
    community_full->photo = Photo();
    if (c->photo.small_file_id.is_valid()) {
      need_reload_community_full = true;
    }
  }
  auto photo = std::move(community_full->photo);
  community_full->photo = Photo();
  on_update_community_full_photo(community_full, community_id, std::move(photo));

  community_full->is_update_community_full_sent = true;
  update_community_full(community_full, community_id, "on_load_community_full_from_database", true);

  if (need_reload_community_full) {
    reload_community_full(community_id, Auto(), "on_load_community_full_from_database");
  }
}

void CommunityManager::load_community_full(CommunityId community_id, Promise<Unit> &&promise, const char *source) {
  auto community_full = get_community_full_force(community_id, true, source);
  if (community_full != nullptr) {
    return promise.set_value(Unit());
  }
  reload_community_full(community_id, std::move(promise), source);
}

void CommunityManager::reload_community_full(CommunityId community_id, Promise<Unit> &&promise, const char *source) {
  auto input_community = get_input_community(community_id);
  if (input_community == nullptr) {
    return promise.set_error(400, "Community not found");
  }

  LOG(INFO) << "Get full " << community_id << " from " << source;
  auto send_query = PromiseCreator::lambda([td = td_, community_id, input_community = std::move(input_community)](
                                               Result<Promise<Unit>> &&promise) mutable {
    if (promise.is_ok() && !G()->close_flag()) {
      td->create_handler<GetFullCommunityQuery>(promise.move_as_ok())->send(community_id, std::move(input_community));
    }
  });
  get_community_full_queries_.add_query(community_id.get(), std::move(send_query), std::move(promise));
}

void CommunityManager::on_get_community_full(telegram_api::object_ptr<telegram_api::communityFull> &&community) {
  CommunityId community_id(community->id_);
  auto c = get_community(community_id);
  if (c == nullptr) {
    LOG(ERROR) << "Can't find " << community_id;
    return;
  }

  CommunityFull *community_full = add_community_full(community_id);
  auto community_dialogs =
      transform(std::move(community->linked_peers_), [](auto &&linked_peer) { return CommunityDialog(linked_peer); });
  td::remove_if(community_dialogs, [td = td_](const CommunityDialog &dialog) {
    if (!dialog.is_valid()) {
      LOG(ERROR) << "Receive an invalid community chat";
      return true;
    }
    td->dialog_manager_->force_create_dialog(dialog.get_dialog_id(), "CommunityDialog", true);
    return false;
  });
  auto administrator_count = community->admins_count_;
  auto banned_count = community->kicked_count_;
  if (community_full->about != community->about_ || community_full->dialogs != community_dialogs ||
      community_full->administrator_count != administrator_count || community_full->banned_count != banned_count ||
      community_full->peer_link_requests_pending != community->peer_link_requests_pending_) {
    community_full->about = std::move(community->about_);
    community_full->dialogs = std::move(community_dialogs);
    community_full->administrator_count = administrator_count;
    community_full->banned_count = banned_count;
    community_full->peer_link_requests_pending = community->peer_link_requests_pending_;
    community_full->is_changed = true;
  }
  auto photo = get_photo(td_, std::move(community->chat_photo_), DialogId());
  on_update_community_photo(
      c, community_id,
      as_dialog_photo(td_->file_manager_.get(), community_id.get_fake_dialog_id(), c->access_hash, photo, false),
      false);
  on_update_community_full_photo(community_full, community_id, std::move(photo));

  if (c->is_changed) {
    LOG(ERROR) << "Receive inconsistent chatPhoto and chatPhotoInfo for " << community_id;
    update_community(c, community_id);
  }

  community_full->is_update_community_full_sent = true;
  update_community_full(community_full, community_id, "on_get_community_full");
}

void CommunityManager::update_community_full(CommunityFull *community_full, CommunityId community_id,
                                             const char *source, bool from_database) {
  CHECK(community_full != nullptr);

  if (community_full->is_being_updated) {
    LOG(ERROR) << "Detected recursive update of full " << community_id << " from " << source;
  }
  community_full->is_being_updated = true;
  SCOPE_EXIT {
    community_full->is_being_updated = false;
  };

  unavailable_community_fulls_.erase(community_id);  // don't needed anymore

  community_full->need_save_to_database |= community_full->is_changed;
  if (community_full->is_changed) {
    if (!community_full->is_update_community_full_sent) {
      LOG(ERROR) << "Send partial updateCommunityFullInfo for " << community_id << " from " << source;
      community_full->is_update_community_full_sent = true;
    }
    send_closure(G()->td(), &Td::send_update,
                 get_update_community_full_info_object(community_id, community_full, source));
    community_full->is_changed = false;
  }
  if (community_full->need_save_to_database) {
    if (!from_database) {
      save_community_full(community_full, community_id);
    }
    community_full->need_save_to_database = false;
  }
}

void CommunityManager::on_update_community_full_photo(CommunityFull *community_full, CommunityId community_id,
                                                      Photo photo) {
  CHECK(community_full != nullptr);
  if (photo != community_full->photo) {
    community_full->photo = std::move(photo);
    community_full->is_changed = true;
  }

  auto photo_file_ids = photo_get_file_ids(community_full->photo);
  if (community_full->registered_photo_file_ids == photo_file_ids) {
    return;
  }

  auto &file_source_id = community_full->file_source_id;
  if (!file_source_id.is_valid()) {
    file_source_id = community_full_file_source_ids_.get(community_id);
    if (file_source_id.is_valid()) {
      VLOG(file_references) << "Move " << file_source_id << " inside of " << community_id;
      community_full_file_source_ids_.erase(community_id);
    } else {
      VLOG(file_references) << "Need to create new file source for full " << community_id;
      file_source_id = td_->file_reference_manager_->create_community_full_file_source(community_id);
    }
  }

  td_->file_manager_->change_files_source(file_source_id, community_full->registered_photo_file_ids, photo_file_ids,
                                          "on_update_community_full_photo");
  community_full->registered_photo_file_ids = std::move(photo_file_ids);
}

void CommunityManager::create_community(const string &name, DialogId dialog_id, bool is_hidden,
                                        Promise<td_api::object_ptr<td_api::communityId>> &&promise) {
  TRY_STATUS_PROMISE(
      promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "create_community"));
  td_->create_handler<CreateCommunityQuery>(std::move(promise))->send(name, dialog_id, is_hidden);
}

void CommunityManager::finish_create_community(CommunityId community_id,
                                               Promise<td_api::object_ptr<td_api::communityId>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  promise.set_value(
      td_api::make_object<td_api::communityId>(get_community_id_object(community_id, "finish_create_community")));
}

void CommunityManager::set_community_name(CommunityId community_id, const string &name, Promise<Unit> &&promise) {
  auto *c = get_community(community_id);
  if (c == nullptr) {
    return promise.set_error(400, "Community not found");
  }
  auto status = get_community_status(c);
  if (!status.is_administrator() || !status.can_change_info_and_settings()) {
    return promise.set_error(400, "Have not enough rights");
  }
  auto title = clean_name(name, MAX_TITLE_LENGTH);
  if (title.empty()) {
    return promise.set_error(400, "Name must be non-empty");
  }
  td_->create_handler<EditCommunityTitleQuery>(std::move(promise))->send(community_id, title);
}

FileSourceId CommunityManager::get_community_full_file_source_id(CommunityId community_id) {
  if (!community_id.is_valid()) {
    return FileSourceId();
  }

  auto community_full = get_community_full_const(community_id);
  if (community_full != nullptr) {
    VLOG(file_references) << "Don't need to create file source for full " << community_id;
    // community full was already added, source ID was registered and shouldn't be needed
    return community_full->is_update_community_full_sent ? FileSourceId() : community_full->file_source_id;
  }

  auto &source_id = community_full_file_source_ids_[community_id];
  if (!source_id.is_valid()) {
    source_id = td_->file_reference_manager_->create_community_full_file_source(community_id);
  }
  VLOG(file_references) << "Return " << source_id << " for full " << community_id;
  return source_id;
}

int64 CommunityManager::get_community_id_object(CommunityId community_id, const char *source) const {
  if (community_id.is_valid() && get_community(community_id) == nullptr &&
      unknown_communities_.count(community_id) == 0) {
    LOG(ERROR) << "Have no information about " << community_id << " received from " << source;
    unknown_communities_.insert(community_id);
    send_closure(G()->td(), &Td::send_update, get_update_unknown_community_object(community_id));
  }
  return community_id.get();
}

td_api::object_ptr<td_api::community> CommunityManager::get_community_object(CommunityId community_id) const {
  return get_community_object(community_id, get_community(community_id));
}

td_api::object_ptr<td_api::community> CommunityManager::get_community_object(CommunityId community_id,
                                                                             const Community *c) const {
  if (c == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::community>(community_id.get(), c->access_hash != 0, c->title,
                                                get_chat_photo_info_object(td_->file_manager_.get(), &c->photo),
                                                c->date, c->status.get_community_member_status_object(),
                                                c->default_permissions.get_community_permissions_object());
}

td_api::object_ptr<td_api::updateCommunity> CommunityManager::get_update_community_object(CommunityId community_id,
                                                                                          const Community *c) const {
  if (c == nullptr) {
    return get_update_unknown_community_object(community_id);
  }
  return td_api::make_object<td_api::updateCommunity>(get_community_object(community_id, c));
}

td_api::object_ptr<td_api::updateCommunity> CommunityManager::get_update_unknown_community_object(
    CommunityId community_id) const {
  return td_api::make_object<td_api::updateCommunity>(td_api::make_object<td_api::community>(
      community_id.get(), false, string(), nullptr, 0, td_api::make_object<td_api::communityMemberStatusBanned>(),
      RestrictedRights::restrict_all().get_community_permissions_object()));
}

td_api::object_ptr<td_api::communityFullInfo> CommunityManager::get_community_full_info_object(
    CommunityId community_id, const CommunityFull *community_full) const {
  CHECK(community_full != nullptr);
  auto chats = transform(community_full->dialogs,
                         [td = td_](const auto &dialog) { return dialog.get_community_chat_object(td); });
  return td_api::make_object<td_api::communityFullInfo>(
      get_chat_photo_object(td_->file_manager_.get(), community_full->photo), std::move(chats),
      community_full->administrator_count, community_full->banned_count, community_full->peer_link_requests_pending);
}

td_api::object_ptr<td_api::updateCommunityFullInfo> CommunityManager::get_update_community_full_info_object(
    CommunityId community_id, const CommunityFull *community_full, const char *source) const {
  return td_api::make_object<td_api::updateCommunityFullInfo>(
      get_community_id_object(community_id, source), get_community_full_info_object(community_id, community_full));
}

telegram_api::object_ptr<telegram_api::InputChannel> CommunityManager::get_input_community(
    CommunityId community_id) const {
  int64 access_hash = 0;
  const Community *c = get_community(community_id);
  if (c == nullptr) {
    if (!td_->auth_manager_->is_bot() || !community_id.is_valid()) {
      return nullptr;
    }
  } else {
    access_hash = c->access_hash;
  }
  return telegram_api::make_object<telegram_api::inputChannel>(community_id.get(), access_hash);
}

void CommunityManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  for (auto community_id : unknown_communities_) {
    if (!have_community(community_id)) {
      updates.push_back(get_update_unknown_community_object(community_id));
    }
  }

  communities_.foreach([&](const CommunityId &community_id, const unique_ptr<Community> &community) {
    updates.push_back(get_update_community_object(community_id, community.get()));
  });

  communities_full_.foreach([&](const CommunityId &community_id, const unique_ptr<CommunityFull> &community_full) {
    CHECK(community_full->is_update_community_full_sent);
    updates.push_back(get_update_community_full_info_object(community_id, community_full.get(), "get_current_state"));
  });
}

}  // namespace td
