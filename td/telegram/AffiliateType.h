//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class AffiliateType {
  DialogId dialog_id_;

  explicit AffiliateType(DialogId dialog_id) : dialog_id_(dialog_id) {
  }

 public:
  static Result<AffiliateType> get_affiliate_type(Td *td, const td_api::object_ptr<td_api::AffiliateType> &type);

  telegram_api::object_ptr<telegram_api::InputPeer> get_input_peer(Td *td) const;

  td_api::object_ptr<td_api::AffiliateType> get_affiliate_type_object(Td *td) const;
};

}  // namespace td
