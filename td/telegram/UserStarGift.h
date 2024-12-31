//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

namespace td {

class Td;

class UserStarGift {
  UserId sender_user_id_;
  StarGift gift_;
  FormattedText message_;
  MessageId message_id_;
  int64 convert_star_count_ = 0;
  int64 upgrade_star_count_ = 0;
  int64 transfer_star_count_ = 0;
  int32 date_ = 0;
  int32 can_export_at_ = 0;
  bool is_name_hidden_ = false;
  bool is_saved_ = false;
  bool can_upgrade_ = false;
  bool can_transfer_ = false;
  bool was_refunded_ = false;

 public:
  UserStarGift(Td *td, telegram_api::object_ptr<telegram_api::userStarGift> &&gift, bool is_me);

  bool is_valid() const {
    return gift_.is_valid() && (is_name_hidden_ || sender_user_id_ != UserId());
  }

  td_api::object_ptr<td_api::userGift> get_user_gift_object(Td *td) const;
};

}  // namespace td
