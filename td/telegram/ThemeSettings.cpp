//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ThemeSettings.h"

#include "td/telegram/BackgroundType.h"

namespace td {

ThemeSettings::ThemeSettings(Td *td, telegram_api::object_ptr<telegram_api::themeSettings> settings) {
  if (settings != nullptr && settings->message_colors_.size() <= 4) {
    accent_color_ = settings->accent_color_;
    bool has_outbox_accent_color = (settings->flags_ & telegram_api::themeSettings::OUTBOX_ACCENT_COLOR_MASK) != 0;
    message_accent_color_ = (has_outbox_accent_color ? settings->outbox_accent_color_ : accent_color_);
    background_info_ = BackgroundInfo(td, std::move(settings->wallpaper_), true, true /*gift themes are pattern-like*/);
    base_theme_ = get_base_theme(settings->base_theme_);
    message_colors_ = std::move(settings->message_colors_);
    animate_message_colors_ = settings->message_colors_animated_;
  }
}

td_api::object_ptr<td_api::themeSettings> ThemeSettings::get_theme_settings_object(Td *td) const {
  return td_api::make_object<td_api::themeSettings>(
      get_built_in_theme_object(base_theme_), accent_color_, background_info_.get_background_object(td),
      BackgroundFill::get_background_fill_object(message_colors_), animate_message_colors_, message_accent_color_);
}

bool operator==(const ThemeSettings &lhs, const ThemeSettings &rhs) {
  return lhs.accent_color_ == rhs.accent_color_ && lhs.message_accent_color_ == rhs.message_accent_color_ &&
         lhs.background_info_ == rhs.background_info_ && lhs.base_theme_ == rhs.base_theme_ &&
         lhs.message_colors_ == rhs.message_colors_ && lhs.animate_message_colors_ == rhs.animate_message_colors_;
}

}  // namespace td
