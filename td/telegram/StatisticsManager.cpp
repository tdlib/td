//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StatisticsManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesInfo.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

#include <limits>

namespace td {

static td_api::object_ptr<td_api::dateRange> convert_date_range(
    const telegram_api::object_ptr<telegram_api::statsDateRangeDays> &obj) {
  return td_api::make_object<td_api::dateRange>(obj->min_date_, obj->max_date_);
}

static td_api::object_ptr<td_api::StatisticalGraph> convert_stats_graph(
    telegram_api::object_ptr<telegram_api::StatsGraph> obj) {
  CHECK(obj != nullptr);

  switch (obj->get_id()) {
    case telegram_api::statsGraphAsync::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraphAsync>(obj);
      return td_api::make_object<td_api::statisticalGraphAsync>(std::move(graph->token_));
    }
    case telegram_api::statsGraphError::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraphError>(obj);
      return td_api::make_object<td_api::statisticalGraphError>(std::move(graph->error_));
    }
    case telegram_api::statsGraph::ID: {
      auto graph = move_tl_object_as<telegram_api::statsGraph>(obj);
      return td_api::make_object<td_api::statisticalGraphData>(std::move(graph->json_->data_),
                                                               std::move(graph->zoom_token_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static double get_percentage_value(double part, double total, bool is_percentage) {
  if (total < 1e-6 && total > -1e-6) {
    if (part < 1e-6 && part > -1e-6) {
      return 0.0;
    }
    return 100.0;
  }
  if (part > 1e20) {
    return 100.0;
  }
  auto value = part / total * 100;
  if (is_percentage) {
    return clamp(value, 0.0, 100.0);
  } else {
    return max(value, -100.0);
  }
}

static td_api::object_ptr<td_api::statisticalValue> convert_stats_absolute_value(
    const telegram_api::object_ptr<telegram_api::statsAbsValueAndPrev> &obj) {
  return td_api::make_object<td_api::statisticalValue>(
      obj->current_, obj->previous_, get_percentage_value(obj->current_ - obj->previous_, obj->previous_, false));
}

static td_api::object_ptr<td_api::chatStatisticsSupergroup> convert_megagroup_stats(
    Td *td, telegram_api::object_ptr<telegram_api::stats_megagroupStats> obj) {
  CHECK(obj != nullptr);

  td->contacts_manager_->on_get_users(std::move(obj->users_), "convert_megagroup_stats");

  // just in case
  td::remove_if(obj->top_posters_, [](auto &obj) {
    return !UserId(obj->user_id_).is_valid() || obj->messages_ < 0 || obj->avg_chars_ < 0;
  });
  td::remove_if(obj->top_admins_, [](auto &obj) {
    return !UserId(obj->user_id_).is_valid() || obj->deleted_ < 0 || obj->kicked_ < 0 || obj->banned_ < 0;
  });
  td::remove_if(obj->top_inviters_,
                [](auto &obj) { return !UserId(obj->user_id_).is_valid() || obj->invitations_ < 0; });

  auto top_senders = transform(
      std::move(obj->top_posters_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopPoster> &&top_poster) {
        return td_api::make_object<td_api::chatStatisticsMessageSenderInfo>(
            td->contacts_manager_->get_user_id_object(UserId(top_poster->user_id_), "get_top_senders"),
            top_poster->messages_, top_poster->avg_chars_);
      });
  auto top_administrators = transform(
      std::move(obj->top_admins_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopAdmin> &&top_admin) {
        return td_api::make_object<td_api::chatStatisticsAdministratorActionsInfo>(
            td->contacts_manager_->get_user_id_object(UserId(top_admin->user_id_), "get_top_administrators"),
            top_admin->deleted_, top_admin->kicked_, top_admin->banned_);
      });
  auto top_inviters = transform(
      std::move(obj->top_inviters_), [td](telegram_api::object_ptr<telegram_api::statsGroupTopInviter> &&top_inviter) {
        return td_api::make_object<td_api::chatStatisticsInviterInfo>(
            td->contacts_manager_->get_user_id_object(UserId(top_inviter->user_id_), "get_top_inviters"),
            top_inviter->invitations_);
      });

  return td_api::make_object<td_api::chatStatisticsSupergroup>(
      convert_date_range(obj->period_), convert_stats_absolute_value(obj->members_),
      convert_stats_absolute_value(obj->messages_), convert_stats_absolute_value(obj->viewers_),
      convert_stats_absolute_value(obj->posters_), convert_stats_graph(std::move(obj->growth_graph_)),
      convert_stats_graph(std::move(obj->members_graph_)),
      convert_stats_graph(std::move(obj->new_members_by_source_graph_)),
      convert_stats_graph(std::move(obj->languages_graph_)), convert_stats_graph(std::move(obj->messages_graph_)),
      convert_stats_graph(std::move(obj->actions_graph_)), convert_stats_graph(std::move(obj->top_hours_graph_)),
      convert_stats_graph(std::move(obj->weekdays_graph_)), std::move(top_senders), std::move(top_administrators),
      std::move(top_inviters));
}

static td_api::object_ptr<td_api::chatStatisticsChannel> convert_broadcast_stats(
    telegram_api::object_ptr<telegram_api::stats_broadcastStats> obj) {
  CHECK(obj != nullptr);

  auto recent_message_interactions = transform(std::move(obj->recent_message_interactions_), [](auto &&interaction) {
    return td_api::make_object<td_api::chatStatisticsMessageInteractionInfo>(
        MessageId(ServerMessageId(interaction->msg_id_)).get(), interaction->views_, interaction->forwards_);
  });

  return td_api::make_object<td_api::chatStatisticsChannel>(
      convert_date_range(obj->period_), convert_stats_absolute_value(obj->followers_),
      convert_stats_absolute_value(obj->views_per_post_), convert_stats_absolute_value(obj->shares_per_post_),
      get_percentage_value(obj->enabled_notifications_->part_, obj->enabled_notifications_->total_, true),
      convert_stats_graph(std::move(obj->growth_graph_)), convert_stats_graph(std::move(obj->followers_graph_)),
      convert_stats_graph(std::move(obj->mute_graph_)), convert_stats_graph(std::move(obj->top_hours_graph_)),
      convert_stats_graph(std::move(obj->views_by_source_graph_)),
      convert_stats_graph(std::move(obj->new_followers_by_source_graph_)),
      convert_stats_graph(std::move(obj->languages_graph_)), convert_stats_graph(std::move(obj->interactions_graph_)),
      convert_stats_graph(std::move(obj->iv_interactions_graph_)), std::move(recent_message_interactions));
}

class GetMegagroupStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::ChatStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetMegagroupStatsQuery(Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = 0;
    if (is_dark) {
      flags |= telegram_api::stats_getMegagroupStats::DARK_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stats_getMegagroupStats(flags, false /*ignored*/, std::move(input_channel)), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMegagroupStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(convert_megagroup_stats(td_, result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetMegagroupStatsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetBroadcastStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::ChatStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetBroadcastStatsQuery(Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = 0;
    if (is_dark) {
      flags |= telegram_api::stats_getBroadcastStats::DARK_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stats_getBroadcastStats(flags, false /*ignored*/, std::move(input_channel)), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getBroadcastStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = convert_broadcast_stats(result_ptr.move_as_ok());
    for (auto &info : result->recent_message_interactions_) {
      td_->messages_manager_->on_update_message_interaction_info({DialogId(channel_id_), MessageId(info->message_id_)},
                                                                 info->view_count_, info->forward_count_, false,
                                                                 nullptr);
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetBroadcastStatsQuery");
    promise_.set_error(std::move(status));
  }
};

static td_api::object_ptr<td_api::messageStatistics> convert_message_stats(
    telegram_api::object_ptr<telegram_api::stats_messageStats> obj) {
  return td_api::make_object<td_api::messageStatistics>(convert_stats_graph(std::move(obj->views_graph_)));
}

class GetMessageStatsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::messageStatistics>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetMessageStatsQuery(Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId message_id, bool is_dark, DcId dc_id) {
    channel_id_ = channel_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Supergroup not found"));
    }

    int32 flags = 0;
    if (is_dark) {
      flags |= telegram_api::stats_getMessageStats::DARK_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stats_getMessageStats(flags, false /*ignored*/, std::move(input_channel),
                                            message_id.get_server_message_id().get()),
        {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMessageStats>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(convert_message_stats(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetMessageStatsQuery");
    promise_.set_error(std::move(status));
  }
};

class LoadAsyncGraphQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::StatisticalGraph>> promise_;

 public:
  explicit LoadAsyncGraphQuery(Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &token, int64 x, DcId dc_id) {
    int32 flags = 0;
    if (x != 0) {
      flags |= telegram_api::stats_loadAsyncGraph::X_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::stats_loadAsyncGraph(flags, token, x), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_loadAsyncGraph>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(convert_stats_graph(std::move(result)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetMessagePublicForwardsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundMessages>> promise_;
  DialogId dialog_id_;
  int32 limit_;

 public:
  explicit GetMessagePublicForwardsQuery(Promise<td_api::object_ptr<td_api::foundMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DcId dc_id, MessageFullId message_full_id, int32 offset_date, DialogId offset_dialog_id,
            ServerMessageId offset_message_id, int32 limit) {
    dialog_id_ = message_full_id.get_dialog_id();
    limit_ = limit;

    auto input_peer = MessagesManager::get_input_peer_force(offset_dialog_id);
    CHECK(input_peer != nullptr);

    auto input_channel = td_->contacts_manager_->get_input_channel(dialog_id_.get_channel_id());
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::stats_getMessagePublicForwards(
            std::move(input_channel), message_full_id.get_message_id().get_server_message_id().get(), offset_date,
            std::move(input_peer), offset_message_id.get(), limit),
        {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stats_getMessagePublicForwards>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, DialogId(), result_ptr.move_as_ok(), "GetMessagePublicForwardsQuery");
    td_->messages_manager_->get_channel_differences_if_needed(
        std::move(info), PromiseCreator::lambda([actor_id = td_->statistics_manager_actor_.get(),
                                                 promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &StatisticsManager::on_get_message_public_forwards, info.total_count,
                         std::move(info.messages), info.next_rate, std::move(promise));
          }
        }));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetMessagePublicForwardsQuery");
    promise_.set_error(std::move(status));
  }
};

StatisticsManager::StatisticsManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void StatisticsManager::tear_down() {
  parent_.reset();
}

void StatisticsManager::get_channel_statistics(DialogId dialog_id, bool is_dark,
                                               Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, is_dark, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
        if (r_dc_id.is_error()) {
          return promise.set_error(r_dc_id.move_as_error());
        }
        send_closure(actor_id, &StatisticsManager::send_get_channel_stats_query, r_dc_id.move_as_ok(),
                     dialog_id.get_channel_id(), is_dark, std::move(promise));
      });
  td_->contacts_manager_->get_channel_statistics_dc_id(dialog_id, true, std::move(dc_id_promise));
}

void StatisticsManager::send_get_channel_stats_query(DcId dc_id, ChannelId channel_id, bool is_dark,
                                                     Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  if (td_->contacts_manager_->is_megagroup_channel(channel_id)) {
    td_->create_handler<GetMegagroupStatsQuery>(std::move(promise))->send(channel_id, is_dark, dc_id);
  } else {
    td_->create_handler<GetBroadcastStatsQuery>(std::move(promise))->send(channel_id, is_dark, dc_id);
  }
}

void StatisticsManager::get_channel_message_statistics(
    MessageFullId message_full_id, bool is_dark, Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), message_full_id, is_dark,
                                               promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_get_channel_message_stats_query, r_dc_id.move_as_ok(),
                 message_full_id, is_dark, std::move(promise));
  });
  td_->contacts_manager_->get_channel_statistics_dc_id(message_full_id.get_dialog_id(), false,
                                                       std::move(dc_id_promise));
}

void StatisticsManager::send_get_channel_message_stats_query(
    DcId dc_id, MessageFullId message_full_id, bool is_dark,
    Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto dialog_id = message_full_id.get_dialog_id();
  if (!td_->messages_manager_->have_message_force(message_full_id, "send_get_channel_message_stats_query")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }
  if (!td_->messages_manager_->can_get_message_statistics(message_full_id)) {
    return promise.set_error(Status::Error(400, "Message statistics is inaccessible"));
  }
  CHECK(dialog_id.get_type() == DialogType::Channel);
  td_->create_handler<GetMessageStatsQuery>(std::move(promise))
      ->send(dialog_id.get_channel_id(), message_full_id.get_message_id(), is_dark, dc_id);
}

void StatisticsManager::load_statistics_graph(DialogId dialog_id, string token, int64 x,
                                              Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise) {
  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), token = std::move(token), x,
                                               promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_load_async_graph_query, r_dc_id.move_as_ok(), std::move(token), x,
                 std::move(promise));
  });
  td_->contacts_manager_->get_channel_statistics_dc_id(dialog_id, false, std::move(dc_id_promise));
}

void StatisticsManager::send_load_async_graph_query(DcId dc_id, string token, int64 x,
                                                    Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  td_->create_handler<LoadAsyncGraphQuery>(std::move(promise))->send(token, x, dc_id);
}

void StatisticsManager::get_message_public_forwards(MessageFullId message_full_id, string offset, int32 limit,
                                                    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  auto dc_id_promise = PromiseCreator::lambda([actor_id = actor_id(this), message_full_id, offset = std::move(offset),
                                               limit, promise = std::move(promise)](Result<DcId> r_dc_id) mutable {
    if (r_dc_id.is_error()) {
      return promise.set_error(r_dc_id.move_as_error());
    }
    send_closure(actor_id, &StatisticsManager::send_get_message_public_forwards_query, r_dc_id.move_as_ok(),
                 message_full_id, std::move(offset), limit, std::move(promise));
  });
  td_->contacts_manager_->get_channel_statistics_dc_id(message_full_id.get_dialog_id(), false,
                                                       std::move(dc_id_promise));
}

void StatisticsManager::send_get_message_public_forwards_query(
    DcId dc_id, MessageFullId message_full_id, string offset, int32 limit,
    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  if (!td_->messages_manager_->have_message_force(message_full_id, "send_get_message_public_forwards_query")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }
  if (!td_->messages_manager_->can_get_message_statistics(message_full_id)) {
    return promise.set_error(Status::Error(400, "Message forwards are inaccessible"));
  }

  static constexpr int32 MAX_MESSAGE_FORWARDS = 100;  // server side limit
  if (limit > MAX_MESSAGE_FORWARDS) {
    limit = MAX_MESSAGE_FORWARDS;
  }

  int32 offset_date = std::numeric_limits<int32>::max();
  DialogId offset_dialog_id;
  ServerMessageId offset_server_message_id;

  if (!offset.empty()) {
    auto parts = full_split(offset, ',');
    if (parts.size() != 3) {
      return promise.set_error(Status::Error(400, "Invalid offset specified"));
    }
    auto r_offset_date = to_integer_safe<int32>(parts[0]);
    auto r_offset_dialog_id = to_integer_safe<int64>(parts[1]);
    auto r_offset_server_message_id = to_integer_safe<int32>(parts[2]);
    if (r_offset_date.is_error() || r_offset_dialog_id.is_error() || r_offset_server_message_id.is_error()) {
      return promise.set_error(Status::Error(400, "Invalid offset specified"));
    }

    offset_date = r_offset_date.ok();
    offset_dialog_id = DialogId(r_offset_dialog_id.ok());
    offset_server_message_id = ServerMessageId(r_offset_server_message_id.ok());
  }

  td_->create_handler<GetMessagePublicForwardsQuery>(std::move(promise))
      ->send(dc_id, message_full_id, offset_date, offset_dialog_id, offset_server_message_id, limit);
}

void StatisticsManager::on_get_message_public_forwards(
    int32 total_count, vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, int32 next_rate,
    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Receive " << messages.size() << " forwarded messages";
  vector<td_api::object_ptr<td_api::message>> result;
  int32 last_message_date = 0;
  MessageId last_message_id;
  DialogId last_dialog_id;
  for (auto &message : messages) {
    auto message_date = MessagesManager::get_message_date(message);
    auto message_id = MessageId::get_message_id(message, false);
    auto dialog_id = DialogId::get_message_dialog_id(message);
    if (message_date > 0 && message_id.is_valid() && dialog_id.is_valid()) {
      last_message_date = message_date;
      last_message_id = message_id;
      last_dialog_id = dialog_id;
    }

    auto new_message_full_id =
        td_->messages_manager_->on_get_message(std::move(message), false, dialog_id.get_type() == DialogType::Channel,
                                               false, "on_get_message_public_forwards");
    if (new_message_full_id != MessageFullId()) {
      CHECK(dialog_id == new_message_full_id.get_dialog_id());
      result.push_back(
          td_->messages_manager_->get_message_object(new_message_full_id, "on_get_message_public_forwards"));
      CHECK(result.back() != nullptr);
    } else {
      total_count--;
    }
  }
  if (total_count < static_cast<int32>(result.size())) {
    LOG(ERROR) << "Receive " << result.size() << " valid messages out of " << total_count << " in " << messages.size()
               << " messages";
    total_count = static_cast<int32>(result.size());
  }
  string next_offset;
  if (!result.empty()) {
    if (next_rate > 0) {
      last_message_date = next_rate;
    }
    next_offset = PSTRING() << last_message_date << ',' << last_dialog_id.get() << ','
                            << last_message_id.get_server_message_id().get();
  }

  promise.set_value(td_api::make_object<td_api::foundMessages>(total_count, std::move(result), next_offset));
}

}  // namespace td
