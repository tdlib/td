//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundInfo.h"
#include "td/telegram/BaseTheme.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

struct ThemeSettings {
  int32 accent_color = 0;
  int32 message_accent_color = 0;
  BackgroundInfo background_info;
  BaseTheme base_theme = BaseTheme::Classic;
  vector<int32> message_colors;
  bool animate_message_colors = false;

  ThemeSettings() = default;

  ThemeSettings(Td *td, telegram_api::object_ptr<telegram_api::themeSettings> settings);

  td_api::object_ptr<td_api::themeSettings> get_theme_settings_object(Td *td) const;

  bool is_empty() const {
    return message_colors.empty();
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ThemeSettings &lhs, const ThemeSettings &rhs);

inline bool operator!=(const ThemeSettings &lhs, const ThemeSettings &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
