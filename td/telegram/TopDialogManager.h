//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/net/NetQuery.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Time.h"

#include <array>
#include <utility>

namespace td {
enum class TopDialogCategory : int32 { Correspondent, BotPM, BotInline, Group, Channel, Call, Size };

inline TopDialogCategory top_dialog_category_from_td_api(const td_api::TopChatCategory &category) {
  switch (category.get_id()) {
    case td_api::topChatCategoryUsers::ID:
      return TopDialogCategory::Correspondent;
    case td_api::topChatCategoryBots::ID:
      return TopDialogCategory::BotPM;
    case td_api::topChatCategoryInlineBots::ID:
      return TopDialogCategory::BotInline;
    case td_api::topChatCategoryGroups::ID:
      return TopDialogCategory::Group;
    case td_api::topChatCategoryChannels::ID:
      return TopDialogCategory::Channel;
    case td_api::topChatCategoryCalls::ID:
      return TopDialogCategory::Call;
    default:
      UNREACHABLE();
  }
}

class TopDialogManager : public NetQueryCallback {
 public:
  explicit TopDialogManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void on_dialog_used(TopDialogCategory category, DialogId dialog_id, int32 date);

  void remove_dialog(TopDialogCategory category, DialogId dialog_id, tl_object_ptr<telegram_api::InputPeer> input_peer);

  void get_top_dialogs(TopDialogCategory category, size_t limit, Promise<vector<DialogId>> promise);

  void update_rating_e_decay();

  void update_is_enabled(bool is_enabled);

 private:
  static constexpr size_t MAX_TOP_DIALOGS_LIMIT = 30;
  static constexpr int32 SERVER_SYNC_DELAY = 86400;      // seconds
  static constexpr int32 SERVER_SYNC_RESEND_DELAY = 60;  // seconds
  static constexpr int32 DB_SYNC_DELAY = 5;              // seconds
  ActorShared<> parent_;

  bool is_active_{false};
  bool is_enabled_{true};
  int32 rating_e_decay_ = 241920;

  bool have_toggle_top_peers_query_ = false;
  bool toggle_top_peers_query_is_enabled_ = false;
  bool have_pending_toggle_top_peers_query_ = false;
  bool pending_toggle_top_peers_query_ = false;
  bool was_first_sync_{false};
  enum class SyncState : int32 { None, Pending, Ok };
  SyncState db_sync_state_ = SyncState::None;
  Timestamp first_unsync_change_;
  SyncState server_sync_state_ = SyncState::None;
  Timestamp last_server_sync_;

  struct GetTopDialogsQuery {
    TopDialogCategory category;
    size_t limit;
    Promise<vector<DialogId>> promise;
  };
  std::vector<GetTopDialogsQuery> pending_get_top_dialogs_;

  struct TopDialog {
    DialogId dialog_id;
    double rating = 0;
    bool operator<(const TopDialog &other) const {
      return std::make_pair(-rating, dialog_id.get()) < std::make_pair(-other.rating, other.dialog_id.get());
    }
  };

  struct TopDialogs {
    bool is_dirty = false;
    double rating_timestamp = 0;
    std::vector<TopDialog> dialogs;
  };
  template <class T>
  friend void parse(TopDialog &top_dialog, T &parser);
  template <class T>
  friend void store(const TopDialog &top_dialog, T &storer);
  template <class T>
  friend void parse(TopDialogs &top_dialogs, T &parser);
  template <class T>
  friend void store(const TopDialogs &top_dialogs, T &storer);

  std::array<TopDialogs, static_cast<size_t>(TopDialogCategory::Size)> by_category_;

  double rating_add(double now, double rating_timestamp) const;
  double current_rating_add(double rating_timestamp) const;
  void normalize_rating();

  bool set_is_enabled(bool is_enabled);
  void send_toggle_top_peers(bool is_enabled);

  void do_get_top_dialogs(GetTopDialogsQuery &&query);

  void do_get_top_peers();
  void do_save_top_dialogs();

  void on_first_sync();

  void on_result(NetQueryPtr net_query) override;

  void init();

  void start_up() override;
  void loop() override;
};
}  // namespace td
