//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CommunityManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogPhoto.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
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
  bool has_photo = photo.small_file_id.is_valid();
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
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), communities_, unknown_communities_);
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

  load_community_from_database_impl(community_id, false, Auto());
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
  load_community_from_database_impl(community_id, false, std::move(promise));
}

void CommunityManager::load_community_from_database_impl(CommunityId community_id, bool is_recursive,
                                                         Promise<Unit> promise) {
  LOG(INFO) << "Load " << community_id << " from database";
  auto &load_community_queries = load_community_from_database_queries_[community_id];
  load_community_queries.push_back(std::move(promise));
  if (load_community_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(
        get_community_database_key(community_id), PromiseCreator::lambda([community_id, is_recursive](string value) {
          send_closure(G()->community_manager(), &CommunityManager::on_load_community_from_database, community_id,
                       std::move(value), false, is_recursive);
        }));
  }
}

void CommunityManager::on_load_community_from_database(CommunityId community_id, string value, bool force,
                                                       bool is_recursive) {
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

const CommunityManager::Community *CommunityManager::get_community(CommunityId community_id) const {
  return communities_.get_pointer(community_id);
}

CommunityManager::Community *CommunityManager::get_community(CommunityId community_id) {
  return communities_.get_pointer(community_id);
}

CommunityManager::Community *CommunityManager::add_community(CommunityId community_id, const char *source) {
  CHECK(community_id.is_valid());
  auto &community_ptr = communities_[community_id];
  if (community_ptr == nullptr) {
    community_ptr = make_unique<Community>();
  }
  return community_ptr.get();
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

CommunityManager::Community *CommunityManager::get_community_force(CommunityId community_id, const char *source,
                                                                   bool is_recursive) {
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
  on_load_community_from_database(community_id,
                                  G()->td_db()->get_sqlite_sync_pmc()->get(get_community_database_key(community_id)),
                                  true, is_recursive);
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
  Community *c = add_community(community_id, "on_get_community");
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
                            get_dialog_photo(td_->file_manager_.get(), DialogId(ChannelId(community_id.get())),
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
      /*
      auto community_full = get_community_full(community_id, true, "on_update_community_photo");  // must not load CommunityFull
      if (community_full != nullptr) {
        on_update_community_full_photo(community_full, community_id, Photo());
        if (c->photo.small_file_id.is_valid()) {
          if (community_full->expires_at > 0.0) {
            community_full->expires_at = 0.0;
            community_full->need_save_to_database = true;
          }
          reload_community_full(community_id, Auto(), "on_update_community_photo");
        }
        update_community_full(community_full, community_id, "on_update_community_photo");
      }
      */
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
}

}  // namespace td
