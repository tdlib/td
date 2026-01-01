//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class ChannelParticipantFilter {
  enum class Type : int32 { Recent, Contacts, Administrators, Search, Mention, Restricted, Banned, Bots };
  Type type_;
  string query_;
  MessageTopic message_topic_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantFilter &filter);

  friend class DialogParticipantFilter;

 public:
  ChannelParticipantFilter(Td *td, DialogId dialog_id,
                           const td_api::object_ptr<td_api::SupergroupMembersFilter> &filter);

  telegram_api::object_ptr<telegram_api::ChannelParticipantsFilter> get_input_channel_participants_filter() const;

  static ChannelParticipantFilter recent();

  bool is_administrators() const {
    return type_ == Type::Administrators;
  }

  bool is_bots() const {
    return type_ == Type::Bots;
  }

  bool is_recent() const {
    return type_ == Type::Recent;
  }

  bool is_contacts() const {
    return type_ == Type::Contacts;
  }

  bool is_search() const {
    return type_ == Type::Search;
  }

  bool is_restricted() const {
    return type_ == Type::Restricted;
  }

  bool is_banned() const {
    return type_ == Type::Banned;
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantFilter &filter);

}  // namespace td
