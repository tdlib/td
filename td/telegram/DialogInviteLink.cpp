//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogInviteLink.h"

#include "td/telegram/ContactsManager.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

const CSlice DialogInviteLink::INVITE_LINK_URLS[12] = {
    "t.me/joinchat/", "telegram.me/joinchat/", "telegram.dog/joinchat/",
    "t.me/+",         "telegram.me/+",         "telegram.dog/+",
    "t.me/ ",         "telegram.me/ ",         "telegram.dog/ ",
    "t.me/%20",       "telegram.me/%20",       "telegram.dog/%20"};

DialogInviteLink::DialogInviteLink(tl_object_ptr<telegram_api::chatInviteExported> exported_invite) {
  if (exported_invite == nullptr) {
    return;
  }

  invite_link_ = std::move(exported_invite->link_);
  LOG_IF(ERROR, !is_valid_invite_link(invite_link_)) << "Unsupported invite link " << invite_link_;
  creator_user_id_ = UserId(exported_invite->admin_id_);
  if (!creator_user_id_.is_valid()) {
    LOG(ERROR) << "Receive invalid " << creator_user_id_ << " as creator of a link " << invite_link_;
    creator_user_id_ = UserId();
  }
  date_ = exported_invite->date_;
  if (date_ < 1000000000) {
    LOG(ERROR) << "Receive wrong date " << date_ << " as a creation date of a link " << invite_link_;
    date_ = 0;
  }
  if ((exported_invite->flags_ & telegram_api::chatInviteExported::EXPIRE_DATE_MASK) != 0) {
    expire_date_ = exported_invite->expire_date_;
    if (expire_date_ < 1000000000) {
      LOG(ERROR) << "Receive wrong date " << expire_date_ << " as an expire date of a link " << invite_link_;
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
  if ((exported_invite->flags_ & telegram_api::chatInviteExported::START_DATE_MASK) != 0) {
    edit_date_ = exported_invite->start_date_;
    if (edit_date_ < 1000000000) {
      LOG(ERROR) << "Receive wrong date " << edit_date_ << " as an edit date of a link " << invite_link_;
      edit_date_ = 0;
    }
  }
  is_revoked_ = exported_invite->revoked_;
  is_permanent_ = exported_invite->permanent_;

  if (is_permanent_ && (usage_limit_ > 0 || expire_date_ > 0 || edit_date_ > 0)) {
    LOG(ERROR) << "Receive wrong permanent " << *this;
    expire_date_ = 0;
    usage_limit_ = 0;
    edit_date_ = 0;
  }
}

bool DialogInviteLink::is_valid_invite_link(Slice invite_link) {
  return !get_dialog_invite_link_hash(invite_link).empty();
}

Slice DialogInviteLink::get_dialog_invite_link_hash(Slice invite_link) {
  auto lower_cased_invite_link_str = to_lower(invite_link);
  Slice lower_cased_invite_link = lower_cased_invite_link_str;
  size_t offset = 0;
  if (begins_with(lower_cased_invite_link, "https://")) {
    offset = 8;
  } else if (begins_with(lower_cased_invite_link, "http://")) {
    offset = 7;
  }
  lower_cased_invite_link.remove_prefix(offset);

  for (auto &url : INVITE_LINK_URLS) {
    if (begins_with(lower_cased_invite_link, url)) {
      Slice hash = invite_link.substr(url.size() + offset);
      hash.truncate(hash.find('#'));
      hash.truncate(hash.find('?'));
      return hash;
    }
  }
  return Slice();
}

td_api::object_ptr<td_api::chatInviteLink> DialogInviteLink::get_chat_invite_link_object(
    const ContactsManager *contacts_manager) const {
  CHECK(contacts_manager != nullptr);
  if (!is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::chatInviteLink>(
      invite_link_, contacts_manager->get_user_id_object(creator_user_id_, "get_chat_invite_link_object"), date_,
      edit_date_, expire_date_, usage_limit_, usage_count_, is_permanent_, is_revoked_);
}

bool operator==(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return lhs.invite_link_ == rhs.invite_link_ && lhs.creator_user_id_ == rhs.creator_user_id_ &&
         lhs.date_ == rhs.date_ && lhs.edit_date_ == rhs.edit_date_ && lhs.expire_date_ == rhs.expire_date_ &&
         lhs.usage_limit_ == rhs.usage_limit_ && lhs.usage_count_ == rhs.usage_count_ &&
         lhs.is_permanent_ == rhs.is_permanent_ && lhs.is_revoked_ == rhs.is_revoked_;
}

bool operator!=(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogInviteLink &invite_link) {
  return string_builder << "ChatInviteLink[" << invite_link.invite_link_ << " by " << invite_link.creator_user_id_
                        << " created at " << invite_link.date_ << " edited at " << invite_link.edit_date_
                        << " expiring at " << invite_link.expire_date_ << " used by " << invite_link.usage_count_
                        << " with usage limit " << invite_link.usage_limit_ << "]";
}

}  // namespace td
