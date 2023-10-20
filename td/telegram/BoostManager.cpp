//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BoostManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/SliceBuilder.h"

namespace td {

class GetBoostsStatusQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::chatBoostStatus>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetBoostsStatusQuery(Promise<td_api::object_ptr<td_api::chatBoostStatus>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);
    send_query(
        G()->net_query_creator().create(telegram_api::premium_getBoostsStatus(std::move(input_peer)), {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::premium_getBoostsStatus>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetBoostsStatusQuery: " << to_string(result);
    if (result->level_ < 0 || result->current_level_boosts_ < 0 || result->boosts_ < result->current_level_boosts_ ||
        (result->next_level_boosts_ != 0 && result->boosts_ >= result->next_level_boosts_)) {
      LOG(ERROR) << "Receive invalid " << to_string(result);
      if (result->level_ < 0) {
        result->level_ = 0;
      }
      if (result->current_level_boosts_ < 0) {
        result->current_level_boosts_ = 0;
      }
      if (result->boosts_ < result->current_level_boosts_) {
        result->boosts_ = result->current_level_boosts_;
      }
      if (result->next_level_boosts_ != 0 && result->boosts_ >= result->next_level_boosts_) {
        result->next_level_boosts_ = result->boosts_ + 1;
      }
    }
    int32 premium_member_count = 0;
    double premium_member_percentage = 0.0;
    if (result->premium_audience_ != nullptr) {
      premium_member_count = max(0, static_cast<int32>(result->premium_audience_->part_));
      auto participant_count = max(static_cast<int32>(result->premium_audience_->total_), premium_member_count);
      if (dialog_id_.get_type() == DialogType::Channel) {
        td_->contacts_manager_->on_update_channel_participant_count(dialog_id_.get_channel_id(), participant_count);
      }
      if (participant_count > 0) {
        premium_member_percentage = 100.0 * premium_member_count / participant_count;
      }
    }
    auto giveaways = transform(std::move(result->prepaid_giveaways_),
                               [](telegram_api::object_ptr<telegram_api::prepaidGiveaway> giveaway) {
                                 return td_api::make_object<td_api::prepaidPremiumGiveaway>(
                                     giveaway->id_, giveaway->quantity_, giveaway->months_, giveaway->date_);
                               });
    promise_.set_value(td_api::make_object<td_api::chatBoostStatus>(
        result->boost_url_, result->my_boost_, result->level_, result->boosts_, result->current_level_boosts_,
        result->next_level_boosts_, premium_member_count, premium_member_percentage, std::move(giveaways)));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetBoostsStatusQuery");
    promise_.set_error(std::move(status));
  }
};

class ApplyBoostQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit ApplyBoostQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::premium_applyBoost(0, vector<int>(), std::move(input_peer)), {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::premium_applyBoost>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "ApplyBoostQuery");
    promise_.set_error(std::move(status));
  }
};

class GetBoostsListQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundChatBoosts>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetBoostsListQuery(Promise<td_api::object_ptr<td_api::foundChatBoosts>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, bool only_gift_codes, const string &offset, int32 limit) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);
    int32 flags = 0;
    if (only_gift_codes) {
      flags |= telegram_api::premium_getBoostsList::GIFTS_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::premium_getBoostsList(0, false /*ignored*/, std::move(input_peer), offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::premium_getBoostsList>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetBoostsListQuery: " << to_string(result);
    td_->contacts_manager_->on_get_users(std::move(result->users_), "GetBoostsListQuery");

    auto total_count = result->count_;
    vector<td_api::object_ptr<td_api::chatBoost>> boosts;
    for (auto &boost : result->boosts_) {
      auto source = [&]() -> td_api::object_ptr<td_api::ChatBoostSource> {
        if (boost->giveaway_) {
          UserId user_id(boost->user_id_);
          if (!user_id.is_valid()) {
            user_id = UserId();
          }
          auto giveaway_message_id = MessageId(ServerMessageId(boost->giveaway_msg_id_));
          if (!giveaway_message_id.is_valid()) {
            giveaway_message_id = MessageId::min();
          }
          return td_api::make_object<td_api::chatBoostSourceGiveaway>(
              td_->contacts_manager_->get_user_id_object(user_id, "chatBoostSourceGiveaway"), boost->used_gift_slug_,
              giveaway_message_id.get(), boost->unclaimed_);
        }
        if (boost->gift_) {
          UserId user_id(boost->user_id_);
          if (!user_id.is_valid()) {
            return nullptr;
          }
          return td_api::make_object<td_api::chatBoostSourceGiftCode>(
              td_->contacts_manager_->get_user_id_object(user_id, "chatBoostSourceGiftCode"), boost->used_gift_slug_);
        }

        UserId user_id(boost->user_id_);
        if (!user_id.is_valid()) {
          return nullptr;
        }
        return td_api::make_object<td_api::chatBoostSourcePremium>(
            td_->contacts_manager_->get_user_id_object(user_id, "chatBoostSourcePremium"));
      }();
      if (source == nullptr) {
        LOG(ERROR) << "Receive " << to_string(boost);
        continue;
      }
      auto expiration_date = boost->expires_;
      if (expiration_date <= G()->unix_time()) {
        continue;
      }
      boosts.push_back(td_api::make_object<td_api::chatBoost>(max(boost->multiplier_, 1), std::move(source),
                                                              boost->date_, expiration_date));
    }
    promise_.set_value(
        td_api::make_object<td_api::foundChatBoosts>(total_count, std::move(boosts), result->next_offset_));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetBoostsListQuery");
    promise_.set_error(std::move(status));
  }
};

BoostManager::BoostManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void BoostManager::tear_down() {
  parent_.reset();
}

void BoostManager::get_dialog_boost_status(DialogId dialog_id,
                                           Promise<td_api::object_ptr<td_api::chatBoostStatus>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_boost_status")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }

  td_->create_handler<GetBoostsStatusQuery>(std::move(promise))->send(dialog_id);
}

void BoostManager::boost_dialog(DialogId dialog_id, Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_boost_status")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }

  td_->create_handler<ApplyBoostQuery>(std::move(promise))->send(dialog_id);
}

Result<std::pair<string, bool>> BoostManager::get_dialog_boost_link(DialogId dialog_id) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_boost_status")) {
    return Status::Error(400, "Chat not found");
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return Status::Error(400, "Can't access the chat");
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      !td_->contacts_manager_->is_broadcast_channel(dialog_id.get_channel_id())) {
    return Status::Error(400, "Can't boost the chat");
  }

  SliceBuilder sb;
  sb << LinkManager::get_t_me_url();

  auto username = td_->contacts_manager_->get_channel_first_username(dialog_id.get_channel_id());
  bool is_public = !username.empty();
  if (is_public) {
    sb << username;
  } else {
    sb << "c/" << dialog_id.get_channel_id().get();
  }
  sb << "?boost";

  return std::make_pair(sb.as_cslice().str(), is_public);
}

void BoostManager::get_dialog_boost_link_info(Slice url, Promise<DialogBoostLinkInfo> &&promise) {
  auto r_dialog_boost_link_info = LinkManager::get_dialog_boost_link_info(url);
  if (r_dialog_boost_link_info.is_error()) {
    return promise.set_error(Status::Error(400, r_dialog_boost_link_info.error().message()));
  }

  auto info = r_dialog_boost_link_info.move_as_ok();
  auto query_promise = PromiseCreator::lambda(
      [info, promise = std::move(promise)](Result<DialogId> &&result) mutable { promise.set_value(std::move(info)); });
  td_->messages_manager_->resolve_dialog(info.username, info.channel_id, std::move(query_promise));
}

td_api::object_ptr<td_api::chatBoostLinkInfo> BoostManager::get_chat_boost_link_info_object(
    const DialogBoostLinkInfo &info) const {
  CHECK(info.username.empty() == info.channel_id.is_valid());

  bool is_public = !info.username.empty();
  DialogId dialog_id =
      is_public ? td_->messages_manager_->resolve_dialog_username(info.username) : DialogId(info.channel_id);
  return td_api::make_object<td_api::chatBoostLinkInfo>(
      is_public, td_->messages_manager_->get_chat_id_object(dialog_id, "chatBoostLinkInfo"));
}

void BoostManager::get_dialog_boosts(DialogId dialog_id, bool only_gift_codes, const string &offset, int32 limit,
                                     Promise<td_api::object_ptr<td_api::foundChatBoosts>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_boost_status")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  td_->create_handler<GetBoostsListQuery>(std::move(promise))->send(dialog_id, only_gift_codes, offset, limit);
}

}  // namespace td
