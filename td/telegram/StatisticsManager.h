//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/td_api.h"

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

  void get_channel_message_statistics(MessageFullId message_full_id, bool is_dark,
                                      Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void load_statistics_graph(DialogId dialog_id, string token, int64 x,
                             Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

 private:
  void tear_down() final;

  void send_get_channel_stats_query(DcId dc_id, ChannelId channel_id, bool is_dark,
                                    Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise);

  void send_get_channel_message_stats_query(DcId dc_id, MessageFullId message_full_id, bool is_dark,
                                            Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void send_load_async_graph_query(DcId dc_id, string token, int64 x,
                                   Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
