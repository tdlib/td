//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class DialogParticipantStatus {
  static constexpr uint32 CAN_CHANGE_INFO_AND_SETTINGS = 1 << 0;
  static constexpr uint32 CAN_POST_MESSAGES = 1 << 1;
  static constexpr uint32 CAN_EDIT_MESSAGES = 1 << 2;
  static constexpr uint32 CAN_DELETE_MESSAGES = 1 << 3;
  static constexpr uint32 CAN_INVITE_USERS = 1 << 4;
  static constexpr uint32 CAN_EXPORT_DIALOG_INVITE_LINK = 1 << 5;
  static constexpr uint32 CAN_RESTRICT_MEMBERS = 1 << 6;
  static constexpr uint32 CAN_PIN_MESSAGES = 1 << 7;
  static constexpr uint32 CAN_PROMOTE_MEMBERS = 1 << 8;

  static constexpr uint32 CAN_BE_EDITED = 1 << 15;

  static constexpr uint32 CAN_SEND_MESSAGES = 1 << 16;
  static constexpr uint32 CAN_SEND_MEDIA = 1 << 17;
  static constexpr uint32 CAN_SEND_STICKERS = 1 << 18;
  static constexpr uint32 CAN_SEND_ANIMATIONS = 1 << 19;
  static constexpr uint32 CAN_SEND_GAMES = 1 << 20;
  static constexpr uint32 CAN_USE_INLINE_BOTS = 1 << 21;
  static constexpr uint32 CAN_ADD_WEB_PAGE_PREVIEWS = 1 << 22;

  static constexpr uint32 IS_MEMBER = 1 << 27;

  // bits 28-31 reserved for Type and until_date flag
  static constexpr int TYPE_SHIFT = 28;
  static constexpr uint32 HAS_UNTIL_DATE = 1u << 31;

  static constexpr uint32 ALL_ADMINISTRATOR_RIGHTS =
      CAN_CHANGE_INFO_AND_SETTINGS | CAN_POST_MESSAGES | CAN_EDIT_MESSAGES | CAN_DELETE_MESSAGES | CAN_INVITE_USERS |
      CAN_EXPORT_DIALOG_INVITE_LINK | CAN_RESTRICT_MEMBERS | CAN_PIN_MESSAGES | CAN_PROMOTE_MEMBERS;

  static constexpr uint32 ALL_RESTRICTED_RIGHTS = CAN_SEND_MESSAGES | CAN_SEND_MEDIA | CAN_SEND_STICKERS |
                                                  CAN_SEND_ANIMATIONS | CAN_SEND_GAMES | CAN_USE_INLINE_BOTS |
                                                  CAN_ADD_WEB_PAGE_PREVIEWS;

  enum class Type : int32 { Creator, Administrator, Member, Restricted, Left, Banned };
  // all fields are logically const, but should be updated in update_restrictions()
  mutable Type type_;
  mutable uint32 flags_;
  mutable int32 until_date_;  // restricted and banned only

  static int32 fix_until_date(int32 date);

  DialogParticipantStatus(Type type, uint32 flags, int32 until_date)
      : type_(type), flags_(flags), until_date_(until_date) {
  }

 public:
  static DialogParticipantStatus Creator(bool is_member);

  static DialogParticipantStatus Administrator(bool can_be_edited, bool can_change_info, bool can_post_messages,
                                               bool can_edit_messages, bool can_delete_messages, bool can_invite_users,
                                               bool can_export_dialog_invite_link, bool can_restrict_members,
                                               bool can_pin_messages, bool can_promote_members);

  static DialogParticipantStatus Member();

  static DialogParticipantStatus Restricted(bool is_member, int32 restricted_until_date, bool can_send_messages,
                                            bool can_send_media, bool can_send_stickers, bool can_send_animations,
                                            bool can_send_games, bool can_use_inline_bots,
                                            bool can_add_web_page_previews);

  static DialogParticipantStatus Left();

  static DialogParticipantStatus Banned(int32 banned_until_date);

  // legacy rights
  static DialogParticipantStatus GroupAdministrator(bool is_creator);

  // legacy rights
  static DialogParticipantStatus ChannelAdministrator(bool is_creator, bool is_megagroup);

  tl_object_ptr<td_api::ChatMemberStatus> get_chat_member_status_object() const;

  tl_object_ptr<telegram_api::channelAdminRights> get_channel_admin_rights() const;

  tl_object_ptr<telegram_api::channelBannedRights> get_channel_banned_rights() const;

  // unrestricts user if restriction time expired. Should be called before all privileges checks
  void update_restrictions() const;

  bool can_change_info_and_settings() const {
    return (flags_ & CAN_CHANGE_INFO_AND_SETTINGS) != 0;
  }

  bool can_post_messages() const {
    return (flags_ & CAN_POST_MESSAGES) != 0;
  }

  bool can_edit_messages() const {
    return (flags_ & CAN_EDIT_MESSAGES) != 0;
  }

  bool can_delete_messages() const {
    return (flags_ & CAN_DELETE_MESSAGES) != 0;
  }

  bool can_invite_users() const {
    return (flags_ & CAN_INVITE_USERS) != 0;
  }

  bool can_export_dialog_invite_link() const {
    return (flags_ & CAN_EXPORT_DIALOG_INVITE_LINK) != 0;
  }

  bool can_restrict_members() const {
    return (flags_ & CAN_RESTRICT_MEMBERS) != 0;
  }

  bool can_pin_messages() const {
    return (flags_ & CAN_PIN_MESSAGES) != 0;
  }

  bool can_promote_members() const {
    return (flags_ & CAN_PROMOTE_MEMBERS) != 0;
  }

  bool can_be_edited() const {
    return (flags_ & CAN_BE_EDITED) != 0;
  }

  bool can_send_messages() const {
    return (flags_ & CAN_SEND_MESSAGES) != 0;
  }

  bool can_send_media() const {
    return (flags_ & CAN_SEND_MEDIA) != 0;
  }

  bool can_send_stickers() const {
    return (flags_ & CAN_SEND_STICKERS) != 0;
  }

  bool can_send_animations() const {
    return (flags_ & CAN_SEND_ANIMATIONS) != 0;
  }

  bool can_send_games() const {
    return (flags_ & CAN_SEND_GAMES) != 0;
  }

  bool can_use_inline_bots() const {
    return (flags_ & CAN_USE_INLINE_BOTS) != 0;
  }

  bool can_add_web_page_previews() const {
    return (flags_ & CAN_ADD_WEB_PAGE_PREVIEWS) != 0;
  }

  bool is_member() const {
    return (flags_ & IS_MEMBER) != 0;
  }

  bool is_creator() const {
    return type_ == Type::Creator;
  }

  bool is_administrator() const {
    return type_ == Type::Administrator || type_ == Type::Creator;
  }

  bool is_restricted() const {
    return type_ == Type::Restricted;
  }

  bool is_banned() const {
    return type_ == Type::Banned;
  }

  int32 get_until_date() const {
    return until_date_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    uint32 stored_flags = flags_ | (static_cast<uint32>(type_) << TYPE_SHIFT);
    if (until_date_ > 0) {
      stored_flags |= HAS_UNTIL_DATE;
    }
    td::store(stored_flags, storer);
    if (until_date_ > 0) {
      td::store(until_date_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    uint32 stored_flags;
    td::parse(stored_flags, parser);
    if ((stored_flags & HAS_UNTIL_DATE) != 0) {
      td::parse(until_date_, parser);
      stored_flags &= ~HAS_UNTIL_DATE;
    }
    type_ = static_cast<Type>(stored_flags >> TYPE_SHIFT);
    flags_ = stored_flags & ((1 << TYPE_SHIFT) - 1);
  }

  friend bool operator==(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantStatus &status);
};

bool operator==(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs);

bool operator!=(const DialogParticipantStatus &lhs, const DialogParticipantStatus &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantStatus &status);

struct DialogParticipant {
  UserId user_id;
  UserId inviter_user_id;
  int32 joined_date = 0;
  DialogParticipantStatus status = DialogParticipantStatus::Left();

  DialogParticipant() = default;

  DialogParticipant(UserId user_id, UserId inviter_user_id, int32 joined_date, DialogParticipantStatus status)
      : user_id(user_id), inviter_user_id(inviter_user_id), joined_date(joined_date), status(status) {
  }
};

class ChannelParticipantsFilter {
  enum class Type : int32 { Recent, Administrators, Search, Restricted, Banned, Bots } type;
  string query;

 public:
  explicit ChannelParticipantsFilter(const tl_object_ptr<td_api::SupergroupMembersFilter> &filter);

  tl_object_ptr<telegram_api::ChannelParticipantsFilter> get_input_channel_participants_filter() const;

  bool is_administrators() const {
    return type == Type::Administrators;
  }
};

enum class DialogParticipantsFilter : int32 { Administrators, Members, Restricted, Banned, Bots };

DialogParticipantsFilter get_dialog_participants_filter(const tl_object_ptr<td_api::ChatMembersFilter> &filter);

DialogParticipantStatus get_dialog_participant_status(const tl_object_ptr<td_api::ChatMemberStatus> &status);

DialogParticipantStatus get_dialog_participant_status(
    bool can_be_edited, const tl_object_ptr<telegram_api::channelAdminRights> &admin_rights);

DialogParticipantStatus get_dialog_participant_status(
    bool is_member, const tl_object_ptr<telegram_api::channelBannedRights> &banned_rights);

}  // namespace td
