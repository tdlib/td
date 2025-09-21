//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/StarGiftAttribute.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class Td;

class StarGift {
  int64 id_ = 0;
  DialogId released_by_dialog_id_;
  bool is_premium_ = false;

  FileId sticker_file_id_;
  int64 star_count_ = 0;
  int64 default_sell_star_count_ = 0;
  int64 upgrade_star_count_ = 0;
  int32 availability_remains_ = 0;
  int32 availability_total_ = 0;
  int32 first_sale_date_ = 0;
  int32 last_sale_date_ = 0;
  int32 per_user_remains_ = 0;
  int32 per_user_total_ = 0;
  int32 locked_until_date_ = 0;

  bool is_for_birthday_ = false;

  bool is_unique_ = false;
  bool resale_ton_only_ = false;
  bool is_theme_available_ = false;
  StarGiftAttributeSticker model_;
  StarGiftAttributeSticker pattern_;
  StarGiftAttributeBackdrop backdrop_;
  StarGiftAttributeOriginalDetails original_details_;
  string title_;
  string slug_;
  DialogId owner_dialog_id_;
  string owner_address_;
  string owner_name_;
  string gift_address_;
  int32 num_ = 0;
  int32 unique_availability_issued_ = 0;
  int32 unique_availability_total_ = 0;
  int64 resale_star_count_ = 0;
  int64 resale_ton_count_ = 0;
  int64 regular_gift_id_ = 0;
  string value_currency_;
  int64 value_amount_ = 0;
  DialogId theme_dialog_id_;

  friend bool operator==(const StarGift &lhs, const StarGift &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StarGift &star_gift);

 public:
  StarGift() = default;

  StarGift(Td *td, telegram_api::object_ptr<telegram_api::StarGift> &&star_gift_ptr, bool allow_unique_gift);

  bool is_valid() const {
    return id_ != 0 && (is_unique_ ? model_.is_valid() && pattern_.is_valid() && backdrop_.is_valid()
                                   : sticker_file_id_.is_valid());
  }

  bool is_unique() const {
    return is_unique_;
  }

  int64 get_id() const {
    return id_;
  }

  int64 get_star_count() const {
    CHECK(!is_unique_);
    return star_count_;
  }

  int64 get_upgrade_star_count() const {
    CHECK(!is_unique_);
    return upgrade_star_count_;
  }

  static void fix_availability(int32 &total, int32 &remains);

  static td_api::object_ptr<td_api::giftPurchaseLimits> get_gift_purchase_limits_object(int32 total, int32 remains);

  td_api::object_ptr<td_api::gift> get_gift_object(const Td *td) const;

  td_api::object_ptr<td_api::upgradedGift> get_upgraded_gift_object(Td *td) const;

  td_api::object_ptr<td_api::giftForResale> get_gift_for_resale_object(Td *td) const;

  td_api::object_ptr<td_api::SentGift> get_sent_gift_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGift &lhs, const StarGift &rhs);

inline bool operator!=(const StarGift &lhs, const StarGift &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGift &star_gift);

}  // namespace td
