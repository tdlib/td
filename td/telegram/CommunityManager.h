//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CommunityId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/DialogPhoto.h"
#include "td/telegram/QueryMerger.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/WaitFreeHashMap.h"

namespace td {

struct BinlogEvent;
class Td;

class CommunityManager final : public Actor {
 public:
  CommunityManager(Td *td, ActorShared<> parent);
  CommunityManager(const CommunityManager &) = delete;
  CommunityManager &operator=(const CommunityManager &) = delete;
  CommunityManager(CommunityManager &&) = delete;
  CommunityManager &operator=(CommunityManager &&) = delete;
  ~CommunityManager() final;

  bool have_community(CommunityId community_id) const;

  bool have_community_force(CommunityId community_id, const char *source);

  bool have_accessible_community(CommunityId community_id) const;

  void reload_community(CommunityId community_id, Promise<Unit> &&promise, const char *source);

  void on_get_community(telegram_api::community &community, const char *source);

  void on_get_community_forbidden(telegram_api::communityForbidden &community, const char *source);

  int64 get_community_id_object(CommunityId community_id, const char *source) const;

  td_api::object_ptr<td_api::community> get_community_object(CommunityId community_id) const;

  telegram_api::object_ptr<telegram_api::InputChannel> get_input_community(CommunityId community_id) const;

  void on_binlog_community_event(BinlogEvent &&event);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  struct Community {
    int64 access_hash = 0;
    string title;
    DialogPhoto photo;
    DialogParticipantStatus status = DialogParticipantStatus::Banned(0, string());
    RestrictedRights default_permissions = RestrictedRights::restrict_all();
    int32 date = 0;
    bool collapsed_in_dialogs = false;

    static constexpr uint32 CACHE_VERSION = 1;
    uint32 cache_version = 0;

    bool is_being_updated = false;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database
    bool is_update_community_sent = false;

    bool is_repaired = false;  // whether cached value is rechecked

    bool is_saved = false;        // is current community version being saved/is saved to the database
    bool is_being_saved = false;  // is current community being saved to the database

    bool is_received_from_server = false;  // true, if the community was received from the server and not the database

    uint64 log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  class CommunityLogEvent;

  void tear_down() final;

  void save_community(Community *c, CommunityId community_id, bool from_binlog);

  static string get_community_database_key(CommunityId community_id);

  static string get_community_database_value(const Community *c);

  void save_community_to_database(Community *c, CommunityId community_id);

  void save_community_to_database_impl(Community *c, CommunityId community_id, string value);

  void on_save_community_to_database(CommunityId community_id, bool success);

  void load_community_from_database(Community *c, CommunityId community_id, Promise<Unit> promise);

  void load_community_from_database_impl(CommunityId community_id, bool is_recursive, Promise<Unit> promise);

  void on_load_community_from_database(CommunityId community_id, string value, bool force, bool is_recursive);

  void update_community(Community *c, CommunityId community_id, bool from_binlog = false, bool from_database = false);

  const Community *get_community(CommunityId community_id) const;

  Community *get_community(CommunityId community_id);

  Community *get_community_force(CommunityId community_id, const char *source, bool is_recursive = false);

  Community *add_community(CommunityId community_id, const char *source);

  void on_update_community_photo(Community *c, CommunityId community_id,
                                 telegram_api::object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);

  void on_update_community_photo(Community *c, CommunityId community_id, DialogPhoto &&photo,
                                 bool invalidate_photo_cache);

  static void on_update_community_title(Community *c, CommunityId community_id, string &&title);

  void on_update_community_status(Community *c, CommunityId community_id, DialogParticipantStatus &&status);

  static void on_update_community_default_permissions(Community *c, CommunityId community_id,
                                                      RestrictedRights default_permissions);

  td_api::object_ptr<td_api::updateCommunity> get_update_community_object(CommunityId community_id,
                                                                          const Community *c) const;

  td_api::object_ptr<td_api::updateCommunity> get_update_unknown_community_object(CommunityId community_id) const;

  td_api::object_ptr<td_api::community> get_community_object(CommunityId Community_id, const Community *c) const;

  Td *td_;
  ActorShared<> parent_;

  WaitFreeHashMap<CommunityId, unique_ptr<Community>, CommunityIdHash> communities_;
  mutable FlatHashSet<CommunityId, CommunityIdHash> unknown_communities_;

  FlatHashMap<CommunityId, vector<Promise<Unit>>, CommunityIdHash> load_community_from_database_queries_;
  FlatHashSet<CommunityId, CommunityIdHash> loaded_from_database_communities_;

  QueryMerger get_community_queries_{"GetCommunityMerger", 10, 1};
};

}  // namespace td
