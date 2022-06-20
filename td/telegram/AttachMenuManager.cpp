//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AttachMenuManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileId.hpp"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TdParameters.h"
#include "td/telegram/ThemeManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class RequestWebViewQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::webAppInfo>> promise_;
  DialogId dialog_id_;
  UserId bot_user_id_;
  MessageId reply_to_message_id_;
  DialogId as_dialog_id_;
  bool from_attach_menu_ = false;

 public:
  explicit RequestWebViewQuery(Promise<td_api::object_ptr<td_api::webAppInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, UserId bot_user_id, tl_object_ptr<telegram_api::InputUser> &&input_user, string &&url,
            td_api::object_ptr<td_api::themeParameters> &&theme, MessageId reply_to_message_id, bool silent,
            DialogId as_dialog_id) {
    dialog_id_ = dialog_id;
    bot_user_id_ = bot_user_id;
    reply_to_message_id_ = reply_to_message_id;
    as_dialog_id_ = as_dialog_id;

    int32 flags = 0;

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
    CHECK(input_peer != nullptr);

    string start_parameter;
    if (begins_with(url, "start://")) {
      start_parameter = url.substr(8);
      url = string();

      flags |= telegram_api::messages_requestWebView::START_PARAM_MASK;
    } else if (begins_with(url, "menu://")) {
      url = url.substr(7);

      flags |= telegram_api::messages_requestWebView::FROM_BOT_MENU_MASK;
      flags |= telegram_api::messages_requestWebView::URL_MASK;
    } else if (!url.empty()) {
      flags |= telegram_api::messages_requestWebView::URL_MASK;
    } else {
      from_attach_menu_ = true;
    }

    tl_object_ptr<telegram_api::dataJSON> theme_parameters;
    if (theme != nullptr) {
      theme_parameters = make_tl_object<telegram_api::dataJSON>(string());
      theme_parameters->data_ = ThemeManager::get_theme_parameters_json_string(theme, false);

      flags |= telegram_api::messages_requestWebView::THEME_PARAMS_MASK;
    }

    if (reply_to_message_id.is_valid()) {
      flags |= telegram_api::messages_requestWebView::REPLY_TO_MSG_ID_MASK;
    }

    if (silent) {
      flags |= telegram_api::messages_requestWebView::SILENT_MASK;
    }

    tl_object_ptr<telegram_api::InputPeer> as_input_peer;
    if (as_dialog_id.is_valid()) {
      as_input_peer = td_->messages_manager_->get_input_peer(as_dialog_id, AccessRights::Write);
      if (as_input_peer != nullptr) {
        flags |= telegram_api::messages_requestWebView::SEND_AS_MASK;
      }
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_requestWebView(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_peer), std::move(input_user), url, start_parameter,
        std::move(theme_parameters), reply_to_message_id.get_server_message_id().get(), std::move(as_input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_requestWebView>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td_->attach_menu_manager_->open_web_view(ptr->query_id_, dialog_id_, bot_user_id_, reply_to_message_id_,
                                             as_dialog_id_);
    promise_.set_value(td_api::make_object<td_api::webAppInfo>(ptr->query_id_, ptr->url_));
  }

  void on_error(Status status) final {
    if (!td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "RequestWebViewQuery")) {
      if (from_attach_menu_) {
        td_->attach_menu_manager_->reload_attach_menu_bots(Promise<Unit>());
      }
    }
    promise_.set_error(std::move(status));
  }
};

class ProlongWebViewQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id, UserId bot_user_id, int64 query_id, MessageId reply_to_message_id, bool silent,
            DialogId as_dialog_id) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
    auto r_input_user = td_->contacts_manager_->get_input_user(bot_user_id);
    if (input_peer == nullptr || r_input_user.is_error()) {
      return;
    }

    int32 flags = 0;
    if (reply_to_message_id.is_valid()) {
      flags |= telegram_api::messages_prolongWebView::REPLY_TO_MSG_ID_MASK;
    }

    if (silent) {
      flags |= telegram_api::messages_prolongWebView::SILENT_MASK;
    }

    tl_object_ptr<telegram_api::InputPeer> as_input_peer;
    if (as_dialog_id.is_valid()) {
      as_input_peer = td_->messages_manager_->get_input_peer(as_dialog_id, AccessRights::Write);
      if (as_input_peer != nullptr) {
        flags |= telegram_api::messages_prolongWebView::SEND_AS_MASK;
      }
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_prolongWebView(
        flags, false /*ignored*/, std::move(input_peer), r_input_user.move_as_ok(), query_id,
        reply_to_message_id.get_server_message_id().get(), std::move(as_input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_prolongWebView>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool ptr = result_ptr.ok();
    if (!ptr) {
      LOG(ERROR) << "Failed to prolong a web view";
    }
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "ProlongWebViewQuery");
  }
};

class GetAttachMenuBotsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::AttachMenuBots>> promise_;

 public:
  explicit GetAttachMenuBotsQuery(Promise<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getAttachMenuBots(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getAttachMenuBots>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAttachMenuBotsQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetAttachMenuBotQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::attachMenuBotsBot>> promise_;

 public:
  explicit GetAttachMenuBotQuery(Promise<telegram_api::object_ptr<telegram_api::attachMenuBotsBot>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> &&input_user) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getAttachMenuBot(std::move(input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getAttachMenuBot>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAttachMenuBotQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ToggleBotInAttachMenuQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleBotInAttachMenuQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> &&input_user, bool is_added) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_toggleBotInAttachMenu(std::move(input_user), is_added)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_toggleBotInAttachMenu>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    if (!result) {
      LOG(ERROR) << "Failed to add a bot to attachment menu";
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->attach_menu_manager_->reload_attach_menu_bots(Promise<Unit>());
    promise_.set_error(std::move(status));
  }
};

bool operator==(const AttachMenuManager::AttachMenuBotColor &lhs, const AttachMenuManager::AttachMenuBotColor &rhs) {
  return lhs.light_color_ == rhs.light_color_ && lhs.dark_color_ == rhs.dark_color_;
}

bool operator!=(const AttachMenuManager::AttachMenuBotColor &lhs, const AttachMenuManager::AttachMenuBotColor &rhs) {
  return !(lhs == rhs);
}

template <class StorerT>
void AttachMenuManager::AttachMenuBotColor::store(StorerT &storer) const {
  td::store(light_color_, storer);
  td::store(dark_color_, storer);
}

template <class ParserT>
void AttachMenuManager::AttachMenuBotColor::parse(ParserT &parser) {
  td::parse(light_color_, parser);
  td::parse(dark_color_, parser);
}

bool operator==(const AttachMenuManager::AttachMenuBot &lhs, const AttachMenuManager::AttachMenuBot &rhs) {
  return lhs.user_id_ == rhs.user_id_ && lhs.supports_self_dialog_ == rhs.supports_self_dialog_ &&
         lhs.supports_user_dialogs_ == rhs.supports_user_dialogs_ &&
         lhs.supports_bot_dialogs_ == rhs.supports_bot_dialogs_ &&
         lhs.supports_group_dialogs_ == rhs.supports_group_dialogs_ &&
         lhs.supports_broadcast_dialogs_ == rhs.supports_broadcast_dialogs_ &&
         lhs.supports_settings_ == rhs.supports_settings_ && lhs.name_ == rhs.name_ &&
         lhs.default_icon_file_id_ == rhs.default_icon_file_id_ &&
         lhs.ios_static_icon_file_id_ == rhs.ios_static_icon_file_id_ &&
         lhs.ios_animated_icon_file_id_ == rhs.ios_animated_icon_file_id_ &&
         lhs.android_icon_file_id_ == rhs.android_icon_file_id_ && lhs.macos_icon_file_id_ == rhs.macos_icon_file_id_ &&
         lhs.is_added_ == rhs.is_added_ && lhs.name_color_ == rhs.name_color_ && lhs.icon_color_ == rhs.icon_color_ &&
         lhs.placeholder_file_id_ == rhs.placeholder_file_id_;
}

bool operator!=(const AttachMenuManager::AttachMenuBot &lhs, const AttachMenuManager::AttachMenuBot &rhs) {
  return !(lhs == rhs);
}

template <class StorerT>
void AttachMenuManager::AttachMenuBot::store(StorerT &storer) const {
  bool has_ios_static_icon_file_id = ios_static_icon_file_id_.is_valid();
  bool has_ios_animated_icon_file_id = ios_animated_icon_file_id_.is_valid();
  bool has_android_icon_file_id = android_icon_file_id_.is_valid();
  bool has_macos_icon_file_id = macos_icon_file_id_.is_valid();
  bool has_name_color = name_color_ != AttachMenuBotColor();
  bool has_icon_color = icon_color_ != AttachMenuBotColor();
  bool has_support_flags = true;
  bool has_placeholder_file_id = placeholder_file_id_.is_valid();
  bool has_cache_version = cache_version_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_ios_static_icon_file_id);
  STORE_FLAG(has_ios_animated_icon_file_id);
  STORE_FLAG(has_android_icon_file_id);
  STORE_FLAG(has_macos_icon_file_id);
  STORE_FLAG(is_added_);
  STORE_FLAG(has_name_color);
  STORE_FLAG(has_icon_color);
  STORE_FLAG(has_support_flags);
  STORE_FLAG(supports_self_dialog_);
  STORE_FLAG(supports_user_dialogs_);
  STORE_FLAG(supports_bot_dialogs_);
  STORE_FLAG(supports_group_dialogs_);
  STORE_FLAG(supports_broadcast_dialogs_);
  STORE_FLAG(supports_settings_);
  STORE_FLAG(has_placeholder_file_id);
  STORE_FLAG(has_cache_version);
  END_STORE_FLAGS();
  td::store(user_id_, storer);
  td::store(name_, storer);
  td::store(default_icon_file_id_, storer);
  if (has_ios_static_icon_file_id) {
    td::store(ios_static_icon_file_id_, storer);
  }
  if (has_ios_animated_icon_file_id) {
    td::store(ios_animated_icon_file_id_, storer);
  }
  if (has_android_icon_file_id) {
    td::store(android_icon_file_id_, storer);
  }
  if (has_macos_icon_file_id) {
    td::store(macos_icon_file_id_, storer);
  }
  if (has_name_color) {
    td::store(name_color_, storer);
  }
  if (has_icon_color) {
    td::store(icon_color_, storer);
  }
  if (has_placeholder_file_id) {
    td::store(placeholder_file_id_, storer);
  }
  if (has_cache_version) {
    td::store(cache_version_, storer);
  }
}

template <class ParserT>
void AttachMenuManager::AttachMenuBot::parse(ParserT &parser) {
  bool has_ios_static_icon_file_id;
  bool has_ios_animated_icon_file_id;
  bool has_android_icon_file_id;
  bool has_macos_icon_file_id;
  bool has_name_color;
  bool has_icon_color;
  bool has_support_flags;
  bool has_placeholder_file_id;
  bool has_cache_version;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_ios_static_icon_file_id);
  PARSE_FLAG(has_ios_animated_icon_file_id);
  PARSE_FLAG(has_android_icon_file_id);
  PARSE_FLAG(has_macos_icon_file_id);
  PARSE_FLAG(is_added_);
  PARSE_FLAG(has_name_color);
  PARSE_FLAG(has_icon_color);
  PARSE_FLAG(has_support_flags);
  PARSE_FLAG(supports_self_dialog_);
  PARSE_FLAG(supports_user_dialogs_);
  PARSE_FLAG(supports_bot_dialogs_);
  PARSE_FLAG(supports_group_dialogs_);
  PARSE_FLAG(supports_broadcast_dialogs_);
  PARSE_FLAG(supports_settings_);
  PARSE_FLAG(has_placeholder_file_id);
  PARSE_FLAG(has_cache_version);
  END_PARSE_FLAGS();
  td::parse(user_id_, parser);
  td::parse(name_, parser);
  td::parse(default_icon_file_id_, parser);
  if (has_ios_static_icon_file_id) {
    td::parse(ios_static_icon_file_id_, parser);
  }
  if (has_ios_animated_icon_file_id) {
    td::parse(ios_animated_icon_file_id_, parser);
  }
  if (has_android_icon_file_id) {
    td::parse(android_icon_file_id_, parser);
  }
  if (has_macos_icon_file_id) {
    td::parse(macos_icon_file_id_, parser);
  }
  if (has_name_color) {
    td::parse(name_color_, parser);
  }
  if (has_icon_color) {
    td::parse(icon_color_, parser);
  }
  if (has_placeholder_file_id) {
    td::parse(placeholder_file_id_, parser);
  }
  if (has_cache_version) {
    td::parse(cache_version_, parser);
  }

  if (!has_support_flags) {
    supports_self_dialog_ = true;
    supports_user_dialogs_ = true;
    supports_bot_dialogs_ = true;
  }
}

class AttachMenuManager::AttachMenuBotsLogEvent {
 public:
  int64 hash_ = 0;
  vector<AttachMenuBot> attach_menu_bots_;

  AttachMenuBotsLogEvent() = default;

  AttachMenuBotsLogEvent(int64 hash, vector<AttachMenuBot> attach_menu_bots)
      : hash_(hash), attach_menu_bots_(std::move(attach_menu_bots)) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(hash_, storer);
    td::store(attach_menu_bots_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(hash_, parser);
    td::parse(attach_menu_bots_, parser);
  }
};

AttachMenuManager::AttachMenuManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void AttachMenuManager::tear_down() {
  parent_.reset();
}

void AttachMenuManager::start_up() {
  init();
}

void AttachMenuManager::init() {
  if (!is_active()) {
    return;
  }
  if (is_inited_) {
    return;
  }
  is_inited_ = true;

  if (!G()->parameters().use_chat_info_db) {
    G()->td_db()->get_binlog_pmc()->erase(get_attach_menu_bots_database_key());
  } else {
    auto attach_menu_bots_string = G()->td_db()->get_binlog_pmc()->get(get_attach_menu_bots_database_key());

    if (!attach_menu_bots_string.empty()) {
      AttachMenuBotsLogEvent attach_menu_bots_log_event;
      bool is_valid = true;
      is_valid &= log_event_parse(attach_menu_bots_log_event, attach_menu_bots_string).is_ok();

      Dependencies dependencies;
      for (auto &attach_menu_bot : attach_menu_bots_log_event.attach_menu_bots_) {
        if (!attach_menu_bot.user_id_.is_valid() || !attach_menu_bot.default_icon_file_id_.is_valid()) {
          is_valid = false;
        }
        if (!is_valid) {
          break;
        }
        dependencies.add(attach_menu_bot.user_id_);
      }
      if (is_valid && dependencies.resolve_force(td_, "AttachMenuBotsLogEvent")) {
        bool is_cache_outdated = false;
        for (auto &bot : attach_menu_bots_log_event.attach_menu_bots_) {
          if (bot.cache_version_ != AttachMenuBot::CACHE_VERSION) {
            is_cache_outdated = true;
          }
        }
        hash_ = is_cache_outdated ? 0 : attach_menu_bots_log_event.hash_;
        attach_menu_bots_ = std::move(attach_menu_bots_log_event.attach_menu_bots_);
      } else {
        LOG(ERROR) << "Ignore invalid attachment menu bots log event";
      }
    }
  }

  class StateCallback final : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<AttachMenuManager> parent) : parent_(std::move(parent)) {
    }
    bool on_online(bool is_online) final {
      if (is_online) {
        send_closure(parent_, &AttachMenuManager::on_online, is_online);
      }
      return parent_.is_alive();
    }

   private:
    ActorId<AttachMenuManager> parent_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));

  send_update_attach_menu_bots();
  reload_attach_menu_bots(Promise<Unit>());
}

void AttachMenuManager::timeout_expired() {
  if (!is_active()) {
    return;
  }

  reload_attach_menu_bots(Promise<Unit>());
}

bool AttachMenuManager::is_active() const {
  return !G()->close_flag() && td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot();
}

void AttachMenuManager::on_online(bool is_online) {
  if (is_online) {
    ping_web_view();
  } else {
    ping_web_view_timeout_.cancel_timeout();
  }
}

void AttachMenuManager::ping_web_view_static(void *td_void) {
  if (G()->close_flag()) {
    return;
  }

  CHECK(td_void != nullptr);
  auto td = static_cast<Td *>(td_void);

  td->attach_menu_manager_->ping_web_view();
}

void AttachMenuManager::ping_web_view() {
  if (G()->close_flag() || opened_web_views_.empty()) {
    return;
  }

  for (const auto &it : opened_web_views_) {
    const auto &opened_web_view = it.second;
    bool silent = td_->messages_manager_->get_dialog_silent_send_message(opened_web_view.dialog_id_);
    td_->create_handler<ProlongWebViewQuery>()->send(opened_web_view.dialog_id_, opened_web_view.bot_user_id_, it.first,
                                                     opened_web_view.reply_to_message_id_, silent,
                                                     opened_web_view.as_dialog_id_);
  }

  schedule_ping_web_view();
}

void AttachMenuManager::schedule_ping_web_view() {
  ping_web_view_timeout_.set_callback(ping_web_view_static);
  ping_web_view_timeout_.set_callback_data(static_cast<void *>(td_));
  ping_web_view_timeout_.set_timeout_in(PING_WEB_VIEW_TIMEOUT);
}

void AttachMenuManager::request_web_view(DialogId dialog_id, UserId bot_user_id, MessageId reply_to_message_id,
                                         string &&url, td_api::object_ptr<td_api::themeParameters> &&theme,
                                         Promise<td_api::object_ptr<td_api::webAppInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->contacts_manager_->get_bot_data(bot_user_id));
  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(bot_user_id));

  if (!td_->messages_manager_->have_dialog_force(dialog_id, "request_web_view")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::Chat:
    case DialogType::Channel:
      // ok
      break;
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Web Apps can't be opened in secret chats"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }

  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Write)) {
    return promise.set_error(Status::Error(400, "Have no write access to the chat"));
  }

  if (!reply_to_message_id.is_valid() || !reply_to_message_id.is_server() ||
      !td_->messages_manager_->have_message_force({dialog_id, reply_to_message_id}, "request_web_view")) {
    reply_to_message_id = MessageId();
  }

  bool silent = td_->messages_manager_->get_dialog_silent_send_message(dialog_id);
  DialogId as_dialog_id = td_->messages_manager_->get_dialog_default_send_message_as_dialog_id(dialog_id);

  td_->create_handler<RequestWebViewQuery>(std::move(promise))
      ->send(dialog_id, bot_user_id, std::move(input_user), std::move(url), std::move(theme), reply_to_message_id,
             silent, as_dialog_id);
}

void AttachMenuManager::open_web_view(int64 query_id, DialogId dialog_id, UserId bot_user_id,
                                      MessageId reply_to_message_id, DialogId as_dialog_id) {
  if (query_id == 0) {
    LOG(ERROR) << "Receive Web App query identifier == 0";
    return;
  }

  if (opened_web_views_.empty()) {
    schedule_ping_web_view();
  }
  OpenedWebView opened_web_view;
  opened_web_view.dialog_id_ = dialog_id;
  opened_web_view.bot_user_id_ = bot_user_id;
  opened_web_view.reply_to_message_id_ = reply_to_message_id;
  opened_web_view.as_dialog_id_ = as_dialog_id;
  opened_web_views_.emplace(query_id, std::move(opened_web_view));
}

void AttachMenuManager::close_web_view(int64 query_id, Promise<Unit> &&promise) {
  opened_web_views_.erase(query_id);
  if (opened_web_views_.empty()) {
    ping_web_view_timeout_.cancel_timeout();
  }
  promise.set_value(Unit());
}

Result<AttachMenuManager::AttachMenuBot> AttachMenuManager::get_attach_menu_bot(
    tl_object_ptr<telegram_api::attachMenuBot> &&bot) const {
  UserId user_id(bot->bot_id_);
  if (!td_->contacts_manager_->have_user(user_id)) {
    return Status::Error(PSLICE() << "Have no information about " << user_id);
  }

  AttachMenuBot attach_menu_bot;
  attach_menu_bot.is_added_ = !bot->inactive_;
  attach_menu_bot.user_id_ = user_id;
  attach_menu_bot.name_ = std::move(bot->short_name_);
  for (auto &icon : bot->icons_) {
    Slice name = icon->name_;
    int32 document_id = icon->icon_->get_id();
    if (document_id == telegram_api::documentEmpty::ID) {
      return Status::Error(PSLICE() << "Have no icon for " << user_id << " with name " << name);
    }
    CHECK(document_id == telegram_api::document::ID);

    if (name != "default_static" && name != "ios_static" && name != "ios_animated" && name != "android_animated" &&
        name != "macos_animated" && name != "placeholder_static") {
      LOG(ERROR) << "Have icon for " << user_id << " with name " << name;
      continue;
    }

    auto expected_document_type = ends_with(name, "_static") ? Document::Type::General : Document::Type::Sticker;
    auto parsed_document =
        td_->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(icon->icon_), DialogId());
    if (parsed_document.type != expected_document_type) {
      LOG(ERROR) << "Receive wrong attachment menu bot icon \"" << name << "\" for " << user_id;
      continue;
    }
    bool expect_colors = false;
    switch (name[5]) {
      case 'l':
        attach_menu_bot.default_icon_file_id_ = parsed_document.file_id;
        break;
      case 't':
        attach_menu_bot.ios_static_icon_file_id_ = parsed_document.file_id;
        break;
      case 'n':
        attach_menu_bot.ios_animated_icon_file_id_ = parsed_document.file_id;
        break;
      case 'i':
        attach_menu_bot.android_icon_file_id_ = parsed_document.file_id;
        expect_colors = true;
        break;
      case '_':
        attach_menu_bot.macos_icon_file_id_ = parsed_document.file_id;
        break;
      case 'h':
        attach_menu_bot.placeholder_file_id_ = parsed_document.file_id;
        break;
      default:
        UNREACHABLE();
    }
    if (expect_colors) {
      if (icon->colors_.empty()) {
        LOG(ERROR) << "Have no colors for attachment menu bot icon for " << user_id;
      } else {
        for (auto &color : icon->colors_) {
          if (color->name_ != "light_icon" && color->name_ != "light_text" && color->name_ != "dark_icon" &&
              color->name_ != "dark_text") {
            LOG(ERROR) << "Receive unexpected attachment menu color " << color->name_ << " for " << user_id;
            continue;
          }
          auto alpha = (color->color_ >> 24) & 0xFF;
          if (alpha != 0 && alpha != 0xFF) {
            LOG(ERROR) << "Receive alpha in attachment menu color " << color->name_ << " for " << user_id;
          }
          auto c = color->color_ & 0xFFFFFF;
          switch (color->name_[6]) {
            case 'i':
              attach_menu_bot.icon_color_.light_color_ = c;
              break;
            case 't':
              attach_menu_bot.name_color_.light_color_ = c;
              break;
            case 'c':
              attach_menu_bot.icon_color_.dark_color_ = c;
              break;
            case 'e':
              attach_menu_bot.name_color_.dark_color_ = c;
              break;
            default:
              UNREACHABLE();
          }
        }
        if (attach_menu_bot.icon_color_.light_color_ == -1 || attach_menu_bot.icon_color_.dark_color_ == -1) {
          LOG(ERROR) << "Receive wrong icon_color for " << user_id;
          attach_menu_bot.icon_color_ = AttachMenuBotColor();
        }
        if (attach_menu_bot.name_color_.light_color_ == -1 || attach_menu_bot.name_color_.dark_color_ == -1) {
          LOG(ERROR) << "Receive wrong name_color for " << user_id;
          attach_menu_bot.name_color_ = AttachMenuBotColor();
        }
      }
    } else {
      if (!icon->colors_.empty()) {
        LOG(ERROR) << "Have unexpected colors for attachment menu bot icon for " << user_id << " with name " << name;
      }
    }
  }
  for (auto &peer_type : bot->peer_types_) {
    switch (peer_type->get_id()) {
      case telegram_api::attachMenuPeerTypeSameBotPM::ID:
        attach_menu_bot.supports_self_dialog_ = true;
        break;
      case telegram_api::attachMenuPeerTypeBotPM::ID:
        attach_menu_bot.supports_bot_dialogs_ = true;
        break;
      case telegram_api::attachMenuPeerTypePM::ID:
        attach_menu_bot.supports_user_dialogs_ = true;
        break;
      case telegram_api::attachMenuPeerTypeChat::ID:
        attach_menu_bot.supports_group_dialogs_ = true;
        break;
      case telegram_api::attachMenuPeerTypeBroadcast::ID:
        attach_menu_bot.supports_broadcast_dialogs_ = true;
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
  attach_menu_bot.supports_settings_ = bot->has_settings_;
  if (!attach_menu_bot.default_icon_file_id_.is_valid()) {
    return Status::Error(PSLICE() << "Have no default icon for " << user_id);
  }
  attach_menu_bot.cache_version_ = AttachMenuBot::CACHE_VERSION;

  return std::move(attach_menu_bot);
}

void AttachMenuManager::reload_attach_menu_bots(Promise<Unit> &&promise) {
  if (!is_active()) {
    return;
  }
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&result) mutable {
        send_closure(actor_id, &AttachMenuManager::on_reload_attach_menu_bots, std::move(result), std::move(promise));
      });
  td_->create_handler<GetAttachMenuBotsQuery>(std::move(query_promise))->send(hash_);
}

void AttachMenuManager::on_reload_attach_menu_bots(
    Result<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&result, Promise<Unit> &&promise) {
  if (!is_active()) {
    return promise.set_value(Unit());
  }
  if (result.is_error()) {
    set_timeout_in(Random::fast(60, 120));
    return promise.set_value(Unit());
  }

  is_inited_ = true;

  set_timeout_in(Random::fast(3600, 4800));

  auto attach_menu_bots_ptr = result.move_as_ok();
  auto constructor_id = attach_menu_bots_ptr->get_id();
  if (constructor_id == telegram_api::attachMenuBotsNotModified::ID) {
    return promise.set_value(Unit());
  }
  CHECK(constructor_id == telegram_api::attachMenuBots::ID);
  auto attach_menu_bots = move_tl_object_as<telegram_api::attachMenuBots>(attach_menu_bots_ptr);

  td_->contacts_manager_->on_get_users(std::move(attach_menu_bots->users_), "on_reload_attach_menu_bots");

  auto new_hash = attach_menu_bots->hash_;
  vector<AttachMenuBot> new_attach_menu_bots;

  for (auto &bot : attach_menu_bots->bots_) {
    auto r_attach_menu_bot = get_attach_menu_bot(std::move(bot));
    if (r_attach_menu_bot.is_error()) {
      LOG(ERROR) << r_attach_menu_bot.error().message();
      new_hash = 0;
      continue;
    }
    if (!r_attach_menu_bot.ok().is_added_) {
      LOG(ERROR) << "Receive non-added attachment menu bot " << r_attach_menu_bot.ok().user_id_;
      new_hash = 0;
      continue;
    }

    new_attach_menu_bots.push_back(r_attach_menu_bot.move_as_ok());
  }

  bool need_update = new_attach_menu_bots != attach_menu_bots_;
  if (need_update || hash_ != new_hash) {
    hash_ = new_hash;
    attach_menu_bots_ = std::move(new_attach_menu_bots);

    if (need_update) {
      send_update_attach_menu_bots();
    }

    save_attach_menu_bots();
  }
  promise.set_value(Unit());
}

void AttachMenuManager::remove_bot_from_attach_menu(UserId user_id) {
  for (auto it = attach_menu_bots_.begin(); it != attach_menu_bots_.end(); ++it) {
    if (it->user_id_ == user_id) {
      hash_ = 0;
      attach_menu_bots_.erase(it);

      send_update_attach_menu_bots();
      save_attach_menu_bots();
      return;
    }
  }
}

void AttachMenuManager::get_attach_menu_bot(UserId user_id,
                                            Promise<td_api::object_ptr<td_api::attachmentMenuBot>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(user_id));

  TRY_RESULT_PROMISE(promise, bot_data, td_->contacts_manager_->get_bot_data(user_id));
  if (!bot_data.can_be_added_to_attach_menu) {
    return promise.set_error(Status::Error(400, "The bot can't be added to attachment menu"));
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), user_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::attachMenuBotsBot>> &&result) mutable {
        send_closure(actor_id, &AttachMenuManager::on_get_attach_menu_bot, user_id, std::move(result),
                     std::move(promise));
      });
  td_->create_handler<GetAttachMenuBotQuery>(std::move(query_promise))->send(std::move(input_user));
}

void AttachMenuManager::on_get_attach_menu_bot(
    UserId user_id, Result<telegram_api::object_ptr<telegram_api::attachMenuBotsBot>> &&result,
    Promise<td_api::object_ptr<td_api::attachmentMenuBot>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, bot, std::move(result));

  td_->contacts_manager_->on_get_users(std::move(bot->users_), "on_get_attach_menu_bot");

  auto r_attach_menu_bot = get_attach_menu_bot(std::move(bot->bot_));
  if (r_attach_menu_bot.is_error()) {
    LOG(ERROR) << r_attach_menu_bot.error().message();
    return promise.set_error(Status::Error(500, "Receive invalid response"));
  }
  auto attach_menu_bot = r_attach_menu_bot.move_as_ok();
  if (attach_menu_bot.user_id_ != user_id) {
    return promise.set_error(Status::Error(500, "Receive wrong bot"));
  }
  if (attach_menu_bot.is_added_) {
    bool is_found = false;
    for (auto &old_bot : attach_menu_bots_) {
      if (old_bot.user_id_ == user_id) {
        is_found = true;
        break;
      }
    }
    if (!is_found) {
      LOG(INFO) << "Add missing attachment menu bot " << user_id;
    }
    hash_ = 0;
    attach_menu_bots_.insert(attach_menu_bots_.begin(), attach_menu_bot);

    send_update_attach_menu_bots();
    save_attach_menu_bots();
  } else {
    remove_bot_from_attach_menu(user_id);
  }
  promise.set_value(get_attachment_menu_bot_object(attach_menu_bot));
}

void AttachMenuManager::toggle_bot_is_added_to_attach_menu(UserId user_id, bool is_added, Promise<Unit> &&promise) {
  CHECK(is_active());

  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(user_id));

  bool is_found = false;
  for (auto &bot : attach_menu_bots_) {
    if (bot.user_id_ == user_id) {
      is_found = true;
      break;
    }
  }
  if (is_added == is_found) {
    return promise.set_value(Unit());
  }

  if (is_added) {
    TRY_RESULT_PROMISE(promise, bot_data, td_->contacts_manager_->get_bot_data(user_id));
    if (!bot_data.can_be_added_to_attach_menu) {
      return promise.set_error(Status::Error(400, "The bot can't be added to attachment menu"));
    }
  } else {
    remove_bot_from_attach_menu(user_id);
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure(actor_id, &AttachMenuManager::reload_attach_menu_bots, std::move(promise));
        }
      });

  td_->create_handler<ToggleBotInAttachMenuQuery>(std::move(query_promise))->send(std::move(input_user), is_added);
}

td_api::object_ptr<td_api::attachmentMenuBot> AttachMenuManager::get_attachment_menu_bot_object(
    const AttachMenuBot &bot) const {
  auto get_file = [td = td_](FileId file_id) -> td_api::object_ptr<td_api::file> {
    if (!file_id.is_valid()) {
      return nullptr;
    }
    return td->file_manager_->get_file_object(file_id);
  };
  auto get_attach_menu_bot_color_object =
      [](const AttachMenuBotColor &color) -> td_api::object_ptr<td_api::attachmentMenuBotColor> {
    if (color == AttachMenuBotColor()) {
      return nullptr;
    }
    return td_api::make_object<td_api::attachmentMenuBotColor>(color.light_color_, color.dark_color_);
  };

  return td_api::make_object<td_api::attachmentMenuBot>(
      td_->contacts_manager_->get_user_id_object(bot.user_id_, "get_attachment_menu_bot_object"),
      bot.supports_self_dialog_, bot.supports_user_dialogs_, bot.supports_bot_dialogs_, bot.supports_group_dialogs_,
      bot.supports_broadcast_dialogs_, bot.supports_settings_, bot.name_,
      get_attach_menu_bot_color_object(bot.name_color_), get_file(bot.default_icon_file_id_),
      get_file(bot.ios_static_icon_file_id_), get_file(bot.ios_animated_icon_file_id_),
      get_file(bot.android_icon_file_id_), get_file(bot.macos_icon_file_id_),
      get_attach_menu_bot_color_object(bot.icon_color_), get_file(bot.placeholder_file_id_));
}

td_api::object_ptr<td_api::updateAttachmentMenuBots> AttachMenuManager::get_update_attachment_menu_bots_object() const {
  CHECK(is_active());
  CHECK(is_inited_);
  auto bots =
      transform(attach_menu_bots_, [this](const AttachMenuBot &bot) { return get_attachment_menu_bot_object(bot); });
  return td_api::make_object<td_api::updateAttachmentMenuBots>(std::move(bots));
}

void AttachMenuManager::send_update_attach_menu_bots() const {
  send_closure(G()->td(), &Td::send_update, get_update_attachment_menu_bots_object());
}

string AttachMenuManager::get_attach_menu_bots_database_key() {
  return "attach_bots";
}

void AttachMenuManager::save_attach_menu_bots() {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }

  if (attach_menu_bots_.empty()) {
    G()->td_db()->get_binlog_pmc()->erase(get_attach_menu_bots_database_key());
  } else {
    AttachMenuBotsLogEvent attach_menu_bots_log_event{hash_, attach_menu_bots_};
    G()->td_db()->get_binlog_pmc()->set(get_attach_menu_bots_database_key(),
                                        log_event_store(attach_menu_bots_log_event).as_slice().str());
  }
}

void AttachMenuManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!is_active()) {
    return;
  }

  updates.push_back(get_update_attachment_menu_bots_object());
}

}  // namespace td
