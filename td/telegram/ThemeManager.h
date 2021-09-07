//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundId.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class ThemeManager final : public Actor {
 public:
  ThemeManager(Td *td, ActorShared<> parent);

  void get_chat_themes(Promise<td_api::object_ptr<td_api::chatThemes>> &&promise);

 private:
  enum class BaseTheme : int32 { Classic, Day, Night, Tinted, Arctic };

  static constexpr int32 THEME_CACHE_TIME = 3600;

  struct ThemeSettings {
    int32 accent_color = 0;
    BackgroundId background_id;
    BackgroundType background_type;
    BaseTheme base_theme;
    vector<int32> message_colors;
    bool animate_message_colors = false;
  };

  struct ChatTheme {
    string emoji;
    ThemeSettings light_theme;
    ThemeSettings dark_theme;
  };

  struct ChatThemes {
    int32 hash = 0;
    double next_reload_time = 0;
    vector<ChatTheme> themes;
  };

  void tear_down() final;

  void on_get_chat_themes(Result<telegram_api::object_ptr<telegram_api::account_ChatThemes>> result);

  td_api::object_ptr<td_api::themeSettings> get_theme_settings_object(const ThemeSettings &settings) const;

  td_api::object_ptr<td_api::chatTheme> get_chat_theme_object(const ChatTheme &theme) const;

  td_api::object_ptr<td_api::chatThemes> get_chat_themes_object() const;

  static BaseTheme get_base_theme(const telegram_api::object_ptr<telegram_api::BaseTheme> &base_theme);

  ThemeSettings get_chat_theme_settings(telegram_api::object_ptr<telegram_api::themeSettings> settings);

  vector<Promise<td_api::object_ptr<td_api::chatThemes>>> pending_get_chat_themes_queries_;

  ChatThemes chat_themes_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
