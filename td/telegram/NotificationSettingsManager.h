//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/NotificationSettings.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class NotificationSettingsManager final : public Actor {
 public:
  NotificationSettingsManager(Td *td, ActorShared<> parent);
  NotificationSettingsManager(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager &operator=(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager(NotificationSettingsManager &&) = delete;
  NotificationSettingsManager &operator=(NotificationSettingsManager &&) = delete;
  ~NotificationSettingsManager() final;

  void send_get_dialog_notification_settings_query(DialogId dialog_id, Promise<Unit> &&promise);

  void send_get_scope_notification_settings_query(NotificationSettingsScope scope, Promise<Unit> &&promise);

  void on_get_dialog_notification_settings_query_finished(DialogId dialog_id, Status &&status);

  void update_dialog_notify_settings(DialogId dialog_id, const DialogNotificationSettings &new_settings,
                                     Promise<Unit> &&promise);

  void update_scope_notify_settings(NotificationSettingsScope scope, const ScopeNotificationSettings &new_settings,
                                    Promise<Unit> &&promise);

  void reset_notify_settings(Promise<Unit> &&promise);

  void get_notify_settings_exceptions(NotificationSettingsScope scope, bool filter_scope, bool compare_sound,
                                      Promise<Unit> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;

  FlatHashMap<DialogId, vector<Promise<Unit>>, DialogIdHash> get_dialog_notification_settings_queries_;
};

}  // namespace td
