//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class Td;

class ProfileShareTheme {
  static constexpr size_t MAX_EMOJI_COUNT = 6;

  enum class Type : int32 { Default, Emoji, Gift };
  Type type_ = Type::Default;

  struct ThemeEmoji {
    string emoji_;                   // for regular emoji
    CustomEmojiId custom_emoji_id_;  // for custom emoji

    bool operator==(const ThemeEmoji &other) const {
      return emoji_ == other.emoji_ && custom_emoji_id_ == other.custom_emoji_id_;
    }

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };
  vector<ThemeEmoji> emojis_;            // for Emoji
  bool use_default_background_ = false;  // for Emoji
  int32 backdrop_id_ = 0;                // for Emoji

  StarGift star_gift_;  // for Gift

  friend bool operator==(const ProfileShareTheme &lhs, const ProfileShareTheme &rhs);

 public:
  ProfileShareTheme() = default;

  ProfileShareTheme(Td *td, telegram_api::object_ptr<telegram_api::ShareTheme> theme);

  static unique_ptr<ProfileShareTheme> get_profile_share_theme(
      Td *td, telegram_api::object_ptr<telegram_api::ShareTheme> &&theme);

  td_api::object_ptr<td_api::ProfileShareTheme> get_profile_share_theme_object(Td *td) const;

  bool is_default() const {
    return type_ == Type::Default;
  }

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ProfileShareTheme &lhs, const ProfileShareTheme &rhs);

bool operator!=(const ProfileShareTheme &lhs, const ProfileShareTheme &rhs);

}  // namespace td
