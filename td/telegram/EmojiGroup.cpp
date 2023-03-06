//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/EmojiGroup.h"

#include "td/telegram/StickersManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/Time.h"

namespace td {

EmojiGroup::EmojiGroup(telegram_api::object_ptr<telegram_api::emojiGroup> &&emoji_group)
    : title_(std::move(emoji_group->title_))
    , icon_custom_emoji_id_(emoji_group->icon_emoji_id_)
    , emojis_(std::move(emoji_group->emoticons_)) {
}

td_api::object_ptr<td_api::emojiCategory> EmojiGroup::get_emoji_category_object(
    StickersManager *stickers_manager) const {
  return td_api::make_object<td_api::emojiCategory>(
      title_, stickers_manager->get_custom_emoji_sticker_object(icon_custom_emoji_id_), vector<string>(emojis_));
}

EmojiGroupList::EmojiGroupList(string used_language_codes, int32 hash,
                               vector<telegram_api::object_ptr<telegram_api::emojiGroup>> &&emoji_groups)
    : used_language_codes_(std::move(used_language_codes))
    , hash_(hash)
    , emoji_groups_(transform(std::move(emoji_groups),
                              [](telegram_api::object_ptr<telegram_api::emojiGroup> &&emoji_group) {
                                return EmojiGroup(std::move(emoji_group));
                              }))
    , next_reload_time_(Time::now() + 3600) {
}

td_api::object_ptr<td_api::emojiCategories> EmojiGroupList::get_emoji_categories_object(
    StickersManager *stickers_manager) const {
  auto emoji_categories = transform(emoji_groups_, [stickers_manager](const EmojiGroup &emoji_group) {
    return emoji_group.get_emoji_category_object(stickers_manager);
  });
  td::remove_if(emoji_categories, [](const td_api::object_ptr<td_api::emojiCategory> &emoji_category) {
    if (emoji_category->icon_ == nullptr) {
      LOG(ERROR) << "Failed to get icon for emoji category " << emoji_category->name_;
      return true;
    }
    return false;
  });
  return td_api::make_object<td_api::emojiCategories>(std::move(emoji_categories));
}

bool EmojiGroupList::is_expired() const {
  return next_reload_time_ < Time::now();
}

void EmojiGroupList::update_next_reload_time() {
  next_reload_time_ = Time::now() + 3600;
}

vector<CustomEmojiId> EmojiGroupList::get_icon_custom_emoji_ids() const {
  return transform(emoji_groups_, [](const EmojiGroup &emoji_group) { return emoji_group.get_icon_custom_emoji_id(); });
}

}  // namespace td
