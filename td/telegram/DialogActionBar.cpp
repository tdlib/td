//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogActionBar.h"

#include "td/telegram/ChatManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/logging.h"

namespace td {

unique_ptr<DialogActionBar> DialogActionBar::create(bool can_report_spam, bool can_add_contact, bool can_block_user,
                                                    bool can_share_phone_number, bool can_report_location,
                                                    bool can_unarchive, int32 distance, bool can_invite_members,
                                                    string join_request_dialog_title, bool is_join_request_broadcast,
                                                    int32 join_request_date) {
  auto action_bar = make_unique<DialogActionBar>();
  action_bar->can_report_spam_ = can_report_spam;
  action_bar->can_add_contact_ = can_add_contact;
  action_bar->can_block_user_ = can_block_user;
  action_bar->can_share_phone_number_ = can_share_phone_number;
  action_bar->can_report_location_ = can_report_location;
  action_bar->can_unarchive_ = can_unarchive;
  action_bar->distance_ = distance >= 0 ? distance : -1;
  action_bar->can_invite_members_ = can_invite_members;
  action_bar->join_request_dialog_title_ = std::move(join_request_dialog_title);
  action_bar->is_join_request_broadcast_ = is_join_request_broadcast;
  action_bar->join_request_date_ = join_request_date;
  if (action_bar->is_empty()) {
    return nullptr;
  }
  return action_bar;
}

bool DialogActionBar::is_empty() const {
  return !can_report_spam_ && !can_add_contact_ && !can_block_user_ && !can_share_phone_number_ &&
         !can_report_location_ && !can_invite_members_ && join_request_dialog_title_.empty();
}

void DialogActionBar::fix(Td *td, DialogId dialog_id, bool is_dialog_blocked, FolderId folder_id) {
  auto dialog_type = dialog_id.get_type();
  if (distance_ >= 0 && dialog_type != DialogType::User) {
    LOG(ERROR) << "Receive distance " << distance_ << " to " << dialog_id;
    distance_ = -1;
  }

  if (!join_request_dialog_title_.empty()) {
    if (dialog_type != DialogType::User || join_request_date_ <= 0) {
      LOG(ERROR) << "Receive join_request_date = " << join_request_date_ << " in " << dialog_id;
      join_request_dialog_title_.clear();
      is_join_request_broadcast_ = false;
      join_request_date_ = 0;
    } else if (can_report_location_ || can_report_spam_ || can_add_contact_ || can_block_user_ ||
               can_share_phone_number_ || can_unarchive_ || can_invite_members_) {
      LOG(ERROR) << "Receive action bar " << can_report_location_ << '/' << can_report_spam_ << '/' << can_add_contact_
                 << '/' << can_block_user_ << '/' << can_share_phone_number_ << '/' << can_report_location_ << '/'
                 << can_unarchive_ << '/' << can_invite_members_;
      can_report_location_ = false;
      can_report_spam_ = false;
      can_add_contact_ = false;
      can_block_user_ = false;
      can_share_phone_number_ = false;
      can_unarchive_ = false;
      can_invite_members_ = false;
      distance_ = -1;
    }
  }
  if (join_request_dialog_title_.empty() && (is_join_request_broadcast_ || join_request_date_ != 0)) {
    LOG(ERROR) << "Receive join request date = " << join_request_date_ << " and " << is_join_request_broadcast_
               << " in " << dialog_id;
    is_join_request_broadcast_ = false;
    join_request_date_ = 0;
  }
  if (can_report_location_) {
    if (dialog_type != DialogType::Channel) {
      LOG(ERROR) << "Receive can_report_location in " << dialog_id;
      can_report_location_ = false;
    } else if (can_report_spam_ || can_add_contact_ || can_block_user_ || can_share_phone_number_ || can_unarchive_ ||
               can_invite_members_) {
      LOG(ERROR) << "Receive action bar " << can_report_spam_ << '/' << can_add_contact_ << '/' << can_block_user_
                 << '/' << can_share_phone_number_ << '/' << can_report_location_ << '/' << can_unarchive_ << '/'
                 << can_invite_members_;
      can_report_spam_ = false;
      can_add_contact_ = false;
      can_block_user_ = false;
      can_share_phone_number_ = false;
      can_unarchive_ = false;
      can_invite_members_ = false;
      CHECK(distance_ == -1);
    }
  }
  if (can_invite_members_) {
    if (dialog_type != DialogType::Chat &&
        (dialog_type != DialogType::Channel || td->chat_manager_->is_broadcast_channel(dialog_id.get_channel_id()))) {
      LOG(ERROR) << "Receive can_invite_members in " << dialog_id;
      can_invite_members_ = false;
    } else if (can_report_spam_ || can_add_contact_ || can_block_user_ || can_share_phone_number_ || can_unarchive_) {
      LOG(ERROR) << "Receive action bar " << can_report_spam_ << '/' << can_add_contact_ << '/' << can_block_user_
                 << '/' << can_share_phone_number_ << '/' << can_unarchive_ << '/' << can_invite_members_;
      can_report_spam_ = false;
      can_add_contact_ = false;
      can_block_user_ = false;
      can_share_phone_number_ = false;
      can_unarchive_ = false;
      CHECK(distance_ == -1);
    }
  }
  if (dialog_type == DialogType::User) {
    auto user_id = dialog_id.get_user_id();
    bool is_me = user_id == td->user_manager_->get_my_id();
    bool is_deleted = td->user_manager_->is_user_deleted(user_id);
    bool is_contact = td->user_manager_->is_user_contact(user_id);
    if (is_me || is_dialog_blocked) {
      can_report_spam_ = false;
      can_unarchive_ = false;
    }
    if (is_me || is_dialog_blocked || is_deleted) {
      can_share_phone_number_ = false;
    }
    if (is_me || is_dialog_blocked || is_deleted || is_contact) {
      can_block_user_ = false;
      can_add_contact_ = false;
    }
  }
  if (folder_id != FolderId::archive()) {
    can_unarchive_ = false;
  }
  if (can_share_phone_number_) {
    CHECK(!can_report_location_);
    CHECK(!can_invite_members_);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_share_phone_number in " << dialog_id;
      can_share_phone_number_ = false;
    } else if (can_report_spam_ || can_add_contact_ || can_block_user_ || can_unarchive_ || distance_ >= 0) {
      LOG(ERROR) << "Receive action bar " << can_report_spam_ << '/' << can_add_contact_ << '/' << can_block_user_
                 << '/' << can_share_phone_number_ << '/' << can_unarchive_ << '/' << distance_;
      can_report_spam_ = false;
      can_add_contact_ = false;
      can_block_user_ = false;
      can_unarchive_ = false;
    }
  }
  if (can_block_user_) {
    CHECK(!can_report_location_);
    CHECK(!can_invite_members_);
    CHECK(!can_share_phone_number_);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_block_user in " << dialog_id;
      can_block_user_ = false;
    } else if (!can_report_spam_ || !can_add_contact_) {
      LOG(ERROR) << "Receive action bar " << can_report_spam_ << '/' << can_add_contact_ << '/' << can_block_user_;
      can_report_spam_ = true;
      can_add_contact_ = true;
    }
  }
  if (can_add_contact_) {
    CHECK(!can_report_location_);
    CHECK(!can_invite_members_);
    CHECK(!can_share_phone_number_);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_add_contact in " << dialog_id;
      can_add_contact_ = false;
    } else if (can_report_spam_ != can_block_user_) {
      LOG(ERROR) << "Receive action bar " << can_report_spam_ << '/' << can_add_contact_ << '/' << can_block_user_;
      can_report_spam_ = false;
      can_block_user_ = false;
      can_unarchive_ = false;
    }
  }
  if (!can_block_user_) {
    distance_ = -1;
  }
  if (!can_report_spam_) {
    can_unarchive_ = false;
  }
}

td_api::object_ptr<td_api::ChatActionBar> DialogActionBar::get_chat_action_bar_object(DialogType dialog_type,
                                                                                      bool hide_unarchive) const {
  if (!join_request_dialog_title_.empty()) {
    CHECK(dialog_type == DialogType::User);
    CHECK(!can_report_location_ && !can_share_phone_number_ && !can_block_user_ && !can_add_contact_ &&
          !can_report_spam_ && !can_invite_members_);
    return td_api::make_object<td_api::chatActionBarJoinRequest>(join_request_dialog_title_, is_join_request_broadcast_,
                                                                 join_request_date_);
  }
  if (can_report_location_) {
    CHECK(dialog_type == DialogType::Channel);
    CHECK(!can_share_phone_number_ && !can_block_user_ && !can_add_contact_ && !can_report_spam_ &&
          !can_invite_members_);
    return td_api::make_object<td_api::chatActionBarReportUnrelatedLocation>();
  }
  if (can_invite_members_) {
    CHECK(!can_share_phone_number_ && !can_block_user_ && !can_add_contact_ && !can_report_spam_);
    return td_api::make_object<td_api::chatActionBarInviteMembers>();
  }
  if (can_share_phone_number_) {
    CHECK(dialog_type == DialogType::User);
    CHECK(!can_block_user_ && !can_add_contact_ && !can_report_spam_);
    return td_api::make_object<td_api::chatActionBarSharePhoneNumber>();
  }
  if (hide_unarchive) {
    if (can_add_contact_) {
      return td_api::make_object<td_api::chatActionBarAddContact>();
    } else {
      return nullptr;
    }
  }
  if (can_block_user_) {
    CHECK(dialog_type == DialogType::User);
    CHECK(can_report_spam_ && can_add_contact_);
    return td_api::make_object<td_api::chatActionBarReportAddBlock>(can_unarchive_, distance_);
  }
  if (can_add_contact_) {
    CHECK(dialog_type == DialogType::User);
    CHECK(!can_report_spam_);
    return td_api::make_object<td_api::chatActionBarAddContact>();
  }
  if (can_report_spam_) {
    return td_api::make_object<td_api::chatActionBarReportSpam>(can_unarchive_);
  }
  return nullptr;
}

bool DialogActionBar::on_dialog_unarchived() {
  if (!can_unarchive_) {
    return false;
  }

  can_unarchive_ = false;
  can_report_spam_ = false;
  can_block_user_ = false;
  // keep can_add_contact_
  return true;
}

bool DialogActionBar::on_user_contact_added() {
  if (!can_block_user_ && !can_add_contact_) {
    return false;
  }

  can_block_user_ = false;
  can_add_contact_ = false;
  // keep can_unarchive_
  distance_ = -1;
  return true;
}

bool DialogActionBar::on_user_deleted() {
  if (join_request_dialog_title_.empty() && !can_share_phone_number_ && !can_block_user_ && !can_add_contact_ &&
      distance_ < 0) {
    return false;
  }

  join_request_dialog_title_.clear();
  is_join_request_broadcast_ = false;
  join_request_date_ = 0;
  can_share_phone_number_ = false;
  can_block_user_ = false;
  can_add_contact_ = false;
  distance_ = -1;
  return true;
}

bool DialogActionBar::on_outgoing_message() {
  if (distance_ < 0) {
    return false;
  }

  distance_ = -1;
  return true;
}

bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs) {
  if (lhs == nullptr) {
    return rhs == nullptr;
  }
  if (rhs == nullptr) {
    return false;
  }
  return lhs->can_report_spam_ == rhs->can_report_spam_ && lhs->can_add_contact_ == rhs->can_add_contact_ &&
         lhs->can_block_user_ == rhs->can_block_user_ && lhs->can_share_phone_number_ == rhs->can_share_phone_number_ &&
         lhs->can_report_location_ == rhs->can_report_location_ && lhs->can_unarchive_ == rhs->can_unarchive_ &&
         lhs->distance_ == rhs->distance_ && lhs->can_invite_members_ == rhs->can_invite_members_ &&
         lhs->join_request_dialog_title_ == rhs->join_request_dialog_title_ &&
         lhs->is_join_request_broadcast_ == lhs->is_join_request_broadcast_ &&
         lhs->join_request_date_ == rhs->join_request_date_;
}

}  // namespace td
