//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TopDialogManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

#include "td/telegram/telegram_api.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace td {

static CSlice top_dialog_category_name(TopDialogCategory category) {
  switch (category) {
    case TopDialogCategory::Correspondent:
      return CSlice("correspondent");
    case TopDialogCategory::BotPM:
      return CSlice("bot_pm");
    case TopDialogCategory::BotInline:
      return CSlice("bot_inline");
    case TopDialogCategory::Group:
      return CSlice("group");
    case TopDialogCategory::Channel:
      return CSlice("channel");
    case TopDialogCategory::Call:
      return CSlice("call");
    case TopDialogCategory::ForwardUsers:
      return CSlice("forward_users");
    case TopDialogCategory::ForwardChats:
      return CSlice("forward_chats");
    default:
      UNREACHABLE();
  }
}

static TopDialogCategory get_top_dialog_category(const telegram_api::TopPeerCategory &category) {
  switch (category.get_id()) {
    case telegram_api::topPeerCategoryCorrespondents::ID:
      return TopDialogCategory::Correspondent;
    case telegram_api::topPeerCategoryBotsPM::ID:
      return TopDialogCategory::BotPM;
    case telegram_api::topPeerCategoryBotsInline::ID:
      return TopDialogCategory::BotInline;
    case telegram_api::topPeerCategoryGroups::ID:
      return TopDialogCategory::Group;
    case telegram_api::topPeerCategoryChannels::ID:
      return TopDialogCategory::Channel;
    case telegram_api::topPeerCategoryPhoneCalls::ID:
      return TopDialogCategory::Call;
    case telegram_api::topPeerCategoryForwardUsers::ID:
      return TopDialogCategory::ForwardUsers;
    case telegram_api::topPeerCategoryForwardChats::ID:
      return TopDialogCategory::ForwardChats;
    default:
      UNREACHABLE();
  }
}

static tl_object_ptr<telegram_api::TopPeerCategory> get_top_peer_category(TopDialogCategory category) {
  switch (category) {
    case TopDialogCategory::Correspondent:
      return make_tl_object<telegram_api::topPeerCategoryCorrespondents>();
    case TopDialogCategory::BotPM:
      return make_tl_object<telegram_api::topPeerCategoryBotsPM>();
    case TopDialogCategory::BotInline:
      return make_tl_object<telegram_api::topPeerCategoryBotsInline>();
    case TopDialogCategory::Group:
      return make_tl_object<telegram_api::topPeerCategoryGroups>();
    case TopDialogCategory::Channel:
      return make_tl_object<telegram_api::topPeerCategoryChannels>();
    case TopDialogCategory::Call:
      return make_tl_object<telegram_api::topPeerCategoryPhoneCalls>();
    case TopDialogCategory::ForwardUsers:
      return make_tl_object<telegram_api::topPeerCategoryForwardUsers>();
    case TopDialogCategory::ForwardChats:
      return make_tl_object<telegram_api::topPeerCategoryForwardChats>();
    default:
      UNREACHABLE();
  }
}

void TopDialogManager::update_is_enabled(bool is_enabled) {
  auto auth_manager = G()->td().get_actor_unsafe()->auth_manager_.get();
  if (auth_manager == nullptr || !auth_manager->is_authorized() || auth_manager->is_bot()) {
    return;
  }

  if (set_is_enabled(is_enabled)) {
    G()->td_db()->get_binlog_pmc()->set("top_peers_enabled", is_enabled ? "1" : "0");
    send_toggle_top_peers(is_enabled);

    loop();
  }
}

bool TopDialogManager::set_is_enabled(bool is_enabled) {
  if (is_enabled_ == is_enabled) {
    return false;
  }

  LOG(DEBUG) << "Change top chats is_enabled to " << is_enabled;
  is_enabled_ = is_enabled;
  init();
  return true;
}

void TopDialogManager::send_toggle_top_peers(bool is_enabled) {
  if (have_toggle_top_peers_query_) {
    have_pending_toggle_top_peers_query_ = true;
    pending_toggle_top_peers_query_ = is_enabled;
    return;
  }

  LOG(DEBUG) << "Send toggle top peers query to " << is_enabled;
  have_toggle_top_peers_query_ = true;
  toggle_top_peers_query_is_enabled_ = is_enabled;
  auto net_query = G()->net_query_creator().create(telegram_api::contacts_toggleTopPeers(is_enabled));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this, 2));
}

void TopDialogManager::on_dialog_used(TopDialogCategory category, DialogId dialog_id, int32 date) {
  if (!is_active_ || !is_enabled_) {
    return;
  }
  auto pos = static_cast<size_t>(category);
  CHECK(pos < by_category_.size());
  auto &top_dialogs = by_category_[pos];

  top_dialogs.is_dirty = true;
  auto it = std::find_if(top_dialogs.dialogs.begin(), top_dialogs.dialogs.end(),
                         [&](auto &top_dialog) { return top_dialog.dialog_id == dialog_id; });
  if (it == top_dialogs.dialogs.end()) {
    TopDialog top_dialog;
    top_dialog.dialog_id = dialog_id;
    top_dialogs.dialogs.push_back(top_dialog);
    it = top_dialogs.dialogs.end() - 1;
  }

  auto delta = rating_add(date, top_dialogs.rating_timestamp);
  it->rating += delta;
  while (it != top_dialogs.dialogs.begin()) {
    auto next = std::prev(it);
    if (*next < *it) {
      break;
    }
    std::swap(*next, *it);
    it = next;
  }

  LOG(INFO) << "Update " << top_dialog_category_name(category) << " rating of " << dialog_id << " by " << delta;

  if (!first_unsync_change_) {
    first_unsync_change_ = Timestamp::now_cached();
  }
  loop();
}

void TopDialogManager::remove_dialog(TopDialogCategory category, DialogId dialog_id,
                                     tl_object_ptr<telegram_api::InputPeer> input_peer) {
  if (!is_active_ || !is_enabled_) {
    return;
  }
  CHECK(dialog_id.is_valid());

  if (category == TopDialogCategory::ForwardUsers && dialog_id.get_type() != DialogType::User) {
    category = TopDialogCategory::ForwardChats;
  }

  auto pos = static_cast<size_t>(category);
  CHECK(pos < by_category_.size());
  auto &top_dialogs = by_category_[pos];

  LOG(INFO) << "Remove " << top_dialog_category_name(category) << " rating of " << dialog_id;

  if (input_peer != nullptr) {
    auto query = telegram_api::contacts_resetTopPeerRating(get_top_peer_category(category), std::move(input_peer));
    auto net_query = G()->net_query_creator().create(query);
    G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this, 1));
  }

  auto it = std::find_if(top_dialogs.dialogs.begin(), top_dialogs.dialogs.end(),
                         [&](auto &top_dialog) { return top_dialog.dialog_id == dialog_id; });
  if (it == top_dialogs.dialogs.end()) {
    return;
  }

  top_dialogs.is_dirty = true;
  top_dialogs.dialogs.erase(it);
  if (!first_unsync_change_) {
    first_unsync_change_ = Timestamp::now_cached();
  }
  loop();
}

void TopDialogManager::get_top_dialogs(TopDialogCategory category, size_t limit, Promise<vector<DialogId>> promise) {
  if (!is_active_) {
    promise.set_error(Status::Error(400, "Not supported without chat info database"));
    return;
  }
  if (!is_enabled_) {
    promise.set_error(Status::Error(400, "Top chats computation is disabled"));
    return;
  }

  GetTopDialogsQuery query;
  query.category = category;
  query.limit = limit;
  query.promise = std::move(promise);
  pending_get_top_dialogs_.push_back(std::move(query));
  loop();
}

void TopDialogManager::update_rating_e_decay() {
  if (!is_active_) {
    return;
  }
  rating_e_decay_ = G()->shared_config().get_option_integer("rating_e_decay", rating_e_decay_);
}

template <class StorerT>
void store(const TopDialogManager::TopDialog &top_dialog, StorerT &storer) {
  using ::td::store;
  store(top_dialog.dialog_id, storer);
  store(top_dialog.rating, storer);
}

template <class ParserT>
void parse(TopDialogManager::TopDialog &top_dialog, ParserT &parser) {
  using ::td::parse;
  parse(top_dialog.dialog_id, parser);
  parse(top_dialog.rating, parser);
}

template <class StorerT>
void store(const TopDialogManager::TopDialogs &top_dialogs, StorerT &storer) {
  using ::td::store;
  store(top_dialogs.rating_timestamp, storer);
  store(top_dialogs.dialogs, storer);
}

template <class ParserT>
void parse(TopDialogManager::TopDialogs &top_dialogs, ParserT &parser) {
  using ::td::parse;
  parse(top_dialogs.rating_timestamp, parser);
  parse(top_dialogs.dialogs, parser);
}

double TopDialogManager::rating_add(double now, double rating_timestamp) const {
  return std::exp((now - rating_timestamp) / rating_e_decay_);
}

double TopDialogManager::current_rating_add(double rating_timestamp) const {
  return rating_add(G()->server_time_cached(), rating_timestamp);
}

void TopDialogManager::normalize_rating() {
  for (auto &top_dialogs : by_category_) {
    auto div_by = current_rating_add(top_dialogs.rating_timestamp);
    top_dialogs.rating_timestamp = G()->server_time_cached();
    for (auto &dialog : top_dialogs.dialogs) {
      dialog.rating /= div_by;
    }
    top_dialogs.is_dirty = true;
  }
  db_sync_state_ = SyncState::None;
}

void TopDialogManager::do_get_top_dialogs(GetTopDialogsQuery &&query) {
  vector<DialogId> dialog_ids;
  if (query.category != TopDialogCategory::ForwardUsers) {
    auto pos = static_cast<size_t>(query.category);
    CHECK(pos < by_category_.size());
    dialog_ids = transform(by_category_[pos].dialogs, [](const auto &x) { return x.dialog_id; });
  } else {
    // merge ForwardUsers and ForwardChats
    auto &users = by_category_[static_cast<size_t>(TopDialogCategory::ForwardUsers)];
    auto &chats = by_category_[static_cast<size_t>(TopDialogCategory::ForwardChats)];
    size_t users_pos = 0;
    size_t chats_pos = 0;
    while (users_pos < users.dialogs.size() || chats_pos < chats.dialogs.size()) {
      if (chats_pos == chats.dialogs.size() ||
          (users_pos < users.dialogs.size() && users.dialogs[users_pos] < chats.dialogs[chats_pos])) {
        dialog_ids.push_back(users.dialogs[users_pos++].dialog_id);
      } else {
        dialog_ids.push_back(chats.dialogs[chats_pos++].dialog_id);
      }
    }
  }

  auto limit = std::min({query.limit, MAX_TOP_DIALOGS_LIMIT, dialog_ids.size()});

  auto promise = PromiseCreator::lambda([query = std::move(query), dialog_ids, limit](Result<Unit>) mutable {
    vector<DialogId> result;
    result.reserve(limit);
    for (auto dialog_id : dialog_ids) {
      if (dialog_id.get_type() == DialogType::User) {
        auto user_id = dialog_id.get_user_id();
        if (G()->td().get_actor_unsafe()->contacts_manager_->is_user_deleted(user_id)) {
          LOG(INFO) << "Skip deleted " << user_id;
          continue;
        }
        if (G()->td().get_actor_unsafe()->contacts_manager_->get_my_id() == user_id) {
          LOG(INFO) << "Skip self " << user_id;
          continue;
        }
        if (query.category == TopDialogCategory::BotInline || query.category == TopDialogCategory::BotPM) {
          auto r_bot_info = G()->td().get_actor_unsafe()->contacts_manager_->get_bot_data(user_id);
          if (r_bot_info.is_error()) {
            LOG(INFO) << "Skip not a bot " << user_id;
            continue;
          }
          if (query.category == TopDialogCategory::BotInline &&
              (r_bot_info.ok().username.empty() || !r_bot_info.ok().is_inline)) {
            LOG(INFO) << "Skip not inline bot " << user_id;
            continue;
          }
        }
      }

      result.push_back(dialog_id);
      if (result.size() == limit) {
        break;
      }
    }

    query.promise.set_value(std::move(result));
  });
  send_closure(G()->messages_manager(), &MessagesManager::load_dialogs, std::move(dialog_ids), std::move(promise));
}

void TopDialogManager::do_get_top_peers() {
  LOG(INFO) << "Send get top peers request";
  using telegram_api::contacts_getTopPeers;

  std::vector<uint32> ids;
  for (auto &category : by_category_) {
    for (auto &top_dialog : category.dialogs) {
      auto dialog_id = top_dialog.dialog_id;
      switch (dialog_id.get_type()) {
        case DialogType::Channel:
          ids.push_back(dialog_id.get_channel_id().get());
          break;
        case DialogType::User:
          ids.push_back(dialog_id.get_user_id().get());
          break;
        case DialogType::Chat:
          ids.push_back(dialog_id.get_chat_id().get());
          break;
        default:
          break;
      }
    }
  }

  int32 hash = get_vector_hash(ids);

  int32 flags = contacts_getTopPeers::CORRESPONDENTS_MASK | contacts_getTopPeers::BOTS_PM_MASK |
                contacts_getTopPeers::BOTS_INLINE_MASK | contacts_getTopPeers::GROUPS_MASK |
                contacts_getTopPeers::CHANNELS_MASK | contacts_getTopPeers::PHONE_CALLS_MASK |
                contacts_getTopPeers::FORWARD_USERS_MASK | contacts_getTopPeers::FORWARD_CHATS_MASK;

  contacts_getTopPeers query{flags,
                             true /*correspondents*/,
                             true /*bot_pm*/,
                             true /*bot_inline */,
                             true /*phone_calls*/,
                             true /*groups*/,
                             true /*channels*/,
                             true /*forward_users*/,
                             true /*forward_chats*/,
                             0 /*offset*/,
                             100 /*limit*/,
                             hash};
  auto net_query = G()->net_query_creator().create(query);
  G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this));
}

void TopDialogManager::on_result(NetQueryPtr net_query) {
  auto query_type = get_link_token();
  if (query_type == 2) {  // toggleTopPeers
    CHECK(have_toggle_top_peers_query_);
    have_toggle_top_peers_query_ = false;

    if (have_pending_toggle_top_peers_query_) {
      have_pending_toggle_top_peers_query_ = false;
      if (pending_toggle_top_peers_query_ != toggle_top_peers_query_is_enabled_) {
        send_toggle_top_peers(pending_toggle_top_peers_query_);
        return;
      }
    }

    auto r_result = fetch_result<telegram_api::contacts_toggleTopPeers>(std::move(net_query));
    if (r_result.is_ok()) {
      // everything is synchronized
      G()->td_db()->get_binlog_pmc()->erase("top_peers_enabled");
    } else {
      // let's resend the query forever
      if (!G()->close_flag()) {
        send_toggle_top_peers(toggle_top_peers_query_is_enabled_);
      }
    }
    return;
  }
  if (query_type == 1) {  // resetTopPeerRating
    // ignore result
    return;
  }
  SCOPE_EXIT {
    loop();
  };

  normalize_rating();  // once a day too

  auto r_top_peers = fetch_result<telegram_api::contacts_getTopPeers>(std::move(net_query));
  if (r_top_peers.is_error()) {
    last_server_sync_ = Timestamp::in(SERVER_SYNC_RESEND_DELAY - SERVER_SYNC_DELAY);
    return;
  }

  last_server_sync_ = Timestamp::now();
  server_sync_state_ = SyncState::Ok;
  SCOPE_EXIT {
    G()->td_db()->get_binlog_pmc()->set("top_dialogs_ts", to_string(static_cast<uint32>(Clocks::system())));
  };

  auto top_peers_parent = r_top_peers.move_as_ok();
  LOG(DEBUG) << "Receive contacts_getTopPeers result: " << to_string(top_peers_parent);
  switch (top_peers_parent->get_id()) {
    case telegram_api::contacts_topPeersNotModified::ID:
      // nothing to do
      return;
    case telegram_api::contacts_topPeersDisabled::ID:
      G()->shared_config().set_option_boolean("disable_top_chats", true);
      set_is_enabled(false);  // apply immediately
      return;
    case telegram_api::contacts_topPeers::ID: {
      G()->shared_config().set_option_empty("disable_top_chats");
      set_is_enabled(true);  // apply immediately
      auto top_peers = move_tl_object_as<telegram_api::contacts_topPeers>(std::move(top_peers_parent));

      send_closure(G()->contacts_manager(), &ContactsManager::on_get_users, std::move(top_peers->users_),
                   "on get top chats");
      send_closure(G()->contacts_manager(), &ContactsManager::on_get_chats, std::move(top_peers->chats_),
                   "on get top chats");
      for (auto &category : top_peers->categories_) {
        auto dialog_category = get_top_dialog_category(*category->category_);
        auto pos = static_cast<size_t>(dialog_category);
        CHECK(pos < by_category_.size());
        auto &top_dialogs = by_category_[pos];

        top_dialogs.is_dirty = true;
        top_dialogs.dialogs.clear();
        for (auto &top_peer : category->peers_) {
          TopDialog top_dialog;
          top_dialog.dialog_id = DialogId(top_peer->peer_);
          top_dialog.rating = top_peer->rating_;
          top_dialogs.dialogs.push_back(std::move(top_dialog));
        }
      }
      db_sync_state_ = SyncState::None;
      break;
    }
    default:
      UNREACHABLE();
  }
}

void TopDialogManager::do_save_top_dialogs() {
  LOG(INFO) << "Save top chats";
  for (size_t top_dialog_category_i = 0; top_dialog_category_i < by_category_.size(); top_dialog_category_i++) {
    auto top_dialog_category = TopDialogCategory(top_dialog_category_i);
    auto key = PSTRING() << "top_dialogs#" << top_dialog_category_name(top_dialog_category);

    auto &top_dialogs = by_category_[top_dialog_category_i];
    if (!top_dialogs.is_dirty) {
      continue;
    }
    top_dialogs.is_dirty = false;

    G()->td_db()->get_binlog_pmc()->set(key, log_event_store(top_dialogs).as_slice().str());
  }
  db_sync_state_ = SyncState::Ok;
  first_unsync_change_ = Timestamp();
}

void TopDialogManager::start_up() {
  do_start_up();
}

void TopDialogManager::do_start_up() {
  auto auth_manager = G()->td().get_actor_unsafe()->auth_manager_.get();
  if (auth_manager == nullptr || !auth_manager->is_authorized()) {
    return;
  }

  is_active_ = G()->parameters().use_chat_info_db && !auth_manager->is_bot();
  is_enabled_ = !G()->shared_config().get_option_boolean("disable_top_chats");
  update_rating_e_decay();

  string need_update_top_peers = G()->td_db()->get_binlog_pmc()->get("top_peers_enabled");
  if (!need_update_top_peers.empty()) {
    send_toggle_top_peers(need_update_top_peers[0] == '1');
  }

  init();
  loop();
}

void TopDialogManager::init() {
  was_first_sync_ = false;
  first_unsync_change_ = Timestamp();
  server_sync_state_ = SyncState::None;
  last_server_sync_ = Timestamp();
  CHECK(pending_get_top_dialogs_.empty());

  LOG(DEBUG) << "Init is enabled: " << is_enabled_;
  if (!is_active_) {
    G()->td_db()->get_binlog_pmc()->erase_by_prefix("top_dialogs");
    return;
  }

  auto di_top_dialogs_ts = G()->td_db()->get_binlog_pmc()->get("top_dialogs_ts");
  if (!di_top_dialogs_ts.empty()) {
    last_server_sync_ = Timestamp::in(to_integer<uint32>(di_top_dialogs_ts) - Clocks::system());
    if (last_server_sync_.is_in_past()) {
      server_sync_state_ = SyncState::Ok;
    }
  }

  if (is_enabled_) {
    for (size_t top_dialog_category_i = 0; top_dialog_category_i < by_category_.size(); top_dialog_category_i++) {
      auto top_dialog_category = TopDialogCategory(top_dialog_category_i);
      auto key = PSTRING() << "top_dialogs#" << top_dialog_category_name(top_dialog_category);
      auto value = G()->td_db()->get_binlog_pmc()->get(key);

      auto &top_dialogs = by_category_[top_dialog_category_i];
      top_dialogs.is_dirty = false;
      if (value.empty()) {
        continue;
      }
      log_event_parse(top_dialogs, value).ensure();
    }
    normalize_rating();
  } else {
    G()->td_db()->get_binlog_pmc()->erase_by_prefix("top_dialogs#");
    for (auto &top_dialogs : by_category_) {
      top_dialogs.is_dirty = false;
      top_dialogs.rating_timestamp = 0;
      top_dialogs.dialogs.clear();
    }
  }
  db_sync_state_ = SyncState::Ok;

  send_closure(G()->state_manager(), &StateManager::wait_first_sync,
               PromiseCreator::event(self_closure(this, &TopDialogManager::on_first_sync)));
}

void TopDialogManager::on_first_sync() {
  was_first_sync_ = true;
  if (!G()->close_flag() && G()->td().get_actor_unsafe()->auth_manager_->is_bot()) {
    is_active_ = false;
    init();
  }
  loop();
}

void TopDialogManager::loop() {
  if (!is_active_ || G()->close_flag()) {
    return;
  }

  if (!pending_get_top_dialogs_.empty()) {
    for (auto &query : pending_get_top_dialogs_) {
      do_get_top_dialogs(std::move(query));
    }
    pending_get_top_dialogs_.clear();
  }

  // server sync
  Timestamp server_sync_timeout;
  if (server_sync_state_ == SyncState::Ok) {
    server_sync_timeout = Timestamp::at(last_server_sync_.at() + SERVER_SYNC_DELAY);
    if (server_sync_timeout.is_in_past()) {
      server_sync_state_ = SyncState::None;
    }
  }

  Timestamp wakeup_timeout;
  if (server_sync_state_ == SyncState::Ok) {
    wakeup_timeout.relax(server_sync_timeout);
  } else if (server_sync_state_ == SyncState::None && was_first_sync_) {
    server_sync_state_ = SyncState::Pending;
    do_get_top_peers();
  }

  if (is_enabled_) {
    // database sync
    Timestamp db_sync_timeout;
    if (db_sync_state_ == SyncState::Ok) {
      if (first_unsync_change_) {
        db_sync_timeout = Timestamp::at(first_unsync_change_.at() + DB_SYNC_DELAY);
        if (db_sync_timeout.is_in_past()) {
          db_sync_state_ = SyncState::None;
        }
      }
    }

    if (db_sync_state_ == SyncState::Ok) {
      wakeup_timeout.relax(db_sync_timeout);
    } else if (db_sync_state_ == SyncState::None) {
      if (server_sync_state_ == SyncState::Ok) {
        do_save_top_dialogs();
      }
    }
  }

  if (wakeup_timeout) {
    LOG(INFO) << "Wakeup in: " << wakeup_timeout.in();
    set_timeout_at(wakeup_timeout.at());
  } else {
    LOG(INFO) << "Wakeup: never";
    cancel_timeout();
  }
}

}  // namespace td
