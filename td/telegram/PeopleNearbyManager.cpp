//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PeopleNearbyManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <algorithm>
#include <limits>

namespace td {

class SearchDialogsNearbyQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit SearchDialogsNearbyQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const Location &location, bool from_background, int32 expire_date) {
    int32 flags = 0;
    if (from_background) {
      flags |= telegram_api::contacts_getLocated::BACKGROUND_MASK;
    }
    if (expire_date != -1) {
      flags |= telegram_api::contacts_getLocated::SELF_EXPIRES_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::contacts_getLocated(flags, false /*ignored*/, location.get_input_geo_point(), expire_date)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::contacts_getLocated>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

PeopleNearbyManager::PeopleNearbyManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  if (!td_->auth_manager_->is_bot()) {
    location_visibility_expire_date_ =
        to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("location_visibility_expire_date"));
    if (location_visibility_expire_date_ != 0 && location_visibility_expire_date_ <= G()->unix_time()) {
      location_visibility_expire_date_ = 0;
      G()->td_db()->get_binlog_pmc()->erase("location_visibility_expire_date");
    }
    auto pending_location_visibility_expire_date_string =
        G()->td_db()->get_binlog_pmc()->get("pending_location_visibility_expire_date");
    if (!pending_location_visibility_expire_date_string.empty()) {
      pending_location_visibility_expire_date_ = to_integer<int32>(pending_location_visibility_expire_date_string);
    }
    update_is_location_visible();
    LOG(INFO) << "Loaded location_visibility_expire_date = " << location_visibility_expire_date_
              << " and pending_location_visibility_expire_date = " << pending_location_visibility_expire_date_;
  }

  user_nearby_timeout_.set_callback(on_user_nearby_timeout_callback);
  user_nearby_timeout_.set_callback_data(static_cast<void *>(this));
}

void PeopleNearbyManager::tear_down() {
  parent_.reset();
}

void PeopleNearbyManager::start_up() {
  if (!pending_location_visibility_expire_date_) {
    try_send_set_location_visibility_query();
  }
}

void PeopleNearbyManager::on_user_nearby_timeout_callback(void *people_nearby_manager_ptr, int64 user_id_long) {
  if (G()->close_flag()) {
    return;
  }

  auto people_nearby_manager = static_cast<PeopleNearbyManager *>(people_nearby_manager_ptr);
  send_closure_later(people_nearby_manager->actor_id(people_nearby_manager),
                     &PeopleNearbyManager::on_user_nearby_timeout, UserId(user_id_long));
}

void PeopleNearbyManager::on_user_nearby_timeout(UserId user_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Remove " << user_id << " from nearby list";
  DialogId dialog_id(user_id);
  for (size_t i = 0; i < users_nearby_.size(); i++) {
    if (users_nearby_[i].dialog_id == dialog_id) {
      users_nearby_.erase(users_nearby_.begin() + i);
      send_update_users_nearby();
      return;
    }
  }
}

bool PeopleNearbyManager::is_user_nearby(UserId user_id) const {
  return all_users_nearby_.count(user_id) != 0;
}

void PeopleNearbyManager::search_dialogs_nearby(const Location &location,
                                                Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise) {
  if (location.empty()) {
    return promise.set_error(Status::Error(400, "Invalid location specified"));
  }
  last_user_location_ = location;
  try_send_set_location_visibility_query();

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::Updates>> result) mutable {
        send_closure(actor_id, &PeopleNearbyManager::on_get_dialogs_nearby, std::move(result), std::move(promise));
      });
  td_->create_handler<SearchDialogsNearbyQuery>(std::move(query_promise))->send(location, false, -1);
}

vector<td_api::object_ptr<td_api::chatNearby>> PeopleNearbyManager::get_chats_nearby_object(
    const vector<DialogNearby> &dialogs_nearby) const {
  return transform(dialogs_nearby, [td = td_](const DialogNearby &dialog_nearby) {
    return td_api::make_object<td_api::chatNearby>(
        td->dialog_manager_->get_chat_id_object(dialog_nearby.dialog_id, "chatNearby"), dialog_nearby.distance);
  });
}

void PeopleNearbyManager::send_update_users_nearby() const {
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateUsersNearby>(get_chats_nearby_object(users_nearby_)));
}

void PeopleNearbyManager::on_get_dialogs_nearby(Result<telegram_api::object_ptr<telegram_api::Updates>> result,
                                                Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }

  auto updates_ptr = result.move_as_ok();
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << oneline(to_string(*updates_ptr)) << " instead of updates";
    return promise.set_error(Status::Error(500, "Receive unsupported response from the server"));
  }

  auto update = telegram_api::move_object_as<telegram_api::updates>(updates_ptr);
  LOG(INFO) << "Receive chats nearby in " << to_string(update);

  td_->user_manager_->on_get_users(std::move(update->users_), "on_get_dialogs_nearby");
  td_->chat_manager_->on_get_chats(std::move(update->chats_), "on_get_dialogs_nearby");

  for (auto &dialog_nearby : users_nearby_) {
    user_nearby_timeout_.cancel_timeout(dialog_nearby.dialog_id.get_user_id().get());
  }
  auto old_users_nearby = std::move(users_nearby_);
  users_nearby_.clear();
  channels_nearby_.clear();
  int32 location_visibility_expire_date = 0;
  for (auto &update_ptr : update->updates_) {
    if (update_ptr->get_id() != telegram_api::updatePeerLocated::ID) {
      LOG(ERROR) << "Receive unexpected " << to_string(update);
      continue;
    }

    auto expire_date = on_update_peer_located(
        std::move(static_cast<telegram_api::updatePeerLocated *>(update_ptr.get())->peers_), false);
    if (expire_date != -1) {
      location_visibility_expire_date = expire_date;
    }
  }
  if (location_visibility_expire_date != location_visibility_expire_date_) {
    set_location_visibility_expire_date(location_visibility_expire_date);
    update_is_location_visible();
  }

  std::sort(users_nearby_.begin(), users_nearby_.end());
  if (old_users_nearby != users_nearby_) {
    send_update_users_nearby();  // for other clients connected to the same TDLib instance
  }
  promise.set_value(td_api::make_object<td_api::chatsNearby>(get_chats_nearby_object(users_nearby_),
                                                             get_chats_nearby_object(channels_nearby_)));
}

void PeopleNearbyManager::set_location(const Location &location, Promise<Unit> &&promise) {
  if (location.empty()) {
    return promise.set_error(Status::Error(400, "Invalid location specified"));
  }
  last_user_location_ = location;
  try_send_set_location_visibility_query();

  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](Result<telegram_api::object_ptr<telegram_api::Updates>> result) mutable {
        promise.set_value(Unit());
      });
  td_->create_handler<SearchDialogsNearbyQuery>(std::move(query_promise))->send(location, true, -1);
}

void PeopleNearbyManager::set_location_visibility(Td *td) {
  bool is_location_visible = td->option_manager_->get_option_boolean("is_location_visible");
  auto pending_location_visibility_expire_date = is_location_visible ? std::numeric_limits<int32>::max() : 0;
  if (td->people_nearby_manager_ == nullptr) {
    G()->td_db()->get_binlog_pmc()->set("pending_location_visibility_expire_date",
                                        to_string(pending_location_visibility_expire_date));
    return;
  }
  if (td->people_nearby_manager_->pending_location_visibility_expire_date_ == -1 &&
      pending_location_visibility_expire_date == td->people_nearby_manager_->location_visibility_expire_date_) {
    return;
  }
  if (td->people_nearby_manager_->pending_location_visibility_expire_date_ != pending_location_visibility_expire_date) {
    td->people_nearby_manager_->pending_location_visibility_expire_date_ = pending_location_visibility_expire_date;
    G()->td_db()->get_binlog_pmc()->set("pending_location_visibility_expire_date",
                                        to_string(pending_location_visibility_expire_date));
  }
  td->people_nearby_manager_->try_send_set_location_visibility_query();
}

void PeopleNearbyManager::try_send_set_location_visibility_query() {
  if (G()->close_flag()) {
    return;
  }
  if (pending_location_visibility_expire_date_ == -1) {
    return;
  }

  LOG(INFO) << "Trying to send set location visibility query";
  if (is_set_location_visibility_request_sent_) {
    return;
  }
  if (pending_location_visibility_expire_date_ != 0 && last_user_location_.empty()) {
    return;
  }

  is_set_location_visibility_request_sent_ = true;
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), set_expire_date = pending_location_visibility_expire_date_](
                                 Result<telegram_api::object_ptr<telegram_api::Updates>> result) {
        send_closure(actor_id, &PeopleNearbyManager::on_set_location_visibility_expire_date, set_expire_date,
                     result.is_ok() ? 0 : result.error().code());
      });
  td_->create_handler<SearchDialogsNearbyQuery>(std::move(query_promise))
      ->send(last_user_location_, true, pending_location_visibility_expire_date_);
}

void PeopleNearbyManager::on_set_location_visibility_expire_date(int32 set_expire_date, int32 error_code) {
  bool success = error_code == 0;
  is_set_location_visibility_request_sent_ = false;

  if (set_expire_date != pending_location_visibility_expire_date_) {
    return try_send_set_location_visibility_query();
  }

  if (success) {
    set_location_visibility_expire_date(pending_location_visibility_expire_date_);
  } else {
    if (G()->close_flag()) {
      // request will be re-sent after restart
      return;
    }
    if (error_code != 406) {
      LOG(ERROR) << "Failed to set location visibility expire date to " << pending_location_visibility_expire_date_;
    }
  }
  G()->td_db()->get_binlog_pmc()->erase("pending_location_visibility_expire_date");
  pending_location_visibility_expire_date_ = -1;
  update_is_location_visible();
}

void PeopleNearbyManager::get_is_location_visible(Promise<Unit> &&promise) {
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::Updates>> result) mutable {
        send_closure(actor_id, &PeopleNearbyManager::on_get_is_location_visible, std::move(result), std::move(promise));
      });
  td_->create_handler<SearchDialogsNearbyQuery>(std::move(query_promise))->send(Location(), true, -1);
}

void PeopleNearbyManager::on_get_is_location_visible(Result<telegram_api::object_ptr<telegram_api::Updates>> &&result,
                                                     Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (result.is_error()) {
    if (result.error().message() == "GEO_POINT_INVALID" && pending_location_visibility_expire_date_ == -1 &&
        location_visibility_expire_date_ > 0) {
      set_location_visibility_expire_date(0);
      update_is_location_visible();
    }
    return promise.set_value(Unit());
  }

  auto updates_ptr = result.move_as_ok();
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << oneline(to_string(*updates_ptr)) << " instead of updates";
    return promise.set_value(Unit());
  }

  auto updates = std::move(telegram_api::move_object_as<telegram_api::updates>(updates_ptr)->updates_);
  if (updates.size() != 1 || updates[0]->get_id() != telegram_api::updatePeerLocated::ID) {
    LOG(ERROR) << "Receive unexpected " << to_string(updates);
    return promise.set_value(Unit());
  }

  auto peers = std::move(static_cast<telegram_api::updatePeerLocated *>(updates[0].get())->peers_);
  if (peers.size() != 1 || peers[0]->get_id() != telegram_api::peerSelfLocated::ID) {
    LOG(ERROR) << "Receive unexpected " << to_string(peers);
    return promise.set_value(Unit());
  }

  auto location_visibility_expire_date = static_cast<telegram_api::peerSelfLocated *>(peers[0].get())->expires_;
  if (location_visibility_expire_date != location_visibility_expire_date_) {
    set_location_visibility_expire_date(location_visibility_expire_date);
    update_is_location_visible();
  }

  promise.set_value(Unit());
}

int32 PeopleNearbyManager::on_update_peer_located(vector<telegram_api::object_ptr<telegram_api::PeerLocated>> &&peers,
                                                  bool from_update) {
  auto now = G()->unix_time();
  bool need_update = false;
  int32 location_visibility_expire_date = -1;
  for (auto &peer_located_ptr : peers) {
    if (peer_located_ptr->get_id() == telegram_api::peerSelfLocated::ID) {
      auto peer_self_located = telegram_api::move_object_as<telegram_api::peerSelfLocated>(peer_located_ptr);
      if (peer_self_located->expires_ == 0 || peer_self_located->expires_ > G()->unix_time()) {
        location_visibility_expire_date = peer_self_located->expires_;
      }
      continue;
    }

    CHECK(peer_located_ptr->get_id() == telegram_api::peerLocated::ID);
    auto peer_located = telegram_api::move_object_as<telegram_api::peerLocated>(peer_located_ptr);
    DialogId dialog_id(peer_located->peer_);
    int32 expires_at = peer_located->expires_;
    int32 distance = peer_located->distance_;
    if (distance < 0 || distance > 50000000) {
      LOG(ERROR) << "Receive wrong distance to " << to_string(peer_located);
      continue;
    }
    if (expires_at <= now) {
      LOG(INFO) << "Skip expired result " << to_string(peer_located);
      continue;
    }

    auto dialog_type = dialog_id.get_type();
    if (dialog_type == DialogType::User) {
      auto user_id = dialog_id.get_user_id();
      if (!td_->user_manager_->have_user(user_id)) {
        LOG(ERROR) << "Can't find " << user_id;
        continue;
      }
      if (expires_at < now + 86400) {
        user_nearby_timeout_.set_timeout_in(user_id.get(), expires_at - now + 1);
      }
    } else if (dialog_type == DialogType::Channel) {
      auto channel_id = dialog_id.get_channel_id();
      if (!td_->chat_manager_->have_channel(channel_id)) {
        LOG(ERROR) << "Can't find " << channel_id;
        continue;
      }
      if (expires_at != std::numeric_limits<int32>::max()) {
        LOG(ERROR) << "Receive expiring at " << expires_at << " group location in " << to_string(peer_located);
      }
      if (from_update) {
        LOG(ERROR) << "Receive nearby " << channel_id << " from update";
        continue;
      }
    } else {
      LOG(ERROR) << "Receive chat of wrong type in " << to_string(peer_located);
      continue;
    }

    td_->dialog_manager_->force_create_dialog(dialog_id, "on_update_peer_located");

    if (from_update) {
      CHECK(dialog_type == DialogType::User);
      bool is_found = false;
      for (auto &dialog_nearby : users_nearby_) {
        if (dialog_nearby.dialog_id == dialog_id) {
          if (dialog_nearby.distance != distance) {
            dialog_nearby.distance = distance;
            need_update = true;
          }
          is_found = true;
          break;
        }
      }
      if (!is_found) {
        users_nearby_.emplace_back(dialog_id, distance);
        all_users_nearby_.insert(dialog_id.get_user_id());
        need_update = true;
      }
    } else {
      if (dialog_type == DialogType::User) {
        users_nearby_.emplace_back(dialog_id, distance);
        all_users_nearby_.insert(dialog_id.get_user_id());
      } else {
        channels_nearby_.emplace_back(dialog_id, distance);
      }
    }
  }
  if (need_update) {
    std::sort(users_nearby_.begin(), users_nearby_.end());
    send_update_users_nearby();
  }
  return location_visibility_expire_date;
}

void PeopleNearbyManager::set_location_visibility_expire_date(int32 expire_date) {
  if (location_visibility_expire_date_ == expire_date) {
    return;
  }

  LOG(INFO) << "Set set_location_visibility_expire_date to " << expire_date;
  location_visibility_expire_date_ = expire_date;
  if (expire_date == 0) {
    G()->td_db()->get_binlog_pmc()->erase("location_visibility_expire_date");
  } else {
    G()->td_db()->get_binlog_pmc()->set("location_visibility_expire_date", to_string(expire_date));
  }
  // the caller must call update_is_location_visible() itself
}

void PeopleNearbyManager::update_is_location_visible() {
  auto expire_date = pending_location_visibility_expire_date_ != -1 ? pending_location_visibility_expire_date_
                                                                    : location_visibility_expire_date_;
  td_->option_manager_->set_option_boolean("is_location_visible", expire_date != 0);
}

}  // namespace td
