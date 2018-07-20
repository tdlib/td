//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogParticipant.h"

#include "td/telegram/Global.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <limits>

namespace td {

int32 DialogParticipantStatus::fix_until_date(int32 date) {
  if (date == std::numeric_limits<int32>::max() || date < 0) {
    return 0;
  }
  return date;
}

DialogParticipantStatus DialogParticipantStatus::Creator(bool is_member) {
  return DialogParticipantStatus(Type::Creator,
                                 ALL_ADMINISTRATOR_RIGHTS | ALL_RESTRICTED_RIGHTS | (is_member ? IS_MEMBER : 0), 0);
}

DialogParticipantStatus DialogParticipantStatus::Administrator(bool can_be_edited, bool can_change_info,
                                                               bool can_post_messages, bool can_edit_messages,
                                                               bool can_delete_messages, bool can_invite_users,
                                                               bool can_export_dialog_invite_link,
                                                               bool can_restrict_members, bool can_pin_messages,
                                                               bool can_promote_members) {
  uint32 flags = (static_cast<uint32>(can_be_edited) * CAN_BE_EDITED) |
                 (static_cast<uint32>(can_change_info) * CAN_CHANGE_INFO_AND_SETTINGS) |
                 (static_cast<uint32>(can_post_messages) * CAN_POST_MESSAGES) |
                 (static_cast<uint32>(can_edit_messages) * CAN_EDIT_MESSAGES) |
                 (static_cast<uint32>(can_delete_messages) * CAN_DELETE_MESSAGES) |
                 (static_cast<uint32>(can_invite_users) * CAN_INVITE_USERS) |
                 (static_cast<uint32>(can_export_dialog_invite_link) * CAN_EXPORT_DIALOG_INVITE_LINK) |
                 (static_cast<uint32>(can_restrict_members) * CAN_RESTRICT_MEMBERS) |
                 (static_cast<uint32>(can_pin_messages) * CAN_PIN_MESSAGES) |
                 (static_cast<uint32>(can_promote_members) * CAN_PROMOTE_MEMBERS);
  if (flags == 0 || flags == CAN_BE_EDITED) {
    return Member();
  }
  return DialogParticipantStatus(Type::Administrator, IS_MEMBER | ALL_RESTRICTED_RIGHTS | flags, 0);
}

DialogParticipantStatus DialogParticipantStatus::Member() {
  return DialogParticipantStatus(Type::Member, IS_MEMBER | ALL_RESTRICTED_RIGHTS, 0);
}

DialogParticipantStatus DialogParticipantStatus::Restricted(bool is_member, int32 restricted_until_date,
                                                            bool can_send_messages, bool can_send_media,
                                                            bool can_send_stickers, bool can_send_animations,
                                                            bool can_send_games, bool can_use_inline_bots,
                                                            bool can_add_web_page_previews) {
  uint32 flags = (static_cast<uint32>(can_send_messages) * CAN_SEND_MESSAGES) |
                 (static_cast<uint32>(can_send_media) * CAN_SEND_MEDIA) |
                 (static_cast<uint32>(can_send_stickers) * CAN_SEND_STICKERS) |
                 (static_cast<uint32>(can_send_animations) * CAN_SEND_ANIMATIONS) |
                 (static_cast<uint32>(can_send_games) * CAN_SEND_GAMES) |
                 (static_cast<uint32>(can_use_inline_bots) * CAN_USE_INLINE_BOTS) |
                 (static_cast<uint32>(can_add_web_page_previews) * CAN_ADD_WEB_PAGE_PREVIEWS) |
                 (static_cast<uint32>(is_member) * IS_MEMBER);
  if (flags == (IS_MEMBER | ALL_RESTRICTED_RIGHTS)) {
    return Member();
  }
  return DialogParticipantStatus(Type::Restricted, flags, fix_until_date(restricted_until_date));
}

DialogParticipantStatus DialogParticipantStatus::Left() {
  return DialogParticipantStatus(Type::Left, ALL_RESTRICTED_RIGHTS, 0);
}

DialogParticipantStatus DialogParticipantStatus::Banned(int32 banned_until_date) {
  return DialogParticipantStatus(Type::Banned, 0, fix_until_date(banned_until_date));
}

DialogParticipantStatus DialogParticipantStatus::GroupAdministrator(bool is_creator) {
  return DialogParticipantStatus::Administrator(is_creator, true, false, false, true, true, false, true, false, false);
}

DialogParticipantStatus DialogParticipantStatus::ChannelAdministrator(bool is_creator, bool is_megagroup) {
  if (is_megagroup) {
    return DialogParticipantStatus::Administrator(is_creator, true, false, false, true, true, false, true, true, false);
  } else {
    return DialogParticipantStatus::Administrator(is_creator, false, true, true, true, false, false, true, false,
                                                  false);
  }
}

tl_object_ptr<td_api::ChatMemberStatus> DialogParticipantStatus::get_chat_member_status_object() const {
  switch (type_) {
    case Type::Creator:
      return make_tl_object<td_api::chatMemberStatusCreator>(is_member());
    case Type::Administrator:
      return make_tl_object<td_api::chatMemberStatusAdministrator>(
          can_be_edited(), can_change_info_and_settings(), can_post_messages(), can_edit_messages(),
          can_delete_messages(), can_invite_users() || can_export_dialog_invite_link(), can_restrict_members(),
          can_pin_messages(), can_promote_members());
    case Type::Member:
      return make_tl_object<td_api::chatMemberStatusMember>();
    case Type::Restricted:
      return make_tl_object<td_api::chatMemberStatusRestricted>(
          is_member(), until_date_, can_send_messages(), can_send_media(),
          can_send_stickers() && can_send_animations() && can_send_games() && can_use_inline_bots(),
          can_add_web_page_previews());
    case Type::Left:
      return make_tl_object<td_api::chatMemberStatusLeft>();
    case Type::Banned:
      return make_tl_object<td_api::chatMemberStatusBanned>(until_date_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::channelAdminRights> DialogParticipantStatus::get_channel_admin_rights() const {
  int32 flags = 0;
  if (can_change_info_and_settings()) {
    flags |= telegram_api::channelAdminRights::CHANGE_INFO_MASK;
  }
  if (can_post_messages()) {
    flags |= telegram_api::channelAdminRights::POST_MESSAGES_MASK;
  }
  if (can_edit_messages()) {
    flags |= telegram_api::channelAdminRights::EDIT_MESSAGES_MASK;
  }
  if (can_delete_messages()) {
    flags |= telegram_api::channelAdminRights::DELETE_MESSAGES_MASK;
  }
  if (can_invite_users()) {
    flags |= telegram_api::channelAdminRights::INVITE_USERS_MASK;
  }
  if (can_export_dialog_invite_link()) {
    flags |= telegram_api::channelAdminRights::INVITE_LINK_MASK;
  }
  if (can_restrict_members()) {
    flags |= telegram_api::channelAdminRights::BAN_USERS_MASK;
  }
  if (can_pin_messages()) {
    flags |= telegram_api::channelAdminRights::PIN_MESSAGES_MASK;
  }
  if (can_promote_members()) {
    flags |= telegram_api::channelAdminRights::ADD_ADMINS_MASK;
  }

  LOG(INFO) << "Create channel admin rights " << flags;
  return make_tl_object<telegram_api::channelAdminRights>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/);
}

tl_object_ptr<telegram_api::channelBannedRights> DialogParticipantStatus::get_channel_banned_rights() const {
  int32 flags = 0;
  if (type_ == Type::Banned) {
    flags |= telegram_api::channelBannedRights::VIEW_MESSAGES_MASK;
  }
  if (!can_send_messages()) {
    flags |= telegram_api::channelBannedRights::SEND_MESSAGES_MASK;
  }
  if (!can_send_media()) {
    flags |= telegram_api::channelBannedRights::SEND_MEDIA_MASK;
  }
  if (!can_send_stickers()) {
    flags |= telegram_api::channelBannedRights::SEND_STICKERS_MASK;
  }
  if (!can_send_animations()) {
    flags |= telegram_api::channelBannedRights::SEND_GIFS_MASK;
  }
  if (!can_send_games()) {
    flags |= telegram_api::channelBannedRights::SEND_GAMES_MASK;
  }
  if (!can_use_inline_bots()) {
    flags |= telegram_api::channelBannedRights::SEND_INLINE_MASK;
  }
  if (!can_add_web_page_previews()) {
    flags |= telegram_api::channelBannedRights::EMBED_LINKS_MASK;
  }

  LOG(INFO) << "Create channel banned rights " << flags << " until " << until_date_;
  return make_tl_object<telegram_api::channelBannedRights>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, until_date_);
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
      flags_ |= ALL_RESTRICTED_RIGHTS;
    } else if (type_ == Type::Banned) {
      type_ = Type::Left;
    } else {
      UNREACHABLE();
    }
  }
}

bool operator==(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs) {
  return lhs.type_ == rhs.type_ && lhs.flags_ == rhs.flags_ && lhs.until_date_ == rhs.until_date_;
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
      return string_builder;
    case DialogParticipantStatus::Type::Administrator:
      string_builder << "Administrator: ";
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
      if (!status.can_use_inline_bots()) {
        string_builder << "(inline bots)";
      }
      if (!status.can_add_web_page_previews()) {
        string_builder << "(links)";
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
      return DialogParticipantStatus::Creator(st->is_member_);
    }
    case td_api::chatMemberStatusAdministrator::ID: {
      auto st = static_cast<const td_api::chatMemberStatusAdministrator *>(status.get());
      return DialogParticipantStatus::Administrator(
          st->can_be_edited_, st->can_change_info_, st->can_post_messages_, st->can_edit_messages_,
          st->can_delete_messages_, st->can_invite_users_, st->can_invite_users_, st->can_restrict_members_,
          st->can_pin_messages_, st->can_promote_members_);
    }
    case td_api::chatMemberStatusMember::ID:
      return DialogParticipantStatus::Member();
    case td_api::chatMemberStatusRestricted::ID: {
      auto st = static_cast<const td_api::chatMemberStatusRestricted *>(status.get());
      bool can_send_media =
          st->can_send_media_messages_ || st->can_send_other_messages_ || st->can_add_web_page_previews_;
      return DialogParticipantStatus::Restricted(
          st->is_member_, st->restricted_until_date_, st->can_send_messages_ || can_send_media, can_send_media,
          st->can_send_other_messages_, st->can_send_other_messages_, st->can_send_other_messages_,
          st->can_send_other_messages_, st->can_add_web_page_previews_);
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

DialogParticipantStatus get_dialog_participant_status(
    bool can_be_edited, const tl_object_ptr<telegram_api::channelAdminRights> &admin_rights) {
  bool can_change_info = (admin_rights->flags_ & telegram_api::channelAdminRights::CHANGE_INFO_MASK) != 0;
  bool can_post_messages = (admin_rights->flags_ & telegram_api::channelAdminRights::POST_MESSAGES_MASK) != 0;
  bool can_edit_messages = (admin_rights->flags_ & telegram_api::channelAdminRights::EDIT_MESSAGES_MASK) != 0;
  bool can_delete_messages = (admin_rights->flags_ & telegram_api::channelAdminRights::DELETE_MESSAGES_MASK) != 0;
  bool can_invite_users = (admin_rights->flags_ & telegram_api::channelAdminRights::INVITE_USERS_MASK) != 0;
  bool can_export_invite_link = (admin_rights->flags_ & telegram_api::channelAdminRights::INVITE_LINK_MASK) != 0;
  bool can_restrict_members = (admin_rights->flags_ & telegram_api::channelAdminRights::BAN_USERS_MASK) != 0;
  bool can_pin_messages = (admin_rights->flags_ & telegram_api::channelAdminRights::PIN_MESSAGES_MASK) != 0;
  bool can_promote_members = (admin_rights->flags_ & telegram_api::channelAdminRights::ADD_ADMINS_MASK) != 0;
  return DialogParticipantStatus::Administrator(can_be_edited, can_change_info, can_post_messages, can_edit_messages,
                                                can_delete_messages, can_invite_users, can_export_invite_link,
                                                can_restrict_members, can_pin_messages, can_promote_members);
}

DialogParticipantStatus get_dialog_participant_status(
    bool is_member, const tl_object_ptr<telegram_api::channelBannedRights> &banned_rights) {
  bool can_view_messages = (banned_rights->flags_ & telegram_api::channelBannedRights::VIEW_MESSAGES_MASK) == 0;
  if (!can_view_messages) {
    return DialogParticipantStatus::Banned(banned_rights->until_date_);
  }
  bool can_send_messages = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_MESSAGES_MASK) == 0;
  bool can_send_media_messages = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_MEDIA_MASK) == 0;
  bool can_send_stickers = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_STICKERS_MASK) == 0;
  bool can_send_animations = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_GIFS_MASK) == 0;
  bool can_send_games = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_GAMES_MASK) == 0;
  bool can_use_inline_bots = (banned_rights->flags_ & telegram_api::channelBannedRights::SEND_INLINE_MASK) == 0;
  bool can_add_web_page_previews = (banned_rights->flags_ & telegram_api::channelBannedRights::EMBED_LINKS_MASK) == 0;
  return DialogParticipantStatus::Restricted(is_member, banned_rights->until_date_, can_send_messages,
                                             can_send_media_messages, can_send_stickers, can_send_animations,
                                             can_send_games, can_use_inline_bots, can_add_web_page_previews);
}

tl_object_ptr<telegram_api::ChannelParticipantsFilter>
ChannelParticipantsFilter::get_input_channel_participants_filter() const {
  switch (type) {
    case Type::Recent:
      return make_tl_object<telegram_api::channelParticipantsRecent>();
    case Type::Administrators:
      return make_tl_object<telegram_api::channelParticipantsAdmins>();
    case Type::Search:
      return make_tl_object<telegram_api::channelParticipantsSearch>(query);
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
    case td_api::supergroupMembersFilterAdministrators::ID:
      type = Type::Administrators;
      return;
    case td_api::supergroupMembersFilterSearch::ID:
      type = Type::Search;
      query = static_cast<const td_api::supergroupMembersFilterSearch *>(filter.get())->query_;
      return;
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

DialogParticipantsFilter get_dialog_participants_filter(const tl_object_ptr<td_api::ChatMembersFilter> &filter) {
  if (filter == nullptr) {
    return DialogParticipantsFilter::Members;
  }
  switch (filter->get_id()) {
    case td_api::chatMembersFilterAdministrators::ID:
      return DialogParticipantsFilter::Administrators;
    case td_api::chatMembersFilterMembers::ID:
      return DialogParticipantsFilter::Members;
    case td_api::chatMembersFilterRestricted::ID:
      return DialogParticipantsFilter::Restricted;
    case td_api::chatMembersFilterBanned::ID:
      return DialogParticipantsFilter::Banned;
    case td_api::chatMembersFilterBots::ID:
      return DialogParticipantsFilter::Bots;
    default:
      UNREACHABLE();
      return DialogParticipantsFilter::Members;
  }
}

}  // namespace td
