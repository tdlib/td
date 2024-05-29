//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/Location.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class PeopleNearbyManager final : public Actor {
 public:
  PeopleNearbyManager(Td *td, ActorShared<> parent);

  void search_dialogs_nearby(const Location &location, Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise);

  void set_location(const Location &location, Promise<Unit> &&promise);

  static void set_location_visibility(Td *td);

  void get_is_location_visible(Promise<Unit> &&promise);

  int32 on_update_peer_located(vector<telegram_api::object_ptr<telegram_api::PeerLocated>> &&peers, bool from_update);

  bool is_user_nearby(UserId user_id) const;

 private:
  struct DialogNearby {
    DialogId dialog_id;
    int32 distance;

    DialogNearby(DialogId dialog_id, int32 distance) : dialog_id(dialog_id), distance(distance) {
    }

    bool operator<(const DialogNearby &other) const {
      return distance < other.distance || (distance == other.distance && dialog_id.get() < other.dialog_id.get());
    }

    bool operator==(const DialogNearby &other) const {
      return distance == other.distance && dialog_id == other.dialog_id;
    }

    bool operator!=(const DialogNearby &other) const {
      return !(*this == other);
    }
  };

  void start_up() final;

  void tear_down() final;

  static void on_user_nearby_timeout_callback(void *people_nearby_manager_ptr, int64 user_id_long);

  void on_user_nearby_timeout(UserId user_id);

  vector<td_api::object_ptr<td_api::chatNearby>> get_chats_nearby_object(
      const vector<DialogNearby> &dialogs_nearby) const;

  void send_update_users_nearby() const;

  void on_get_dialogs_nearby(Result<telegram_api::object_ptr<telegram_api::Updates>> result,
                             Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise);

  void try_send_set_location_visibility_query();

  void on_set_location_visibility_expire_date(int32 set_expire_date, int32 error_code);

  void set_location_visibility_expire_date(int32 expire_date);

  void on_get_is_location_visible(Result<telegram_api::object_ptr<telegram_api::Updates>> &&result,
                                  Promise<Unit> &&promise);

  void update_is_location_visible();

  Td *td_;
  ActorShared<> parent_;

  vector<DialogNearby> users_nearby_;
  vector<DialogNearby> channels_nearby_;
  FlatHashSet<UserId, UserIdHash> all_users_nearby_;
  MultiTimeout user_nearby_timeout_{"UserNearbyTimeout"};

  int32 location_visibility_expire_date_ = 0;
  int32 pending_location_visibility_expire_date_ = -1;
  bool is_set_location_visibility_request_sent_ = false;
  Location last_user_location_;
};

}  // namespace td
