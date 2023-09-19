//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class StatisticsManager final : public Actor {
 public:
  StatisticsManager(Td *td, ActorShared<> parent);

  void get_channel_statistics(DialogId dialog_id, bool is_dark,
                              Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise);

  void get_channel_message_statistics(FullMessageId full_message_id, bool is_dark,
                                      Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void load_statistics_graph(DialogId dialog_id, string token, int64 x,
                             Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

  static td_api::object_ptr<td_api::dateRange> convert_date_range(
      const telegram_api::object_ptr<telegram_api::statsDateRangeDays> &obj);

  static td_api::object_ptr<td_api::StatisticalGraph> convert_stats_graph(
      telegram_api::object_ptr<telegram_api::StatsGraph> obj);

  static double get_percentage_value(double part, double total);

  static td_api::object_ptr<td_api::statisticalValue> convert_stats_absolute_value(
      const telegram_api::object_ptr<telegram_api::statsAbsValueAndPrev> &obj);

  td_api::object_ptr<td_api::chatStatisticsSupergroup> convert_megagroup_stats(
      telegram_api::object_ptr<telegram_api::stats_megagroupStats> obj);

  static td_api::object_ptr<td_api::chatStatisticsChannel> convert_broadcast_stats(
      telegram_api::object_ptr<telegram_api::stats_broadcastStats> obj);

  static td_api::object_ptr<td_api::messageStatistics> convert_message_stats(
      telegram_api::object_ptr<telegram_api::stats_messageStats> obj);

 private:
  void tear_down() final;

  void send_get_channel_stats_query(DcId dc_id, ChannelId channel_id, bool is_dark,
                                    Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise);

  void send_get_channel_message_stats_query(DcId dc_id, FullMessageId full_message_id, bool is_dark,
                                            Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void send_load_async_graph_query(DcId dc_id, string token, int64 x,
                                   Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
