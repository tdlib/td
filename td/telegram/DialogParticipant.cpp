//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogParticipant.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <limits>

namespace td {

DialogParticipantStatus::DialogParticipantStatus(Type type, uint32 flags, int32 until_date, string rank)
    : type_(type), flags_(flags), until_date_(until_date), rank_(strip_empty_characters(std::move(rank), 16)) {
}

int32 DialogParticipantStatus::fix_until_date(int32 date) {
  if (date == std::numeric_limits<int32>::max() || date < 0) {
    return 0;
  }
  return date;
}

DialogParticipantStatus DialogParticipantStatus::Creator(bool is_member, bool is_anonymous, string rank) {
  return DialogParticipantStatus(Type::Creator,
                                 ALL_ADMINISTRATOR_RIGHTS | ALL_PERMISSION_RIGHTS | (is_member ? IS_MEMBER : 0) |
                                     (is_anonymous ? IS_ANONYMOUS : 0),
                                 0, std::move(rank));
}

DialogParticipantStatus DialogParticipantStatus::Administrator(bool is_anonymous, string rank, bool can_be_edited,
                                                               bool can_manage_dialog, bool can_change_info,
                                                               bool can_post_messages, bool can_edit_messages,
                                                               bool can_delete_messages, bool can_invite_users,
                                                               bool can_restrict_members, bool can_pin_messages,
                                                               bool can_promote_members, bool can_manage_calls) {
  uint32 flags = (static_cast<uint32>(can_be_edited) * CAN_BE_EDITED) |
                 (static_cast<uint32>(can_manage_dialog) * CAN_MANAGE_DIALOG) |
                 (static_cast<uint32>(can_change_info) * CAN_CHANGE_INFO_AND_SETTINGS_ADMIN) |
                 (static_cast<uint32>(can_post_messages) * CAN_POST_MESSAGES) |
                 (static_cast<uint32>(can_edit_messages) * CAN_EDIT_MESSAGES) |
                 (static_cast<uint32>(can_delete_messages) * CAN_DELETE_MESSAGES) |
                 (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS_ADMIN) |
                 (static_cast<uint32>(can_restrict_members) * CAN_RESTRICT_MEMBERS) |
                 (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES_ADMIN) |
                 (static_cast<uint32>(can_promote_members) * CAN_PROMOTE_MEMBERS) |
                 (static_cast<uint32>(can_manage_calls) * CAN_MANAGE_CALLS) |
                 (static_cast<uint32>(is_anonymous) * IS_ANONYMOUS);
  if (flags == 0 || flags == CAN_BE_EDITED) {
    return Member();
  }
  flags |= CAN_MANAGE_DIALOG;
  return DialogParticipantStatus(Type::Administrator, IS_MEMBER | ALL_RESTRICTED_RIGHTS | flags, 0, std::move(rank));
}

DialogParticipantStatus DialogParticipantStatus::Member() {
  return DialogParticipantStatus(Type::Member, IS_MEMBER | ALL_PERMISSION_RIGHTS, 0, string());
}

DialogParticipantStatus DialogParticipantStatus::Restricted(
    bool is_member, int32 restricted_until_date, bool can_send_messages, bool can_send_media, bool can_send_stickers,
    bool can_send_animations, bool can_send_games, bool can_use_inline_bots, bool can_add_web_page_previews,
    bool can_send_polls, bool can_change_info_and_settings, bool can_invite_users, bool can_pin_messages) {
  uint32 flags = (static_cast<uint32>(can_send_messages) * CAN_SEND_MESSAGES) |
                 (static_cast<uint32>(can_send_media) * CAN_SEND_MEDIA) |
                 (static_cast<uint32>(can_send_stickers) * CAN_SEND_STICKERS) |
                 (static_cast<uint32>(can_send_animations) * CAN_SEND_ANIMATIONS) |
                 (static_cast<uint32>(can_send_games) * CAN_SEND_GAMES) |
                 (static_cast<uint32>(can_use_inline_bots) * CAN_USE_INLINE_BOTS) |
                 (static_cast<uint32>(can_add_web_page_previews) * CAN_ADD_WEB_PAGE_PREVIEWS) |
                 (static_cast<uint32>(can_send_polls) * CAN_SEND_POLLS) |
                 (static_cast<uint32>(can_change_info_and_settings) * CAN_CHANGE_INFO_AND_SETTINGS_BANNED) |
                 (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS_BANNED) |
                 (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES_BANNED) |
                 (static_cast<uint32>(is_member) * IS_MEMBER);
  if (flags == (IS_MEMBER | ALL_PERMISSION_RIGHTS)) {
    return Member();
  }
  return DialogParticipantStatus(Type::Restricted, flags, fix_until_date(restricted_until_date), string());
}

DialogParticipantStatus DialogParticipantStatus::Left() {
  return DialogParticipantStatus(Type::Left, ALL_PERMISSION_RIGHTS, 0, string());
}

DialogParticipantStatus DialogParticipantStatus::Banned(int32 banned_until_date) {
  return DialogParticipantStatus(Type::Banned, 0, fix_until_date(banned_until_date), string());
}

DialogParticipantStatus DialogParticipantStatus::GroupAdministrator(bool is_creator) {
  return Administrator(false, string(), is_creator, true, true, false, false, true, true, true, true, false, true);
}

DialogParticipantStatus DialogParticipantStatus::ChannelAdministrator(bool is_creator, bool is_megagroup) {
  if (is_megagroup) {
    return Administrator(false, string(), is_creator, true, true, false, false, true, true, true, true, false, false);
  } else {
    return Administrator(false, string(), is_creator, true, false, true, true, true, false, true, false, false, false);
  }
}

RestrictedRights DialogParticipantStatus::get_restricted_rights() const {
  return RestrictedRights(can_send_messages(), can_send_media(), can_send_stickers(), can_send_animations(),
                          can_send_games(), can_use_inline_bots(), can_add_web_page_previews(), can_send_polls(),
                          can_change_info_and_settings(), can_invite_users(), can_pin_messages());
}

tl_object_ptr<td_api::ChatMemberStatus> DialogParticipantStatus::get_chat_member_status_object() const {
  switch (type_) {
    case Type::Creator:
      return td_api::make_object<td_api::chatMemberStatusCreator>(rank_, is_anonymous(), is_member());
    case Type::Administrator:
      return td_api::make_object<td_api::chatMemberStatusAdministrator>(
          rank_, can_be_edited(), can_manage_dialog(), can_change_info_and_settings(), can_post_messages(),
          can_edit_messages(), can_delete_messages(), can_invite_users(), can_restrict_members(), can_pin_messages(),
          can_promote_members(), can_manage_calls(), is_anonymous());
    case Type::Member:
      return td_api::make_object<td_api::chatMemberStatusMember>();
    case Type::Restricted:
      return td_api::make_object<td_api::chatMemberStatusRestricted>(
          is_member(), until_date_, get_restricted_rights().get_chat_permissions_object());
    case Type::Left:
      return td_api::make_object<td_api::chatMemberStatusLeft>();
    case Type::Banned:
      return td_api::make_object<td_api::chatMemberStatusBanned>(until_date_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::chatAdminRights> DialogParticipantStatus::get_chat_admin_rights() const {
  int32 flags = 0;
  if ((flags_ & CAN_CHANGE_INFO_AND_SETTINGS_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::CHANGE_INFO_MASK;
  }
  if (can_post_messages()) {
    flags |= telegram_api::chatAdminRights::POST_MESSAGES_MASK;
  }
  if (can_edit_messages()) {
    flags |= telegram_api::chatAdminRights::EDIT_MESSAGES_MASK;
  }
  if (can_delete_messages()) {
    flags |= telegram_api::chatAdminRights::DELETE_MESSAGES_MASK;
  }
  if ((flags_ & CAN_INVITE_USERS_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::INVITE_USERS_MASK;
  }
  if (can_restrict_members()) {
    flags |= telegram_api::chatAdminRights::BAN_USERS_MASK;
  }
  if ((flags_ & CAN_PIN_MESSAGES_ADMIN) != 0) {
    flags |= telegram_api::chatAdminRights::PIN_MESSAGES_MASK;
  }
  if (can_promote_members()) {
    flags |= telegram_api::chatAdminRights::ADD_ADMINS_MASK;
  }
  if (can_manage_calls()) {
    flags |= telegram_api::chatAdminRights::MANAGE_CALL_MASK;
  }
  if (is_anonymous()) {
    flags |= telegram_api::chatAdminRights::ANONYMOUS_MASK;
  }
  if (can_manage_dialog()) {
    flags |= telegram_api::chatAdminRights::OTHER_MASK;
  }

  LOG(INFO) << "Create chat admin rights " << flags;
  return make_tl_object<telegram_api::chatAdminRights>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/);
}

tl_object_ptr<telegram_api::chatBannedRights> DialogParticipantStatus::get_chat_banned_rights() const {
  int32 flags = 0;
  if (type_ == Type::Banned) {
    flags |= telegram_api::chatBannedRights::VIEW_MESSAGES_MASK;
  }
  if (!can_send_messages()) {
    flags |= telegram_api::chatBannedRights::SEND_MESSAGES_MASK;
  }
  if (!can_send_media()) {
    flags |= telegram_api::chatBannedRights::SEND_MEDIA_MASK;
  }
  if (!can_send_stickers()) {
    flags |= telegram_api::chatBannedRights::SEND_STICKERS_MASK;
  }
  if (!can_send_animations()) {
    flags |= telegram_api::chatBannedRights::SEND_GIFS_MASK;
  }
  if (!can_send_games()) {
    flags |= telegram_api::chatBannedRights::SEND_GAMES_MASK;
  }
  if (!can_use_inline_bots()) {
    flags |= telegram_api::chatBannedRights::SEND_INLINE_MASK;
  }
  if (!can_add_web_page_previews()) {
    flags |= telegram_api::chatBannedRights::EMBED_LINKS_MASK;
  }
  if (!can_send_polls()) {
    flags |= telegram_api::chatBannedRights::SEND_POLLS_MASK;
  }
  if ((flags_ & CAN_CHANGE_INFO_AND_SETTINGS_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::CHANGE_INFO_MASK;
  }
  if ((flags_ & CAN_INVITE_USERS_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::INVITE_USERS_MASK;
  }
  if ((flags_ & CAN_PIN_MESSAGES_BANNED) == 0) {
    flags |= telegram_api::chatBannedRights::PIN_MESSAGES_MASK;
  }

  LOG(INFO) << "Create chat banned rights " << flags << " until " << until_date_;
  return make_tl_object<telegram_api::chatBannedRights>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, until_date_);
}

DialogParticipantStatus DialogParticipantStatus::apply_restrictions(RestrictedRights default_restrictions,
                                                                    bool is_bot) const {
  auto flags = flags_;
  switch (type_) {
    case Type::Creator:
      // creator can do anything and isn't affected by restrictions
      break;
    case Type::Administrator:
      // administrators aren't affected by restrictions, but if everyone can invite users,
      // pin messages or change info, they also can do that
      if (!is_bot) {
        flags |= default_restrictions.flags_ & ALL_ADMIN_PERMISSION_RIGHTS;
      }
      break;
    case Type::Member:
    case Type::Restricted:
    case Type::Left:
      // members and restricted are affected by default restrictions
      flags &= ~ALL_PERMISSION_RIGHTS | default_restrictions.flags_;
      if (is_bot) {
        flags &= ~ALL_ADMIN_PERMISSION_RIGHTS;
      }
      break;
    case Type::Banned:
      // banned can do nothing, even restrictions allows them to do that
      break;
    default:
      UNREACHABLE();
      break;
  }

  return DialogParticipantStatus(type_, flags, 0, string());
}

void DialogParticipantStatus::update_restrictions() const {
  if (until_date_ != 0 && G()->unix_time() > until_date_) {
    until_date_ = 0;
    if (type_ == Type::Restricted) {
      if (is_member()) {
        type_ = Type::Member;
      } else {
        type_ = Type::Left;
      }
      flags_ |= ALL_PERMISSION_RIGHTS;
    } else if (type_ == Type::Banned) {
      type_ = Type::Left;
    } else {
      UNREACHABLE();
    }
  }
}

bool operator==(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs) {
  return lhs.type_ == rhs.type_ && lhs.flags_ == rhs.flags_ && lhs.until_date_ == rhs.until_date_ &&
         lhs.rank_ == rhs.rank_;
}

bool operator!=(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantStatus &status) {
  switch (status.type_) {
    case DialogParticipantStatus::Type::Creator:
      string_builder << "Creator";
      if (!status.is_member()) {
        string_builder << "-non-member";
      }
      if (!status.rank_.empty()) {
        string_builder << " [" << status.rank_ << "]";
      }
      if (status.is_anonymous()) {
        string_builder << "-anonymous";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Administrator:
      string_builder << "Administrator: ";
      if (status.can_manage_dialog()) {
        string_builder << "(manage)";
      }
      if (status.can_change_info_and_settings()) {
        string_builder << "(change)";
      }
      if (status.can_post_messages()) {
        string_builder << "(post)";
      }
      if (status.can_edit_messages()) {
        string_builder << "(edit)";
      }
      if (status.can_delete_messages()) {
        string_builder << "(delete)";
      }
      if (status.can_invite_users()) {
        string_builder << "(invite)";
      }
      if (status.can_restrict_members()) {
        string_builder << "(restrict)";
      }
      if (status.can_pin_messages()) {
        string_builder << "(pin)";
      }
      if (status.can_promote_members()) {
        string_builder << "(promote)";
      }
      if (status.can_manage_calls()) {
        string_builder << "(voice chat)";
      }
      if (!status.rank_.empty()) {
        string_builder << " [" << status.rank_ << "]";
      }
      if (status.is_anonymous()) {
        string_builder << "-anonymous";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Member:
      return string_builder << "Member";
    case DialogParticipantStatus::Type::Restricted:
      string_builder << "Restricted ";
      if (status.until_date_ == 0) {
        string_builder << "forever ";
      } else {
        string_builder << "until " << status.until_date_ << " ";
      }
      if (!status.is_member()) {
        string_builder << "non-";
      }
      string_builder << "member: ";
      if (!status.can_send_messages()) {
        string_builder << "(text)";
      }
      if (!status.can_send_media()) {
        string_builder << "(media)";
      }
      if (!status.can_send_stickers()) {
        string_builder << "(stickers)";
      }
      if (!status.can_send_animations()) {
        string_builder << "(animations)";
      }
      if (!status.can_send_games()) {
        string_builder << "(games)";
      }
      if (!status.can_send_polls()) {
        string_builder << "(polls)";
      }
      if (!status.can_use_inline_bots()) {
        string_builder << "(inline bots)";
      }
      if (!status.can_add_web_page_previews()) {
        string_builder << "(links)";
      }
      if (!status.can_change_info_and_settings()) {
        string_builder << "(change)";
      }
      if (!status.can_invite_users()) {
        string_builder << "(invite)";
      }
      if (!status.can_pin_messages()) {
        string_builder << "(pin)";
      }
      return string_builder;
    case DialogParticipantStatus::Type::Left:
      return string_builder << "Left";
    case DialogParticipantStatus::Type::Banned:
      string_builder << "Banned ";
      if (status.until_date_ == 0) {
        string_builder << "forever";
      } else {
        string_builder << "until " << status.until_date_;
      }
      return string_builder;
    default:
      UNREACHABLE();
      return string_builder << "Impossible";
  }
}

DialogParticipantStatus get_dialog_participant_status(const tl_object_ptr<td_api::ChatMemberStatus> &status) {
  auto constructor_id = status == nullptr ? td_api::chatMemberStatusMember::ID : status->get_id();
  switch (constructor_id) {
    case td_api::chatMemberStatusCreator::ID: {
      auto st = static_cast<const td_api::chatMemberStatusCreator *>(status.get());
      return DialogParticipantStatus::Creator(st->is_member_, st->is_anonymous_, st->custom_title_);
    }
    case td_api::chatMemberStatusAdministrator::ID: {
      auto st = static_cast<const td_api::chatMemberStatusAdministrator *>(status.get());
      return DialogParticipantStatus::Administrator(
          st->is_anonymous_, st->custom_title_, true /*st->can_be_edited_*/, st->can_manage_chat_, st->can_change_info_,
          st->can_post_messages_, st->can_edit_messages_, st->can_delete_messages_, st->can_invite_users_,
          st->can_restrict_members_, st->can_pin_messages_, st->can_promote_members_, st->can_manage_voice_chats_);
    }
    case td_api::chatMemberStatusMember::ID:
      return DialogParticipantStatus::Member();
    case td_api::chatMemberStatusRestricted::ID: {
      auto st = static_cast<const td_api::chatMemberStatusRestricted *>(status.get());
      auto permissions = st->permissions_.get();
      bool can_send_polls = permissions->can_send_polls_;
      bool can_send_media = permissions->can_send_media_messages_;
      bool can_send_messages = permissions->can_send_messages_ || can_send_media || can_send_polls ||
                               permissions->can_send_other_messages_ || permissions->can_add_web_page_previews_;
      return DialogParticipantStatus::Restricted(
          st->is_member_, st->restricted_until_date_, can_send_messages, can_send_media,
          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
          permissions->can_add_web_page_previews_, permissions->can_send_polls_, permissions->can_change_info_,
          permissions->can_invite_users_, permissions->can_pin_messages_);
    }
    case td_api::chatMemberStatusLeft::ID:
      return DialogParticipantStatus::Left();
    case td_api::chatMemberStatusBanned::ID: {
      auto st = static_cast<const td_api::chatMemberStatusBanned *>(status.get());
      return DialogParticipantStatus::Banned(st->banned_until_date_);
    }
    default:
      UNREACHABLE();
      return DialogParticipantStatus::Member();
  }
}

DialogParticipantStatus get_dialog_participant_status(bool can_be_edited,
                                                      const tl_object_ptr<telegram_api::chatAdminRights> &admin_rights,
                                                      string rank) {
  bool can_change_info = (admin_rights->flags_ & telegram_api::chatAdminRights::CHANGE_INFO_MASK) != 0;
  bool can_post_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::POST_MESSAGES_MASK) != 0;
  bool can_edit_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::EDIT_MESSAGES_MASK) != 0;
  bool can_delete_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::DELETE_MESSAGES_MASK) != 0;
  bool can_invite_users = (admin_rights->flags_ & telegram_api::chatAdminRights::INVITE_USERS_MASK) != 0;
  bool can_restrict_members = (admin_rights->flags_ & telegram_api::chatAdminRights::BAN_USERS_MASK) != 0;
  bool can_pin_messages = (admin_rights->flags_ & telegram_api::chatAdminRights::PIN_MESSAGES_MASK) != 0;
  bool can_promote_members = (admin_rights->flags_ & telegram_api::chatAdminRights::ADD_ADMINS_MASK) != 0;
  bool can_manage_calls = (admin_rights->flags_ & telegram_api::chatAdminRights::MANAGE_CALL_MASK) != 0;
  bool is_anonymous = (admin_rights->flags_ & telegram_api::chatAdminRights::ANONYMOUS_MASK) != 0;
  bool can_manage_dialog = (admin_rights->flags_ & telegram_api::chatAdminRights::OTHER_MASK) != 0;
  if (!can_manage_dialog) {
    LOG(ERROR) << "Receive wrong other flag in " << to_string(admin_rights);
  }
  return DialogParticipantStatus::Administrator(is_anonymous, std::move(rank), can_be_edited, can_manage_dialog,
                                                can_change_info, can_post_messages, can_edit_messages,
                                                can_delete_messages, can_invite_users, can_restrict_members,
                                                can_pin_messages, can_promote_members, can_manage_calls);
}

DialogParticipantStatus get_dialog_participant_status(
    bool is_member, const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights) {
  bool can_view_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::VIEW_MESSAGES_MASK) == 0;
  if (!can_view_messages) {
    return DialogParticipantStatus::Banned(banned_rights->until_date_);
  }
  bool can_send_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MESSAGES_MASK) == 0;
  bool can_send_media_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MEDIA_MASK) == 0;
  bool can_send_stickers = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_STICKERS_MASK) == 0;
  bool can_send_animations = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GIFS_MASK) == 0;
  bool can_send_games = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GAMES_MASK) == 0;
  bool can_use_inline_bots = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_INLINE_MASK) == 0;
  bool can_add_web_page_previews = (banned_rights->flags_ & telegram_api::chatBannedRights::EMBED_LINKS_MASK) == 0;
  bool can_send_polls = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_POLLS_MASK) == 0;
  bool can_change_info_and_settings = (banned_rights->flags_ & telegram_api::chatBannedRights::CHANGE_INFO_MASK) == 0;
  bool can_invite_users = (banned_rights->flags_ & telegram_api::chatBannedRights::INVITE_USERS_MASK) == 0;
  bool can_pin_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::PIN_MESSAGES_MASK) == 0;
  return DialogParticipantStatus::Restricted(
      is_member, banned_rights->until_date_, can_send_messages, can_send_media_messages, can_send_stickers,
      can_send_animations, can_send_games, can_use_inline_bots, can_add_web_page_previews, can_send_polls,
      can_change_info_and_settings, can_invite_users, can_pin_messages);
}

RestrictedRights::RestrictedRights(bool can_send_messages, bool can_send_media, bool can_send_stickers,
                                   bool can_send_animations, bool can_send_games, bool can_use_inline_bots,
                                   bool can_add_web_page_previews, bool can_send_polls,
                                   bool can_change_info_and_settings, bool can_invite_users, bool can_pin_messages) {
  flags_ = (static_cast<uint32>(can_send_messages) * CAN_SEND_MESSAGES) |
           (static_cast<uint32>(can_send_media) * CAN_SEND_MEDIA) |
           (static_cast<uint32>(can_send_stickers) * CAN_SEND_STICKERS) |
           (static_cast<uint32>(can_send_animations) * CAN_SEND_ANIMATIONS) |
           (static_cast<uint32>(can_send_games) * CAN_SEND_GAMES) |
           (static_cast<uint32>(can_use_inline_bots) * CAN_USE_INLINE_BOTS) |
           (static_cast<uint32>(can_add_web_page_previews) * CAN_ADD_WEB_PAGE_PREVIEWS) |
           (static_cast<uint32>(can_send_polls) * CAN_SEND_POLLS) |
           (static_cast<uint32>(can_change_info_and_settings) * CAN_CHANGE_INFO_AND_SETTINGS) |
           (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS) |
           (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES);
}

tl_object_ptr<td_api::chatPermissions> RestrictedRights::get_chat_permissions_object() const {
  return td_api::make_object<td_api::chatPermissions>(
      can_send_messages(), can_send_media(), can_send_polls(),
      can_send_stickers() || can_send_animations() || can_send_games() || can_use_inline_bots(),
      can_add_web_page_previews(), can_change_info_and_settings(), can_invite_users(), can_pin_messages());
}

tl_object_ptr<telegram_api::chatBannedRights> RestrictedRights::get_chat_banned_rights() const {
  int32 flags = 0;
  if (!can_send_messages()) {
    flags |= telegram_api::chatBannedRights::SEND_MESSAGES_MASK;
  }
  if (!can_send_media()) {
    flags |= telegram_api::chatBannedRights::SEND_MEDIA_MASK;
  }
  if (!can_send_stickers()) {
    flags |= telegram_api::chatBannedRights::SEND_STICKERS_MASK;
  }
  if (!can_send_animations()) {
    flags |= telegram_api::chatBannedRights::SEND_GIFS_MASK;
  }
  if (!can_send_games()) {
    flags |= telegram_api::chatBannedRights::SEND_GAMES_MASK;
  }
  if (!can_use_inline_bots()) {
    flags |= telegram_api::chatBannedRights::SEND_INLINE_MASK;
  }
  if (!can_add_web_page_previews()) {
    flags |= telegram_api::chatBannedRights::EMBED_LINKS_MASK;
  }
  if (!can_send_polls()) {
    flags |= telegram_api::chatBannedRights::SEND_POLLS_MASK;
  }
  if (!can_change_info_and_settings()) {
    flags |= telegram_api::chatBannedRights::CHANGE_INFO_MASK;
  }
  if (!can_invite_users()) {
    flags |= telegram_api::chatBannedRights::INVITE_USERS_MASK;
  }
  if (!can_pin_messages()) {
    flags |= telegram_api::chatBannedRights::PIN_MESSAGES_MASK;
  }

  LOG(INFO) << "Create chat banned rights " << flags;
  return make_tl_object<telegram_api::chatBannedRights>(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                        false /*ignored*/, false /*ignored*/, false /*ignored*/, 0);
}

bool operator==(const RestrictedRights &lhs, const RestrictedRights &rhs) {
  return lhs.flags_ == rhs.flags_;
}

bool operator!=(const RestrictedRights &lhs, const RestrictedRights &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const RestrictedRights &status) {
  string_builder << "Restricted: ";
  if (!status.can_send_messages()) {
    string_builder << "(text)";
  }
  if (!status.can_send_media()) {
    string_builder << "(media)";
  }
  if (!status.can_send_stickers()) {
    string_builder << "(stickers)";
  }
  if (!status.can_send_animations()) {
    string_builder << "(animations)";
  }
  if (!status.can_send_games()) {
    string_builder << "(games)";
  }
  if (!status.can_send_polls()) {
    string_builder << "(polls)";
  }
  if (!status.can_use_inline_bots()) {
    string_builder << "(inline bots)";
  }
  if (!status.can_add_web_page_previews()) {
    string_builder << "(links)";
  }
  if (!status.can_change_info_and_settings()) {
    string_builder << "(change)";
  }
  if (!status.can_invite_users()) {
    string_builder << "(invite)";
  }
  if (!status.can_pin_messages()) {
    string_builder << "(pin)";
  }
  return string_builder;
}

RestrictedRights get_restricted_rights(const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights) {
  if (banned_rights == nullptr) {
    return RestrictedRights(false, false, false, false, false, false, false, false, false, false, false);
  }
  bool can_view_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::VIEW_MESSAGES_MASK) == 0;
  if (!can_view_messages) {
    LOG(ERROR) << "Can't view messages in restricted rights " << to_string(banned_rights);
  }
  LOG_IF(ERROR, banned_rights->until_date_ != std::numeric_limits<int32>::max())
      << "Have until date " << banned_rights->until_date_ << " in restricted rights";

  bool can_send_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MESSAGES_MASK) == 0;
  bool can_send_media_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_MEDIA_MASK) == 0;
  bool can_send_stickers = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_STICKERS_MASK) == 0;
  bool can_send_animations = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GIFS_MASK) == 0;
  bool can_send_games = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_GAMES_MASK) == 0;
  bool can_use_inline_bots = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_INLINE_MASK) == 0;
  bool can_add_web_page_previews = (banned_rights->flags_ & telegram_api::chatBannedRights::EMBED_LINKS_MASK) == 0;
  bool can_send_polls = (banned_rights->flags_ & telegram_api::chatBannedRights::SEND_POLLS_MASK) == 0;
  bool can_change_info_and_settings = (banned_rights->flags_ & telegram_api::chatBannedRights::CHANGE_INFO_MASK) == 0;
  bool can_invite_users = (banned_rights->flags_ & telegram_api::chatBannedRights::INVITE_USERS_MASK) == 0;
  bool can_pin_messages = (banned_rights->flags_ & telegram_api::chatBannedRights::PIN_MESSAGES_MASK) == 0;
  return RestrictedRights(can_send_messages, can_send_media_messages, can_send_stickers, can_send_animations,
                          can_send_games, can_use_inline_bots, can_add_web_page_previews, can_send_polls,
                          can_change_info_and_settings, can_invite_users, can_pin_messages);
}

RestrictedRights get_restricted_rights(const td_api::object_ptr<td_api::chatPermissions> &permissions) {
  bool can_send_polls = permissions->can_send_polls_;
  bool can_send_media = permissions->can_send_media_messages_;
  bool can_send_messages = permissions->can_send_messages_ || can_send_media || can_send_polls ||
                           permissions->can_send_other_messages_ || permissions->can_add_web_page_previews_;
  return RestrictedRights(can_send_messages, can_send_media, permissions->can_send_other_messages_,
                          permissions->can_send_other_messages_, permissions->can_send_other_messages_,
                          permissions->can_send_other_messages_, permissions->can_add_web_page_previews_,
                          permissions->can_send_polls_, permissions->can_change_info_, permissions->can_invite_users_,
                          permissions->can_pin_messages_);
}

DialogParticipant::DialogParticipant(UserId user_id, UserId inviter_user_id, int32 joined_date,
                                     DialogParticipantStatus status)
    : user_id(user_id), inviter_user_id(inviter_user_id), joined_date(joined_date), status(status) {
  if (!inviter_user_id.is_valid() && inviter_user_id != UserId()) {
    LOG(ERROR) << "Receive inviter " << inviter_user_id;
    inviter_user_id = UserId();
  }
  if (joined_date < 0) {
    LOG(ERROR) << "Receive date " << joined_date;
    joined_date = 0;
  }
}

DialogParticipant::DialogParticipant(tl_object_ptr<telegram_api::ChatParticipant> &&participant_ptr,
                                     int32 chat_creation_date, bool is_creator) {
  switch (participant_ptr->get_id()) {
    case telegram_api::chatParticipant::ID: {
      auto participant = move_tl_object_as<telegram_api::chatParticipant>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
               DialogParticipantStatus::Member()};
      break;
    }
    case telegram_api::chatParticipantCreator::ID: {
      auto participant = move_tl_object_as<telegram_api::chatParticipantCreator>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(participant->user_id_), chat_creation_date,
               DialogParticipantStatus::Creator(true, false, string())};
      break;
    }
    case telegram_api::chatParticipantAdmin::ID: {
      auto participant = move_tl_object_as<telegram_api::chatParticipantAdmin>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
               DialogParticipantStatus::GroupAdministrator(is_creator)};
      break;
    }
    default:
      UNREACHABLE();
  }
}

DialogParticipant::DialogParticipant(tl_object_ptr<telegram_api::ChannelParticipant> &&participant_ptr) {
  CHECK(participant_ptr != nullptr);
  switch (participant_ptr->get_id()) {
    case telegram_api::channelParticipant::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipant>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(), participant->date_, DialogParticipantStatus::Member()};
      break;
    }
    case telegram_api::channelParticipantSelf::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantSelf>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
               DialogParticipantStatus::Member()};
      break;
    }
    case telegram_api::channelParticipantCreator::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantCreator>(participant_ptr);
      bool is_anonymous = (participant->admin_rights_->flags_ & telegram_api::chatAdminRights::ANONYMOUS_MASK) != 0;
      *this = {UserId(participant->user_id_), UserId(), 0,
               DialogParticipantStatus::Creator(true, is_anonymous, std::move(participant->rank_))};
      break;
    }
    case telegram_api::channelParticipantAdmin::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantAdmin>(participant_ptr);
      bool can_be_edited = (participant->flags_ & telegram_api::channelParticipantAdmin::CAN_EDIT_MASK) != 0;
      *this = {UserId(participant->user_id_), UserId(participant->promoted_by_), participant->date_,
               get_dialog_participant_status(can_be_edited, std::move(participant->admin_rights_),
                                             std::move(participant->rank_))};
      break;
    }
    case telegram_api::channelParticipantLeft::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantLeft>(participant_ptr);
      *this = {UserId(participant->user_id_), UserId(), 0, DialogParticipantStatus::Left()};
      break;
    }
    case telegram_api::channelParticipantBanned::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantBanned>(participant_ptr);
      auto is_member = (participant->flags_ & telegram_api::channelParticipantBanned::LEFT_MASK) == 0;
      *this = {UserId(participant->user_id_), UserId(participant->kicked_by_), participant->date_,
               get_dialog_participant_status(is_member, std::move(participant->banned_rights_))};
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

bool DialogParticipant::is_valid() const {
  if (!user_id.is_valid() || joined_date < 0) {
    return false;
  }
  if (status.is_restricted() || status.is_banned() || (status.is_administrator() && !status.is_creator())) {
    return inviter_user_id.is_valid();
  }
  return true;
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipant &dialog_participant) {
  return string_builder << '[' << dialog_participant.user_id << " invited by " << dialog_participant.inviter_user_id
                        << " at " << dialog_participant.joined_date << " with status " << dialog_participant.status
                        << ']';
}

td_api::object_ptr<td_api::chatMembers> DialogParticipants::get_chat_members_object(Td *td) const {
  vector<tl_object_ptr<td_api::chatMember>> chat_members;
  chat_members.reserve(participants_.size());
  for (auto &participant : participants_) {
    chat_members.push_back(td->contacts_manager_->get_chat_member_object(participant));
  }

  return td_api::make_object<td_api::chatMembers>(total_count_, std::move(chat_members));
}

tl_object_ptr<telegram_api::ChannelParticipantsFilter>
ChannelParticipantsFilter::get_input_channel_participants_filter() const {
  switch (type) {
    case Type::Recent:
      return make_tl_object<telegram_api::channelParticipantsRecent>();
    case Type::Contacts:
      return make_tl_object<telegram_api::channelParticipantsContacts>(query);
    case Type::Administrators:
      return make_tl_object<telegram_api::channelParticipantsAdmins>();
    case Type::Search:
      return make_tl_object<telegram_api::channelParticipantsSearch>(query);
    case Type::Mention: {
      int32 flags = 0;
      if (!query.empty()) {
        flags |= telegram_api::channelParticipantsMentions::Q_MASK;
      }
      if (top_thread_message_id.is_valid()) {
        flags |= telegram_api::channelParticipantsMentions::TOP_MSG_ID_MASK;
      }
      return make_tl_object<telegram_api::channelParticipantsMentions>(
          flags, query, top_thread_message_id.get_server_message_id().get());
    }
    case Type::Restricted:
      return make_tl_object<telegram_api::channelParticipantsBanned>(query);
    case Type::Banned:
      return make_tl_object<telegram_api::channelParticipantsKicked>(query);
    case Type::Bots:
      return make_tl_object<telegram_api::channelParticipantsBots>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

ChannelParticipantsFilter::ChannelParticipantsFilter(const tl_object_ptr<td_api::SupergroupMembersFilter> &filter) {
  if (filter == nullptr) {
    type = Type::Recent;
    return;
  }
  switch (filter->get_id()) {
    case td_api::supergroupMembersFilterRecent::ID:
      type = Type::Recent;
      return;
    case td_api::supergroupMembersFilterContacts::ID:
      type = Type::Contacts;
      query = static_cast<const td_api::supergroupMembersFilterContacts *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterAdministrators::ID:
      type = Type::Administrators;
      return;
    case td_api::supergroupMembersFilterSearch::ID:
      type = Type::Search;
      query = static_cast<const td_api::supergroupMembersFilterSearch *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterMention::ID: {
      auto mention_filter = static_cast<const td_api::supergroupMembersFilterMention *>(filter.get());
      type = Type::Mention;
      query = mention_filter->query_;
      top_thread_message_id = MessageId(mention_filter->message_thread_id_);
      if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
        top_thread_message_id = MessageId();
      }
      return;
    }
    case td_api::supergroupMembersFilterRestricted::ID:
      type = Type::Restricted;
      query = static_cast<const td_api::supergroupMembersFilterRestricted *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBanned::ID:
      type = Type::Banned;
      query = static_cast<const td_api::supergroupMembersFilterBanned *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBots::ID:
      type = Type::Bots;
      return;
    default:
      UNREACHABLE();
      type = Type::Recent;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantsFilter &filter) {
  switch (filter.type) {
    case ChannelParticipantsFilter::Type::Recent:
      return string_builder << "Recent";
    case ChannelParticipantsFilter::Type::Contacts:
      return string_builder << "Contacts \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Administrators:
      return string_builder << "Administrators";
    case ChannelParticipantsFilter::Type::Search:
      return string_builder << "Search \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Mention:
      return string_builder << "Mention \"" << filter.query << "\" in thread of " << filter.top_thread_message_id;
    case ChannelParticipantsFilter::Type::Restricted:
      return string_builder << "Restricted \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Banned:
      return string_builder << "Banned \"" << filter.query << '"';
    case ChannelParticipantsFilter::Type::Bots:
      return string_builder << "Bots";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantsFilter &filter) {
  switch (filter.type) {
    case DialogParticipantsFilter::Type::Contacts:
      return string_builder << "Contacts";
    case DialogParticipantsFilter::Type::Administrators:
      return string_builder << "Administrators";
    case DialogParticipantsFilter::Type::Members:
      return string_builder << "Members";
    case DialogParticipantsFilter::Type::Restricted:
      return string_builder << "Restricted";
    case DialogParticipantsFilter::Type::Banned:
      return string_builder << "Banned";
    case DialogParticipantsFilter::Type::Mention:
      return string_builder << "Mention";
    case DialogParticipantsFilter::Type::Bots:
      return string_builder << "Bots";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

DialogParticipantsFilter get_dialog_participants_filter(const tl_object_ptr<td_api::ChatMembersFilter> &filter) {
  if (filter == nullptr) {
    return DialogParticipantsFilter{DialogParticipantsFilter::Type::Members};
  }
  switch (filter->get_id()) {
    case td_api::chatMembersFilterContacts::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Contacts};
    case td_api::chatMembersFilterAdministrators::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Administrators};
    case td_api::chatMembersFilterMembers::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Members};
    case td_api::chatMembersFilterRestricted::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Restricted};
    case td_api::chatMembersFilterBanned::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Banned};
    case td_api::chatMembersFilterMention::ID: {
      auto mention_filter = static_cast<const td_api::chatMembersFilterMention *>(filter.get());
      auto top_thread_message_id = MessageId(mention_filter->message_thread_id_);
      if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
        top_thread_message_id = MessageId();
      }
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Mention, top_thread_message_id};
    }
    case td_api::chatMembersFilterBots::ID:
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Bots};
    default:
      UNREACHABLE();
      return DialogParticipantsFilter{DialogParticipantsFilter::Type::Members};
  }
}

}  // namespace td
