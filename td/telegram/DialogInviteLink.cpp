//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogInviteLink.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"

namespace td {

DialogInviteLink::DialogInviteLink(tl_object_ptr<telegram_api::chatInviteExported> exported_invite) {
  if (exported_invite == nullptr) {
    return;
  }

  invite_link_ = std::move(exported_invite->link_);
  administrator_user_id_ = UserId(exported_invite->admin_id_);
  if (!administrator_user_id_.is_valid()) {
    LOG(ERROR) << "Receive invalid " << administrator_user_id_ << " as creator of a link " << invite_link_;
    administrator_user_id_ = UserId();
  }
  date_ = exported_invite->date_;
  if (date_ < 1000000000) {
    LOG(ERROR) << "Receive wrong date " << date_ << " as creation date of a link " << invite_link_;
    date_ = 0;
  }
  if ((exported_invite->flags_ & telegram_api::chatInviteExported::EXPIRE_DATE_MASK) != 0) {
    expire_date_ = exported_invite->expire_date_;
    if (expire_date_ < 0) {
      LOG(ERROR) << "Receive wrong date " << expire_date_ << " as expire date of a link " << invite_link_;
      expire_date_ = 0;
    }
  }
  if ((exported_invite->flags_ & telegram_api::chatInviteExported::USAGE_LIMIT_MASK) != 0) {
    usage_limit_ = exported_invite->usage_limit_;
    if (usage_limit_ < 0) {
      LOG(ERROR) << "Receive wrong usage limit " << usage_limit_ << " for a link " << invite_link_;
      usage_limit_ = 0;
    }
  }
  if ((exported_invite->flags_ & telegram_api::chatInviteExported::USAGE_MASK) != 0) {
    usage_count_ = exported_invite->usage_;
    if (usage_count_ < 0) {
      LOG(ERROR) << "Receive wrong usage count " << usage_count_ << " for a link " << invite_link_;
      usage_count_ = 0;
    }
  }
}

bool DialogInviteLink::is_expired() const {
  return (expire_date_ != 0 && G()->unix_time() >= expire_date_) || (usage_limit_ != 0 && usage_count_ >= usage_limit_);
}

int32 DialogInviteLink::get_expire_time() const {
  if (expire_date_ == 0) {
    return 0;
  }
  if (usage_limit_ != 0 && usage_count_ >= usage_limit_) {
    // already expired
    return 0;
  }
  return td::max(expire_date_ - G()->unix_time(), 0);
}

td_api::object_ptr<td_api::chatInviteLink> DialogInviteLink::get_chat_invite_link_object(
    const ContactsManager *contacts_manager) const {
  CHECK(contacts_manager != nullptr);
  if (!is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::chatInviteLink>(
      invite_link_, contacts_manager->get_user_id_object(administrator_user_id_, "get_chat_invite_link_object"), date_,
      expire_date_, usage_limit_, usage_count_, is_expired(), is_revoked_);
}

bool operator==(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return lhs.invite_link_ == rhs.invite_link_ && lhs.administrator_user_id_ == rhs.administrator_user_id_ &&
         lhs.date_ == rhs.date_ && lhs.expire_date_ == rhs.expire_date_ && lhs.usage_limit_ == rhs.usage_limit_ &&
         lhs.usage_count_ == rhs.usage_count_ && lhs.is_revoked_ == rhs.is_revoked_;
}

bool operator!=(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogInviteLink &invite_link) {
  return string_builder << "ChatInviteLink[" << invite_link.invite_link_ << " by " << invite_link.administrator_user_id_
                        << " created at " << invite_link.date_ << " expiring at " << invite_link.expire_date_
                        << " used by " << invite_link.usage_count_ << " with usage limit " << invite_link.usage_limit_
                        << "]";
}

}  // namespace td
