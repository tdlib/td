//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogAdministrator.h"

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/UserManager.h"

namespace td {

DialogAdministrator::DialogAdministrator(UserId user_id, const DialogParticipantStatus &status)
    : user_id_(user_id)
    , rank_(status.get_rank())
    , is_creator_(status.is_creator())
    , can_be_edited_(status.can_be_edited()) {
  CHECK(status.is_administrator());
}

td_api::object_ptr<td_api::chatAdministrator> DialogAdministrator::get_chat_administrator_object(
    const UserManager *user_manager) const {
  CHECK(user_manager != nullptr);
  CHECK(user_id_.is_valid());
  return td_api::make_object<td_api::chatAdministrator>(
      user_manager->get_user_id_object(user_id_, "get_chat_administrator_object"), rank_, is_creator_, can_be_edited_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogAdministrator &administrator) {
  return string_builder << "ChatAdministrator[" << administrator.user_id_ << ", title = " << administrator.rank_
                        << ", is_owner = " << administrator.is_creator_
                        << ", can_be_edited = " << administrator.can_be_edited_ << ']';
}

}  // namespace td
