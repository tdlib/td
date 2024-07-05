//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ThemeSettings.h"

namespace td {

ThemeSettings::ThemeSettings(Td *td, telegram_api::object_ptr<telegram_api::themeSettings> settings) {
  if (settings != nullptr && !settings->message_colors_.empty() && settings->message_colors_.size() <= 4) {
    accent_color = settings->accent_color_;
    bool has_outbox_accent_color = (settings->flags_ & telegram_api::themeSettings::OUTBOX_ACCENT_COLOR_MASK) != 0;
    message_accent_color = (has_outbox_accent_color ? settings->outbox_accent_color_ : accent_color);
    background_info = BackgroundInfo(td, std::move(settings->wallpaper_), true);
    base_theme = get_base_theme(settings->base_theme_);
    message_colors = std::move(settings->message_colors_);
    animate_message_colors = settings->message_colors_animated_;
  }
}

td_api::object_ptr<td_api::themeSettings> ThemeSettings::get_theme_settings_object(Td *td) const {
  auto fill = [&]() -> td_api::object_ptr<td_api::BackgroundFill> {
    if (message_colors.size() >= 3) {
      return td_api::make_object<td_api::backgroundFillFreeformGradient>(vector<int32>(message_colors));
    }
    CHECK(!message_colors.empty());
    if (message_colors.size() == 1 || message_colors[0] == message_colors[1]) {
      return td_api::make_object<td_api::backgroundFillSolid>(message_colors[0]);
    }
    return td_api::make_object<td_api::backgroundFillGradient>(message_colors[1], message_colors[0], 0);
  }();

  // ignore base_theme for now
  return td_api::make_object<td_api::themeSettings>(accent_color, background_info.get_background_object(td),
                                                    std::move(fill), animate_message_colors, message_accent_color);
}

bool operator==(const ThemeSettings &lhs, const ThemeSettings &rhs) {
  return lhs.accent_color == rhs.accent_color && lhs.message_accent_color == rhs.message_accent_color &&
         lhs.background_info == rhs.background_info && lhs.base_theme == rhs.base_theme &&
         lhs.message_colors == rhs.message_colors && lhs.animate_message_colors == rhs.animate_message_colors;
}

}  // namespace td
