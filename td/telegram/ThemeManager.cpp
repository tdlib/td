//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ThemeManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/BackgroundManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"

namespace td {

class GetChatThemesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_ChatThemes>> promise_;

 public:
  explicit GetChatThemesQuery(Promise<telegram_api::object_ptr<telegram_api::account_ChatThemes>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getChatThemes(hash)));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getChatThemes>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) final {
    promise_.set_error(std::move(status));
  }
};

bool operator==(const ThemeManager::ThemeSettings &lhs, const ThemeManager::ThemeSettings &rhs) {
  return lhs.accent_color == rhs.accent_color && lhs.background_id == rhs.background_id &&
         lhs.background_type == rhs.background_type && lhs.base_theme == rhs.base_theme &&
         lhs.message_colors == rhs.message_colors && lhs.animate_message_colors == rhs.animate_message_colors;
}

bool operator!=(const ThemeManager::ThemeSettings &lhs, const ThemeManager::ThemeSettings &rhs) {
  return !(lhs == rhs);
}

ThemeManager::ThemeManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void ThemeManager::start_up() {
  init();
}

void ThemeManager::init() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  chat_themes_.next_reload_time = Time::now();  // TODO load chat themes from binlog
  loop();
}

void ThemeManager::tear_down() {
  parent_.reset();
}

void ThemeManager::loop() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  if (Time::now() < chat_themes_.next_reload_time) {
    return set_timeout_at(chat_themes_.next_reload_time);
  }

  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_ChatThemes>> result) {
        send_closure(actor_id, &ThemeManager::on_get_chat_themes, std::move(result));
      });

  td_->create_handler<GetChatThemesQuery>(std::move(request_promise))->send(chat_themes_.hash);
}

void ThemeManager::on_update_theme(telegram_api::object_ptr<telegram_api::theme> &&theme, Promise<Unit> &&promise) {
  CHECK(theme != nullptr);
  bool is_changed = false;
  for (auto &chat_theme : chat_themes_.themes) {
    if (chat_theme.light_id == theme->id_ || chat_theme.dark_id == theme->id_) {
      auto theme_settings = get_chat_theme_settings(std::move(theme->settings_));
      if (chat_theme.light_id == theme->id_ && chat_theme.light_theme != theme_settings) {
        chat_theme.light_theme = theme_settings;
        is_changed = true;
      }
      if (chat_theme.dark_id == theme->id_ && chat_theme.dark_theme != theme_settings) {
        chat_theme.dark_theme = theme_settings;
        is_changed = true;
      }
    }
  }
  if (is_changed) {
    send_update_chat_themes();
  }
  promise.set_value(Unit());
}

td_api::object_ptr<td_api::themeSettings> ThemeManager::get_theme_settings_object(const ThemeSettings &settings) const {
  auto fill = [colors = settings.message_colors]() mutable -> td_api::object_ptr<td_api::BackgroundFill> {
    if (colors.size() >= 3) {
      return td_api::make_object<td_api::backgroundFillFreeformGradient>(std::move(colors));
    }
    CHECK(!colors.empty());
    if (colors.size() == 1 || colors[0] == colors[1]) {
      return td_api::make_object<td_api::backgroundFillSolid>(colors[0]);
    }
    return td_api::make_object<td_api::backgroundFillGradient>(colors[1], colors[0], 0);
  }();

  // ignore settings.base_theme for now
  return td_api::make_object<td_api::themeSettings>(
      settings.accent_color,
      td_->background_manager_->get_background_object(settings.background_id, false, &settings.background_type),
      std::move(fill), settings.animate_message_colors);
}

td_api::object_ptr<td_api::chatTheme> ThemeManager::get_chat_theme_object(const ChatTheme &theme) const {
  return td_api::make_object<td_api::chatTheme>(theme.emoji, get_theme_settings_object(theme.light_theme),
                                                get_theme_settings_object(theme.dark_theme));
}

td_api::object_ptr<td_api::updateChatThemes> ThemeManager::get_update_chat_themes_object() const {
  return td_api::make_object<td_api::updateChatThemes>(
      transform(chat_themes_.themes, [this](const ChatTheme &theme) { return get_chat_theme_object(theme); }));
}

void ThemeManager::send_update_chat_themes() const {
  send_closure(G()->td(), &Td::send_update, get_update_chat_themes_object());
}

void ThemeManager::on_get_chat_themes(Result<telegram_api::object_ptr<telegram_api::account_ChatThemes>> result) {
  if (result.is_error()) {
    set_timeout_in(Random::fast(40, 60));
    return;
  }

  chat_themes_.next_reload_time = Time::now() + THEME_CACHE_TIME;
  set_timeout_at(chat_themes_.next_reload_time);

  auto chat_themes_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(chat_themes_ptr);
  if (chat_themes_ptr->get_id() == telegram_api::account_chatThemesNotModified::ID) {
    return;
  }
  CHECK(chat_themes_ptr->get_id() == telegram_api::account_chatThemes::ID);
  auto chat_themes = telegram_api::move_object_as<telegram_api::account_chatThemes>(chat_themes_ptr);
  chat_themes_.hash = chat_themes->hash_;
  chat_themes_.themes.clear();
  for (auto &chat_theme : chat_themes->themes_) {
    if (chat_theme->emoticon_.empty()) {
      LOG(ERROR) << "Receive " << to_string(chat_theme);
      continue;
    }

    ChatTheme theme;
    theme.emoji = std::move(chat_theme->emoticon_);
    theme.light_id = chat_theme->theme_->id_;
    theme.dark_id = chat_theme->dark_theme_->id_;
    theme.light_theme = get_chat_theme_settings(std::move(chat_theme->theme_->settings_));
    theme.dark_theme = get_chat_theme_settings(std::move(chat_theme->dark_theme_->settings_));
    if (theme.light_theme.message_colors.empty() || theme.dark_theme.message_colors.empty()) {
      continue;
    }
    chat_themes_.themes.push_back(std::move(theme));
  }

  send_update_chat_themes();
}

ThemeManager::BaseTheme ThemeManager::get_base_theme(
    const telegram_api::object_ptr<telegram_api::BaseTheme> &base_theme) {
  CHECK(base_theme != nullptr);
  switch (base_theme->get_id()) {
    case telegram_api::baseThemeClassic::ID:
      return BaseTheme::Classic;
    case telegram_api::baseThemeDay::ID:
      return BaseTheme::Day;
    case telegram_api::baseThemeNight::ID:
      return BaseTheme::Night;
    case telegram_api::baseThemeTinted::ID:
      return BaseTheme::Tinted;
    case telegram_api::baseThemeArctic::ID:
      return BaseTheme::Arctic;
    default:
      UNREACHABLE();
      return BaseTheme::Classic;
  }
}

ThemeManager::ThemeSettings ThemeManager::get_chat_theme_settings(
    telegram_api::object_ptr<telegram_api::themeSettings> settings) {
  ThemeSettings result;
  if (settings != nullptr && !settings->message_colors_.empty() && settings->message_colors_.size() <= 4) {
    auto background =
        td_->background_manager_->on_get_background(BackgroundId(), string(), std::move(settings->wallpaper_), false);

    result.accent_color = settings->accent_color_;
    result.background_id = background.first;
    result.background_type = std::move(background.second);
    result.base_theme = get_base_theme(settings->base_theme_);
    result.message_colors = std::move(settings->message_colors_);
    result.animate_message_colors = settings->message_colors_animated_;
  }
  return result;
}

void ThemeManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot() || chat_themes_.themes.empty()) {
    return;
  }

  updates.push_back(get_update_chat_themes_object());
}

}  // namespace td
