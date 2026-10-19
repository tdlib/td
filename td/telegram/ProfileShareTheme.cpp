//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ProfileShareTheme.h"

#include "td/telegram/Dependencies.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

ProfileShareTheme::ProfileShareTheme(Td *td, telegram_api::object_ptr<telegram_api::ShareTheme> theme) {
  if (theme == nullptr) {
    return;
  }
  switch (theme->get_id()) {
    case telegram_api::shareThemeDefault::ID:
      break;
    case telegram_api::shareThemeEmojiBackdrop::ID: {
      auto share_theme = telegram_api::move_object_as<telegram_api::shareThemeEmojiBackdrop>(theme);
      vector<ThemeEmoji> emojis;
      for (auto &emoji_ptr : share_theme->emojis_) {
        CHECK(emoji_ptr != nullptr);
        switch (emoji_ptr->get_id()) {
          case telegram_api::reactionEmoji::ID: {
            auto emoji = telegram_api::move_object_as<telegram_api::reactionEmoji>(emoji_ptr);
            if (emoji->emoticon_.empty()) {
              LOG(ERROR) << "Receive empty emoji in a profile share theme";
              break;
            }
            emojis.push_back({std::move(emoji->emoticon_), CustomEmojiId()});
            break;
          }
          case telegram_api::reactionCustomEmoji::ID: {
            auto emoji = telegram_api::move_object_as<telegram_api::reactionCustomEmoji>(emoji_ptr);
            CustomEmojiId custom_emoji_id(emoji->document_id_);
            if (!custom_emoji_id.is_valid()) {
              LOG(ERROR) << "Receive invalid custom emoji in a profile share theme";
              break;
            }
            emojis.push_back({string(), custom_emoji_id});
            break;
          }
          default:
            LOG(ERROR) << "Receive " << to_string(emoji_ptr);
            break;
        }
      }
      if (emojis.size() > MAX_EMOJI_COUNT) {
        LOG(ERROR) << "Receive " << emojis.size() << " emoji in a profile share theme";
        return;
      }
      bool use_default_background =
          (share_theme->flags_ & telegram_api::shareThemeEmojiBackdrop::BACKDROP_ID_MASK) == 0;
      if (emojis.empty() && use_default_background) {
        LOG(ERROR) << "Receive empty profile share theme";
        return;
      }
      type_ = Type::Emoji;
      emojis_ = std::move(emojis);
      use_default_background_ = use_default_background;
      backdrop_id_ = share_theme->backdrop_id_;
      break;
    }
    case telegram_api::shareThemeGift::ID: {
      auto share_theme = telegram_api::move_object_as<telegram_api::shareThemeGift>(theme);
      StarGift star_gift(td, std::move(share_theme->stargift_), true);
      if (!star_gift.is_valid() || !star_gift.is_unique()) {
        LOG(ERROR) << "Receive profile share theme with " << star_gift;
        return;
      }
      type_ = Type::Gift;
      star_gift_ = std::move(star_gift);
      break;
    }
    case telegram_api::inputShareThemeGift::ID:
      LOG(ERROR) << "Receive " << to_string(theme);
      break;
    default:
      UNREACHABLE();
  }
}

unique_ptr<ProfileShareTheme> ProfileShareTheme::get_profile_share_theme(
    Td *td, telegram_api::object_ptr<telegram_api::ShareTheme> &&theme) {
  if (theme == nullptr) {
    return nullptr;
  }
  auto result = td::make_unique<ProfileShareTheme>(td, std::move(theme));
  if (result->is_default()) {
    return nullptr;
  }
  return result;
}

td_api::object_ptr<td_api::ProfileShareTheme> ProfileShareTheme::get_profile_share_theme_object(Td *td) const {
  switch (type_) {
    case Type::Default:
      return nullptr;
    case Type::Emoji: {
      auto emojis = transform(emojis_, [](const ThemeEmoji &emoji) -> td_api::object_ptr<td_api::EmojiType> {
        if (emoji.custom_emoji_id_.is_valid()) {
          return td_api::make_object<td_api::emojiTypeCustomEmoji>(emoji.custom_emoji_id_.get());
        }
        return td_api::make_object<td_api::emojiTypeRegular>(emoji.emoji_);
      });
      return td_api::make_object<td_api::profileShareThemeEmoji>(std::move(emojis), use_default_background_,
                                                                 backdrop_id_);
    }
    case Type::Gift:
      return td_api::make_object<td_api::profileShareThemeUpgradedGift>(star_gift_.get_upgraded_gift_object(td));
    default:
      UNREACHABLE();
  }
}

void ProfileShareTheme::add_dependencies(Dependencies &dependencies) const {
  if (type_ == Type::Gift) {
    star_gift_.add_dependencies(dependencies);
  }
}

bool operator==(const ProfileShareTheme &lhs, const ProfileShareTheme &rhs) {
  return lhs.type_ == rhs.type_ && lhs.emojis_ == rhs.emojis_ &&
         lhs.use_default_background_ == rhs.use_default_background_ && lhs.backdrop_id_ == rhs.backdrop_id_ &&
         lhs.star_gift_ == rhs.star_gift_;
}

bool operator!=(const ProfileShareTheme &lhs, const ProfileShareTheme &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
