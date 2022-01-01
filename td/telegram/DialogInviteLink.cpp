//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogInviteLink.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/LinkManager.h"

#include "td/utils/logging.h"

namespace td {

DialogInviteLink::DialogInviteLink(tl_object_ptr<telegram_api::chatInviteExported> exported_invite) {
  if (exported_invite == nullptr) {
    return;
  }

  invite_link_ = std::move(exported_invite->link_);
  title_ = std::move(exported_invite->title_);
  creator_user_id_ = UserId(exported_invite->admin_id_);
  date_ = exported_invite->date_;
  expire_date_ = exported_invite->expire_date_;
  usage_limit_ = exported_invite->usage_limit_;
  usage_count_ = exported_invite->usage_;
  edit_date_ = exported_invite->start_date_;
  request_count_ = exported_invite->requested_;
  creates_join_request_ = exported_invite->request_needed_;
  is_revoked_ = exported_invite->revoked_;
  is_permanent_ = exported_invite->permanent_;

  LOG_IF(ERROR, !is_valid_invite_link(invite_link_)) << "Unsupported invite link " << invite_link_;
  if (!creator_user_id_.is_valid()) {
    LOG(ERROR) << "Receive invalid " << creator_user_id_ << " as creator of a link " << invite_link_;
    creator_user_id_ = UserId();
  }
  if (date_ != 0 && date_ < 1000000000) {
    LOG(ERROR) << "Receive wrong date " << date_ << " as a creation date of a link " << invite_link_;
    date_ = 0;
  }
  if (expire_date_ != 0 && expire_date_ < 1000000000) {
    LOG(ERROR) << "Receive wrong date " << expire_date_ << " as an expire date of a link " << invite_link_;
    expire_date_ = 0;
  }
  if (usage_limit_ < 0) {
    LOG(ERROR) << "Receive wrong usage limit " << usage_limit_ << " for a link " << invite_link_;
    usage_limit_ = 0;
  }
  if (usage_count_ < 0) {
    LOG(ERROR) << "Receive wrong usage count " << usage_count_ << " for a link " << invite_link_;
    usage_count_ = 0;
  }
  if (edit_date_ != 0 && edit_date_ < 1000000000) {
    LOG(ERROR) << "Receive wrong date " << edit_date_ << " as an edit date of a link " << invite_link_;
    edit_date_ = 0;
  }
  if (request_count_ < 0) {
    LOG(ERROR) << "Receive wrong pending join request count " << request_count_ << " for a link " << invite_link_;
    request_count_ = 0;
  }

  if (is_permanent_ && (!title_.empty() || expire_date_ > 0 || usage_limit_ > 0 || edit_date_ > 0 ||
                        request_count_ > 0 || creates_join_request_)) {
    LOG(ERROR) << "Receive wrong permanent " << *this;
    title_.clear();
    expire_date_ = 0;
    usage_limit_ = 0;
    edit_date_ = 0;
    request_count_ = 0;
    creates_join_request_ = false;
  }
  if (creates_join_request_ && usage_limit_ > 0) {
    LOG(ERROR) << "Receive wrong permanent " << *this;
    usage_limit_ = 0;
  }
}

bool DialogInviteLink::is_valid_invite_link(Slice invite_link) {
  return !LinkManager::get_dialog_invite_link_hash(invite_link).empty();
}

td_api::object_ptr<td_api::chatInviteLink> DialogInviteLink::get_chat_invite_link_object(
    const ContactsManager *contacts_manager) const {
  CHECK(contacts_manager != nullptr);
  if (!is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::chatInviteLink>(
      invite_link_, title_, contacts_manager->get_user_id_object(creator_user_id_, "get_chat_invite_link_object"),
      date_, edit_date_, expire_date_, usage_limit_, usage_count_, request_count_, creates_join_request_, is_permanent_,
      is_revoked_);
}

bool operator==(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return lhs.invite_link_ == rhs.invite_link_ && lhs.title_ == rhs.title_ &&
         lhs.creator_user_id_ == rhs.creator_user_id_ && lhs.date_ == rhs.date_ && lhs.edit_date_ == rhs.edit_date_ &&
         lhs.expire_date_ == rhs.expire_date_ && lhs.usage_limit_ == rhs.usage_limit_ &&
         lhs.usage_count_ == rhs.usage_count_ && lhs.request_count_ == rhs.request_count_ &&
         lhs.creates_join_request_ == rhs.creates_join_request_ && lhs.is_permanent_ == rhs.is_permanent_ &&
         lhs.is_revoked_ == rhs.is_revoked_;
}

bool operator!=(const DialogInviteLink &lhs, const DialogInviteLink &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogInviteLink &invite_link) {
  return string_builder << "ChatInviteLink[" << invite_link.invite_link_ << '(' << invite_link.title_ << ')'
                        << (invite_link.creates_join_request_ ? " creating join request" : "") << " by "
                        << invite_link.creator_user_id_ << " created at " << invite_link.date_ << " edited at "
                        << invite_link.edit_date_ << " expiring at " << invite_link.expire_date_ << " used by "
                        << invite_link.usage_count_ << " with usage limit " << invite_link.usage_limit_ << " and "
                        << invite_link.request_count_ << "pending join requests]";
}

}  // namespace td
