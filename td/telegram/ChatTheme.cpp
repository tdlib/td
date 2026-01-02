//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ChatTheme.h"

#include "td/telegram/Dependencies.h"

#include "td/utils/logging.h"

namespace td {

ChatTheme::ChatTheme(Td *td, telegram_api::object_ptr<telegram_api::ChatTheme> theme) {
  if (theme == nullptr) {
    return;
  }
  switch (theme->get_id()) {
    case telegram_api::chatTheme::ID: {
      auto chat_theme = telegram_api::move_object_as<telegram_api::chatTheme>(theme);
      if (chat_theme->emoticon_.empty()) {
        return;
      }
      type_ = Type::Emoji;
      emoji_ = std::move(chat_theme->emoticon_);
      break;
    }
    case telegram_api::chatThemeUniqueGift::ID: {
      auto chat_theme = telegram_api::move_object_as<telegram_api::chatThemeUniqueGift>(theme);
      StarGift star_gift(td, std::move(chat_theme->gift_), true);
      if (!star_gift.is_valid() || !star_gift.is_unique()) {
        LOG(ERROR) << "Receive chat theme with " << star_gift;
        return;
      }

      bool was_light = false;
      bool was_dark = false;
      for (auto &settings : chat_theme->theme_settings_) {
        auto theme_settings = ThemeSettings(td, std::move(settings));
        if (theme_settings.is_empty()) {
          LOG(ERROR) << "Receive empty chat theme settings for " << star_gift;
          continue;
        }
        if (theme_settings.are_dark()) {
          if (!was_dark) {
            was_dark = true;
            dark_theme_ = std::move(theme_settings);
          } else {
            LOG(ERROR) << "Receive duplicate dark theme for " << star_gift;
          }
        } else {
          if (!was_light) {
            was_light = true;
            light_theme_ = std::move(theme_settings);
          } else {
            LOG(ERROR) << "Receive duplicate light theme for " << star_gift;
          }
        }
      }
      if (light_theme_.is_empty() || dark_theme_.is_empty()) {
        LOG(ERROR) << "Receive chat theme with invalid themes";
        *this = {};
        return;
      }

      type_ = Type::Gift;
      star_gift_ = std::move(star_gift);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

ChatTheme ChatTheme::emoji(string &&emoji) {
  ChatTheme result;
  if (!emoji.empty()) {
    result.type_ = Type::Emoji;
    result.emoji_ = std::move(emoji);
  }
  return result;
}

td_api::object_ptr<td_api::giftChatTheme> ChatTheme::get_gift_chat_theme_object(Td *td) const {
  CHECK(type_ == Type::Gift);
  return td_api::make_object<td_api::giftChatTheme>(star_gift_.get_upgraded_gift_object(td),
                                                    light_theme_.get_theme_settings_object(td),
                                                    dark_theme_.get_theme_settings_object(td));
}

td_api::object_ptr<td_api::ChatTheme> ChatTheme::get_chat_theme_object(Td *td) const {
  switch (type_) {
    case Type::Default:
      return nullptr;
    case Type::Emoji:
      return td_api::make_object<td_api::chatThemeEmoji>(emoji_);
    case Type::Gift:
      return td_api::make_object<td_api::chatThemeGift>(get_gift_chat_theme_object(td));
    default:
      UNREACHABLE();
  }
}

void ChatTheme::add_dependencies(Dependencies &dependencies) const {
  if (type_ == Type::Gift) {
    star_gift_.add_dependencies(dependencies);
  }
}

bool operator==(const ChatTheme &lhs, const ChatTheme &rhs) {
  return lhs.type_ == rhs.type_ && lhs.emoji_ == rhs.emoji_ && lhs.star_gift_ == rhs.star_gift_ &&
         lhs.light_theme_ == rhs.light_theme_ && lhs.dark_theme_ == rhs.dark_theme_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChatTheme &chat_theme) {
  switch (chat_theme.type_) {
    case ChatTheme::Type::Default:
      return string_builder << "default";
    case ChatTheme::Type::Emoji:
      return string_builder << "emoji " << chat_theme.emoji_;
    case ChatTheme::Type::Gift:
      return string_builder << chat_theme.star_gift_;
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
