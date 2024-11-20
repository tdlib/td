//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/SuggestedAction.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"

#include <map>

namespace td {

class Td;

class SuggestedActionManager final : public Actor {
 public:
  SuggestedActionManager(Td *td, ActorShared<> parent);

  void update_suggested_actions(vector<SuggestedAction> &&suggested_actions);

  void hide_suggested_action(SuggestedAction suggested_action);

  void dismiss_suggested_action(SuggestedAction suggested_action, Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void start_up() final;

  void hangup() final;

  void tear_down() final;

  static string get_suggested_actions_database_key();

  void save_suggested_actions();

  void on_dismiss_suggested_action(SuggestedAction suggested_action, Result<Unit> &&result);

  Td *td_;
  ActorShared<> parent_;

  vector<SuggestedAction> suggested_actions_;
  size_t dismiss_suggested_action_request_count_ = 0;
  std::map<int32, vector<Promise<Unit>>> dismiss_suggested_action_queries_;
};

}  // namespace td
