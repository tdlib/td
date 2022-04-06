//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotMenuButton.h"

namespace td {

unique_ptr<BotMenuButton> BotMenuButton::get_bot_menu_button(
    telegram_api::object_ptr<telegram_api::BotMenuButton> &&bot_menu_button) {
  CHECK(bot_menu_button != nullptr);
  switch (bot_menu_button->get_id()) {
    case telegram_api::botMenuButtonCommands::ID:
      return nullptr;
    case telegram_api::botMenuButtonDefault::ID:
      return td::make_unique<BotMenuButton>(string(), "default");
    case telegram_api::botMenuButton::ID: {
      auto button = telegram_api::move_object_as<telegram_api::botMenuButton>(bot_menu_button);
      if (button->text_.empty()) {
        LOG(ERROR) << "Receive bot menu button with empty text: " << to_string(button);
        return nullptr;
      }
      return td::make_unique<BotMenuButton>(std::move(button->text_), std::move(button->url_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::botMenuButton> BotMenuButton::get_bot_menu_button_object() const {
  return td_api::make_object<td_api::botMenuButton>(text_, url_);
}

bool operator==(const BotMenuButton &lhs, const BotMenuButton &rhs) {
  return lhs.text_ == rhs.text_ && lhs.url_ == rhs.url_;
}

bool operator==(const unique_ptr<BotMenuButton> &lhs, const unique_ptr<BotMenuButton> &rhs) {
  if (lhs == nullptr) {
    return rhs == nullptr;
  }
  return rhs != nullptr && *lhs == *rhs;
}

}  // namespace td
