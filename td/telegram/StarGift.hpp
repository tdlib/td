//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/PeerColorCollectible.hpp"
#include "td/telegram/StarGift.h"
#include "td/telegram/StarGiftAttribute.hpp"
#include "td/telegram/StarGiftBackground.hpp"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGift::store(StorerT &storer) const {
  CHECK(is_valid());
  Td *td = storer.context()->td().get_actor_unsafe();
  bool is_limited = availability_total_ != 0;
  bool has_default_sell_star_count = default_sell_star_count_ != star_count_ * 85 / 100;
  bool has_first_sale_date = first_sale_date_ != 0;
  bool has_last_sale_date = last_sale_date_ != 0;
  bool has_original_details = original_details_.is_valid();
  bool has_upgrade_star_count = upgrade_star_count_ != 0;
  bool has_owner_name = !owner_name_.empty();
  bool has_slug = !slug_.empty();
  bool has_owner_dialog_id = owner_dialog_id_.is_valid();
  bool has_owner_address = !owner_address_.empty();
  bool has_gift_address = !gift_address_.empty();
  bool has_resale_star_count = resale_star_count_ != 0;
  bool has_released_by_dialog_id = released_by_dialog_id_.is_valid();
  bool has_per_user_remains = per_user_remains_ != 0;
  bool has_per_user_total = per_user_total_ != 0;
  bool has_resale_ton_count = resale_ton_count_ != 0;
  bool has_regular_gift_id = regular_gift_id_ != 0;
  bool has_value = !value_currency_.empty();
  bool has_locked_until_date = locked_until_date_ != 0;
  bool has_theme_dialog_id = theme_dialog_id_.is_valid();
  bool has_host_dialog_id = host_dialog_id_.is_valid();
  bool has_peer_color = peer_color_ != nullptr;
  bool has_flags2 = true;
  bool has_background = background_ != nullptr;
  bool has_auction_start_date = auction_start_date_ != 0;
  bool has_upgrade_variants = upgrade_variants_ != 0;
  bool has_usd_value = value_usd_amount_ != 0;
  bool has_offer_min_star_count = offer_min_star_count_ != 0;
  bool has_craft_chance_permille = craft_chance_permille_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_limited);
  STORE_FLAG(has_default_sell_star_count);
  STORE_FLAG(has_first_sale_date);
  STORE_FLAG(has_last_sale_date);
  STORE_FLAG(is_for_birthday_);
  STORE_FLAG(is_unique_);
  STORE_FLAG(has_original_details);
  STORE_FLAG(false);  // has_owner_user_id
  STORE_FLAG(has_upgrade_star_count);
  STORE_FLAG(has_owner_name);
  STORE_FLAG(has_slug);
  STORE_FLAG(has_owner_dialog_id);
  STORE_FLAG(has_owner_address);
  STORE_FLAG(has_gift_address);
  STORE_FLAG(has_resale_star_count);
  STORE_FLAG(has_released_by_dialog_id);
  STORE_FLAG(is_premium_);
  STORE_FLAG(has_per_user_remains);
  STORE_FLAG(has_per_user_total);
  STORE_FLAG(resale_ton_only_);
  STORE_FLAG(has_resale_ton_count);
  STORE_FLAG(has_regular_gift_id);
  STORE_FLAG(has_value);
  STORE_FLAG(has_locked_until_date);
  STORE_FLAG(is_theme_available_);
  STORE_FLAG(has_theme_dialog_id);
  STORE_FLAG(has_host_dialog_id);
  STORE_FLAG(has_colors_);
  STORE_FLAG(has_peer_color);
  STORE_FLAG(has_flags2);
  END_STORE_FLAGS();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_auction_);
  STORE_FLAG(has_background);
  STORE_FLAG(has_auction_start_date);
  STORE_FLAG(has_upgrade_variants);
  STORE_FLAG(has_usd_value);
  STORE_FLAG(has_offer_min_star_count);
  STORE_FLAG(is_burned_);
  STORE_FLAG(is_crafted_);
  STORE_FLAG(has_craft_chance_permille);
  END_STORE_FLAGS();
  td::store(id_, storer);
  if (!is_unique_) {
    td->stickers_manager_->store_sticker(sticker_file_id_, false, storer, "StarGift");
    td::store(star_count_, storer);
  }
  if (is_limited) {
    td::store(availability_remains_, storer);
    td::store(availability_total_, storer);
  }
  if (has_default_sell_star_count) {
    td::store(default_sell_star_count_, storer);
  }
  if (has_first_sale_date) {
    td::store(first_sale_date_, storer);
  }
  if (has_last_sale_date) {
    td::store(last_sale_date_, storer);
  }
  if (has_upgrade_star_count) {
    td::store(upgrade_star_count_, storer);
  }
  if (is_unique_) {
    td::store(model_, storer);
    td::store(pattern_, storer);
    td::store(backdrop_, storer);
    if (has_original_details) {
      td::store(original_details_, storer);
    }
    td::store(title_, storer);
    if (has_owner_dialog_id) {
      td::store(owner_dialog_id_, storer);
    }
    if (has_owner_name) {
      td::store(owner_name_, storer);
    }
    td::store(num_, storer);
    td::store(unique_availability_issued_, storer);
    td::store(unique_availability_total_, storer);
    if (has_slug) {
      td::store(slug_, storer);
    }
    if (has_owner_address) {
      td::store(owner_address_, storer);
    }
    if (has_gift_address) {
      td::store(gift_address_, storer);
    }
    if (has_resale_star_count) {
      td::store(resale_star_count_, storer);
    }
    if (has_resale_ton_count) {
      td::store(resale_ton_count_, storer);
    }
    if (has_theme_dialog_id) {
      td::store(theme_dialog_id_, storer);
    }
  }
  if (has_released_by_dialog_id) {
    td::store(released_by_dialog_id_, storer);
  }
  if (has_per_user_remains) {
    td::store(per_user_remains_, storer);
  }
  if (has_per_user_total) {
    td::store(per_user_total_, storer);
  }
  if (has_regular_gift_id) {
    td::store(regular_gift_id_, storer);
  }
  if (has_value) {
    td::store(value_currency_, storer);
    td::store(value_amount_, storer);
  }
  if (has_locked_until_date) {
    td::store(locked_until_date_, storer);
  }
  if (has_host_dialog_id) {
    td::store(host_dialog_id_, storer);
  }
  if (has_peer_color) {
    td::store(peer_color_, storer);
  }
  if (is_auction_) {
    td::store(auction_slug_, storer);
    td::store(gifts_per_round_, storer);
  }
  if (has_background) {
    td::store(background_, storer);
  }
  if (has_auction_start_date) {
    td::store(auction_start_date_, storer);
  }
  if (has_upgrade_variants) {
    td::store(upgrade_variants_, storer);
  }
  if (has_usd_value) {
    td::store(value_usd_amount_, storer);
  }
  if (has_offer_min_star_count) {
    td::store(offer_min_star_count_, storer);
  }
  if (has_craft_chance_permille) {
    td::store(craft_chance_permille_, storer);
  }
}

template <class ParserT>
void StarGift::parse(ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  bool is_limited;
  bool has_default_sell_star_count;
  bool has_first_sale_date;
  bool has_last_sale_date;
  bool has_original_details;
  bool has_owner_user_id;
  bool has_upgrade_star_count;
  bool has_owner_name;
  bool has_slug;
  bool has_owner_dialog_id;
  bool has_owner_address;
  bool has_gift_address;
  bool has_resale_star_count;
  bool has_released_by_dialog_id;
  bool has_per_user_remains;
  bool has_per_user_total;
  bool has_resale_ton_count;
  bool has_regular_gift_id;
  bool has_value;
  bool has_locked_until_date;
  bool has_theme_dialog_id;
  bool has_host_dialog_id;
  bool has_peer_color;
  bool has_flags2;
  bool has_background = false;
  bool has_auction_start_date = false;
  bool has_upgrade_variants = false;
  bool has_usd_value = false;
  bool has_offer_min_star_count = false;
  bool has_craft_chance_permille = false;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_limited);
  PARSE_FLAG(has_default_sell_star_count);
  PARSE_FLAG(has_first_sale_date);
  PARSE_FLAG(has_last_sale_date);
  PARSE_FLAG(is_for_birthday_);
  PARSE_FLAG(is_unique_);
  PARSE_FLAG(has_original_details);
  PARSE_FLAG(has_owner_user_id);
  PARSE_FLAG(has_upgrade_star_count);
  PARSE_FLAG(has_owner_name);
  PARSE_FLAG(has_slug);
  PARSE_FLAG(has_owner_dialog_id);
  PARSE_FLAG(has_owner_address);
  PARSE_FLAG(has_gift_address);
  PARSE_FLAG(has_resale_star_count);
  PARSE_FLAG(has_released_by_dialog_id);
  PARSE_FLAG(is_premium_);
  PARSE_FLAG(has_per_user_remains);
  PARSE_FLAG(has_per_user_total);
  PARSE_FLAG(resale_ton_only_);
  PARSE_FLAG(has_resale_ton_count);
  PARSE_FLAG(has_regular_gift_id);
  PARSE_FLAG(has_value);
  PARSE_FLAG(has_locked_until_date);
  PARSE_FLAG(is_theme_available_);
  PARSE_FLAG(has_theme_dialog_id);
  PARSE_FLAG(has_host_dialog_id);
  PARSE_FLAG(has_colors_);
  PARSE_FLAG(has_peer_color);
  PARSE_FLAG(has_flags2);
  END_PARSE_FLAGS();
  if (has_flags2) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_auction_);
    PARSE_FLAG(has_background);
    PARSE_FLAG(has_auction_start_date);
    PARSE_FLAG(has_upgrade_variants);
    PARSE_FLAG(has_usd_value);
    PARSE_FLAG(has_offer_min_star_count);
    PARSE_FLAG(is_burned_);
    PARSE_FLAG(is_crafted_);
    PARSE_FLAG(has_craft_chance_permille);
    END_PARSE_FLAGS();
  }
  td::parse(id_, parser);
  if (!is_unique_) {
    sticker_file_id_ = td->stickers_manager_->parse_sticker(false, parser);
    td::parse(star_count_, parser);
  }
  if (is_limited) {
    td::parse(availability_remains_, parser);
    td::parse(availability_total_, parser);
  }
  if (has_default_sell_star_count) {
    td::parse(default_sell_star_count_, parser);
  } else {
    default_sell_star_count_ = star_count_ * 85 / 100;
  }
  if (has_first_sale_date) {
    td::parse(first_sale_date_, parser);
  }
  if (has_last_sale_date) {
    td::parse(last_sale_date_, parser);
  }
  if (has_upgrade_star_count) {
    td::parse(upgrade_star_count_, parser);
  }
  if (is_unique_) {
    td::parse(model_, parser);
    td::parse(pattern_, parser);
    td::parse(backdrop_, parser);
    if (has_original_details) {
      td::parse(original_details_, parser);
    }
    td::parse(title_, parser);
    if (has_owner_user_id) {
      UserId owner_user_id;
      td::parse(owner_user_id, parser);
      owner_dialog_id_ = DialogId(owner_user_id);
    }
    if (has_owner_dialog_id) {
      td::parse(owner_dialog_id_, parser);
    }
    if (has_owner_name) {
      td::parse(owner_name_, parser);
    }
    td::parse(num_, parser);
    td::parse(unique_availability_issued_, parser);
    td::parse(unique_availability_total_, parser);
    if (has_slug) {
      td::parse(slug_, parser);
    }
    if (has_owner_address) {
      td::parse(owner_address_, parser);
    }
    if (has_gift_address) {
      td::parse(gift_address_, parser);
    }
    if (has_resale_star_count) {
      td::parse(resale_star_count_, parser);
    }
    if (has_resale_ton_count) {
      td::parse(resale_ton_count_, parser);
    }
    if (has_theme_dialog_id) {
      td::parse(theme_dialog_id_, parser);
    }
  }
  if (has_released_by_dialog_id) {
    td::parse(released_by_dialog_id_, parser);
  }
  if (has_per_user_remains) {
    td::parse(per_user_remains_, parser);
  }
  if (has_per_user_total) {
    td::parse(per_user_total_, parser);
  }
  if (has_regular_gift_id) {
    td::parse(regular_gift_id_, parser);
  }
  if (has_value) {
    td::parse(value_currency_, parser);
    td::parse(value_amount_, parser);
  }
  if (has_locked_until_date) {
    td::parse(locked_until_date_, parser);
  }
  if (has_host_dialog_id) {
    td::parse(host_dialog_id_, parser);
  }
  if (has_peer_color) {
    td::parse(peer_color_, parser);
  }
  if (is_auction_) {
    td::parse(auction_slug_, parser);
    td::parse(gifts_per_round_, parser);
  }
  if (has_background) {
    td::parse(background_, parser);
  }
  if (has_auction_start_date) {
    td::parse(auction_start_date_, parser);
  }
  if (has_upgrade_variants) {
    td::parse(upgrade_variants_, parser);
  }
  if (has_usd_value) {
    td::parse(value_usd_amount_, parser);
  }
  if (has_offer_min_star_count) {
    td::parse(offer_min_star_count_, parser);
  }
  if (has_craft_chance_permille) {
    td::parse(craft_chance_permille_, parser);
  }
}

}  // namespace td
