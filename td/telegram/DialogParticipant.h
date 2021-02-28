//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class Td;

class RestrictedRights {
  static constexpr uint32 CAN_SEND_MESSAGES = 1 << 16;
  static constexpr uint32 CAN_SEND_MEDIA = 1 << 17;
  static constexpr uint32 CAN_SEND_STICKERS = 1 << 18;
  static constexpr uint32 CAN_SEND_ANIMATIONS = 1 << 19;
  static constexpr uint32 CAN_SEND_GAMES = 1 << 20;
  static constexpr uint32 CAN_USE_INLINE_BOTS = 1 << 21;
  static constexpr uint32 CAN_ADD_WEB_PAGE_PREVIEWS = 1 << 22;
  static constexpr uint32 CAN_SEND_POLLS = 1 << 23;
  static constexpr uint32 CAN_CHANGE_INFO_AND_SETTINGS = 1 << 24;
  static constexpr uint32 CAN_INVITE_USERS = 1 << 25;
  static constexpr uint32 CAN_PIN_MESSAGES = 1 << 26;

  uint32 flags_;

  friend class DialogParticipantStatus;

 public:
  RestrictedRights(bool can_send_messages, bool can_send_media, bool can_send_stickers, bool can_send_animations,
                   bool can_send_games, bool can_use_inline_bots, bool can_add_web_page_previews, bool can_send_polls,
                   bool can_change_info_and_settings, bool can_invite_users, bool can_pin_messages);

  td_api::object_ptr<td_api::chatPermissions> get_chat_permissions_object() const;

  tl_object_ptr<telegram_api::chatBannedRights> get_chat_banned_rights() const;

  bool can_change_info_and_settings() const {
    return (flags_ & CAN_CHANGE_INFO_AND_SETTINGS) != 0;
  }

  bool can_invite_users() const {
    return (flags_ & CAN_INVITE_USERS) != 0;
  }

  bool can_pin_messages() const {
    return (flags_ & CAN_PIN_MESSAGES) != 0;
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

  bool can_send_polls() const {
    return (flags_ & CAN_SEND_POLLS) != 0;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(flags_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(flags_, parser);
  }

  friend bool operator==(const RestrictedRights &lhs, const RestrictedRights &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const RestrictedRights &status);
};

bool operator==(const RestrictedRights &lhs, const RestrictedRights &rhs);

bool operator!=(const RestrictedRights &lhs, const RestrictedRights &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const RestrictedRights &status);

class DialogParticipantStatus {
  static constexpr uint32 CAN_CHANGE_INFO_AND_SETTINGS_ADMIN = 1 << 0;
  static constexpr uint32 CAN_POST_MESSAGES = 1 << 1;
  static constexpr uint32 CAN_EDIT_MESSAGES = 1 << 2;
  static constexpr uint32 CAN_DELETE_MESSAGES = 1 << 3;
  static constexpr uint32 CAN_INVITE_USERS_ADMIN = 1 << 4;
  // static constexpr uint32 CAN_EXPORT_DIALOG_INVITE_LINK = 1 << 5;
  static constexpr uint32 CAN_RESTRICT_MEMBERS = 1 << 6;
  static constexpr uint32 CAN_PIN_MESSAGES_ADMIN = 1 << 7;
  static constexpr uint32 CAN_PROMOTE_MEMBERS = 1 << 8;
  static constexpr uint32 CAN_MANAGE_CALLS = 1 << 9;
  static constexpr uint32 CAN_MANAGE_DIALOG = 1 << 10;

  static constexpr uint32 CAN_BE_EDITED = 1 << 15;

  static constexpr uint32 CAN_SEND_MESSAGES = 1 << 16;
  static constexpr uint32 CAN_SEND_MEDIA = 1 << 17;
  static constexpr uint32 CAN_SEND_STICKERS = 1 << 18;
  static constexpr uint32 CAN_SEND_ANIMATIONS = 1 << 19;
  static constexpr uint32 CAN_SEND_GAMES = 1 << 20;
  static constexpr uint32 CAN_USE_INLINE_BOTS = 1 << 21;
  static constexpr uint32 CAN_ADD_WEB_PAGE_PREVIEWS = 1 << 22;
  static constexpr uint32 CAN_SEND_POLLS = 1 << 23;
  static constexpr uint32 CAN_CHANGE_INFO_AND_SETTINGS_BANNED = 1 << 24;
  static constexpr uint32 CAN_INVITE_USERS_BANNED = 1 << 25;
  static constexpr uint32 CAN_PIN_MESSAGES_BANNED = 1 << 26;

  static constexpr uint32 IS_MEMBER = 1 << 27;

  static constexpr uint32 IS_ANONYMOUS = 1 << 13;
  static constexpr uint32 HAS_RANK = 1u << 14;
  // bits 28-30 reserved for Type
  static constexpr int TYPE_SHIFT = 28;
  static constexpr uint32 HAS_UNTIL_DATE = 1u << 31;

  static constexpr uint32 ALL_ADMINISTRATOR_RIGHTS = CAN_CHANGE_INFO_AND_SETTINGS_ADMIN | CAN_POST_MESSAGES |
                                                     CAN_EDIT_MESSAGES | CAN_DELETE_MESSAGES | CAN_INVITE_USERS_ADMIN |
                                                     CAN_RESTRICT_MEMBERS | CAN_PIN_MESSAGES_ADMIN |
                                                     CAN_PROMOTE_MEMBERS | CAN_MANAGE_CALLS | CAN_MANAGE_DIALOG;

  static constexpr uint32 ALL_ADMIN_PERMISSION_RIGHTS =
      CAN_CHANGE_INFO_AND_SETTINGS_BANNED | CAN_INVITE_USERS_BANNED | CAN_PIN_MESSAGES_BANNED;

  static constexpr uint32 ALL_RESTRICTED_RIGHTS = CAN_SEND_MESSAGES | CAN_SEND_MEDIA | CAN_SEND_STICKERS |
                                                  CAN_SEND_ANIMATIONS | CAN_SEND_GAMES | CAN_USE_INLINE_BOTS |
                                                  CAN_ADD_WEB_PAGE_PREVIEWS | CAN_SEND_POLLS;

  static constexpr uint32 ALL_PERMISSION_RIGHTS = ALL_RESTRICTED_RIGHTS | ALL_ADMIN_PERMISSION_RIGHTS;

  enum class Type : int32 { Creator, Administrator, Member, Restricted, Left, Banned };
  // all fields are logically const, but should be updated in update_restrictions()
  mutable Type type_;
  mutable uint32 flags_;
  mutable int32 until_date_;  // restricted and banned only
  string rank_;               // creator and administrator only

  static int32 fix_until_date(int32 date);

  DialogParticipantStatus(Type type, uint32 flags, int32 until_date, string rank);

 public:
  static DialogParticipantStatus Creator(bool is_member, bool is_anonymous, string rank);

  static DialogParticipantStatus Administrator(bool is_anonymous, string rank, bool can_be_edited,
                                               bool can_manage_dialog, bool can_change_info, bool can_post_messages,
                                               bool can_edit_messages, bool can_delete_messages, bool can_invite_users,
                                               bool can_restrict_members, bool can_pin_messages,
                                               bool can_promote_members, bool can_manage_calls);

  static DialogParticipantStatus Member();

  static DialogParticipantStatus Restricted(bool is_member, int32 restricted_until_date, bool can_send_messages,
                                            bool can_send_media, bool can_send_stickers, bool can_send_animations,
                                            bool can_send_games, bool can_use_inline_bots,
                                            bool can_add_web_page_previews, bool can_send_polls,
                                            bool can_change_info_and_settings, bool can_invite_users,
                                            bool can_pin_messages);

  static DialogParticipantStatus Left();

  static DialogParticipantStatus Banned(int32 banned_until_date);

  // legacy rights
  static DialogParticipantStatus GroupAdministrator(bool is_creator);

  // legacy rights
  static DialogParticipantStatus ChannelAdministrator(bool is_creator, bool is_megagroup);

  RestrictedRights get_restricted_rights() const;

  DialogParticipantStatus apply_restrictions(RestrictedRights default_restrictions, bool is_bot) const;

  tl_object_ptr<td_api::ChatMemberStatus> get_chat_member_status_object() const;

  tl_object_ptr<telegram_api::chatAdminRights> get_chat_admin_rights() const;

  tl_object_ptr<telegram_api::chatBannedRights> get_chat_banned_rights() const;

  // unrestricts user if restriction time expired. Should be called before all privileges checks
  void update_restrictions() const;

  bool can_manage_dialog() const {
    return (flags_ & CAN_MANAGE_DIALOG) != 0;
  }

  bool can_change_info_and_settings() const {
    return (flags_ & CAN_CHANGE_INFO_AND_SETTINGS_ADMIN) != 0 || (flags_ & CAN_CHANGE_INFO_AND_SETTINGS_BANNED) != 0;
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
    return (flags_ & CAN_INVITE_USERS_ADMIN) != 0 || (flags_ & CAN_INVITE_USERS_BANNED) != 0;
  }

  bool can_restrict_members() const {
    return (flags_ & CAN_RESTRICT_MEMBERS) != 0;
  }

  bool can_pin_messages() const {
    return (flags_ & CAN_PIN_MESSAGES_ADMIN) != 0 || (flags_ & CAN_PIN_MESSAGES_BANNED) != 0;
  }

  bool can_promote_members() const {
    return (flags_ & CAN_PROMOTE_MEMBERS) != 0;
  }

  bool can_manage_calls() const {
    return (flags_ & CAN_MANAGE_CALLS) != 0;
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

  bool can_send_polls() const {
    return (flags_ & CAN_SEND_POLLS) != 0;
  }

  void set_is_member(bool is_member) {
    if (is_member) {
      flags_ |= IS_MEMBER;
    } else {
      flags_ &= ~IS_MEMBER;
    }
  }

  bool is_member() const {
    return (flags_ & IS_MEMBER) != 0;
  }

  bool is_left() const {
    return (flags_ & IS_MEMBER) == 0;
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

  bool is_anonymous() const {
    return (flags_ & IS_ANONYMOUS) != 0;
  }

  const string &get_rank() const {
    return rank_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    uint32 stored_flags = flags_ | (static_cast<uint32>(type_) << TYPE_SHIFT);
    if (until_date_ > 0) {
      stored_flags |= HAS_UNTIL_DATE;
    }
    if (!rank_.empty()) {
      stored_flags |= HAS_RANK;
    }
    td::store(stored_flags, storer);
    if (until_date_ > 0) {
      td::store(until_date_, storer);
    }
    if (!rank_.empty()) {
      td::store(rank_, storer);
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
    if ((stored_flags & HAS_RANK) != 0) {
      td::parse(rank_, parser);
      stored_flags &= ~HAS_RANK;
    }
    type_ = static_cast<Type>(stored_flags >> TYPE_SHIFT);
    flags_ = stored_flags & ((1 << TYPE_SHIFT) - 1);

    if (is_creator()) {
      flags_ |= ALL_ADMINISTRATOR_RIGHTS | ALL_PERMISSION_RIGHTS;
    } else if (is_administrator()) {
      flags_ |= CAN_MANAGE_DIALOG;
    }
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

  DialogParticipant(UserId user_id, UserId inviter_user_id, int32 joined_date, DialogParticipantStatus status);

  DialogParticipant(tl_object_ptr<telegram_api::ChatParticipant> &&participant_ptr, int32 chat_creation_date,
                    bool is_creator);

  explicit DialogParticipant(tl_object_ptr<telegram_api::ChannelParticipant> &&participant_ptr);

  static DialogParticipant left(UserId user_id) {
    return {user_id, UserId(), 0, DialogParticipantStatus::Left()};
  }

  bool is_valid() const;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(user_id, storer);
    td::store(inviter_user_id, storer);
    td::store(joined_date, storer);
    td::store(status, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(user_id, parser);
    td::parse(inviter_user_id, parser);
    td::parse(joined_date, parser);
    td::parse(status, parser);
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipant &dialog_participant);

struct DialogParticipants {
  int32 total_count_ = 0;
  vector<DialogParticipant> participants_;

  DialogParticipants() = default;
  DialogParticipants(int32 total_count, vector<DialogParticipant> &&participants)
      : total_count_(total_count), participants_(std::move(participants)) {
  }

  td_api::object_ptr<td_api::chatMembers> get_chat_members_object(Td *td) const;
};

class ChannelParticipantsFilter {
  enum class Type : int32 { Recent, Contacts, Administrators, Search, Mention, Restricted, Banned, Bots } type;
  string query;
  MessageId top_thread_message_id;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantsFilter &filter);

 public:
  explicit ChannelParticipantsFilter(const tl_object_ptr<td_api::SupergroupMembersFilter> &filter);

  tl_object_ptr<telegram_api::ChannelParticipantsFilter> get_input_channel_participants_filter() const;

  bool is_administrators() const {
    return type == Type::Administrators;
  }

  bool is_bots() const {
    return type == Type::Bots;
  }

  bool is_recent() const {
    return type == Type::Recent;
  }

  bool is_contacts() const {
    return type == Type::Contacts;
  }

  bool is_search() const {
    return type == Type::Search;
  }

  bool is_restricted() const {
    return type == Type::Restricted;
  }

  bool is_banned() const {
    return type == Type::Banned;
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantsFilter &filter);

class DialogParticipantsFilter {
 public:
  enum class Type : int32 { Contacts, Administrators, Members, Restricted, Banned, Mention, Bots };
  Type type;
  MessageId top_thread_message_id;

  explicit DialogParticipantsFilter(Type type, MessageId top_thread_message_id = MessageId())
      : type(type), top_thread_message_id(top_thread_message_id) {
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantsFilter &filter);

DialogParticipantsFilter get_dialog_participants_filter(const tl_object_ptr<td_api::ChatMembersFilter> &filter);

DialogParticipantStatus get_dialog_participant_status(const tl_object_ptr<td_api::ChatMemberStatus> &status);

DialogParticipantStatus get_dialog_participant_status(bool can_be_edited,
                                                      const tl_object_ptr<telegram_api::chatAdminRights> &admin_rights,
                                                      string rank);

DialogParticipantStatus get_dialog_participant_status(
    bool is_member, const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights);

RestrictedRights get_restricted_rights(const tl_object_ptr<telegram_api::chatBannedRights> &banned_rights);

RestrictedRights get_restricted_rights(const td_api::object_ptr<td_api::chatPermissions> &permissions);

}  // namespace td
