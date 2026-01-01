//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelParticipantFilter.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct DialogParticipant;
class Td;

class DialogParticipantFilter {
  enum class Type : int32 { Contacts, Administrators, Members, Restricted, Banned, Mention, Bots };
  Type type_;
  MessageTopic message_topic_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantFilter &filter);

 public:
  DialogParticipantFilter(Td *td, DialogId dialog_id, const td_api::object_ptr<td_api::ChatMembersFilter> &filter);

  ChannelParticipantFilter as_channel_participant_filter(const string &query) const;

  bool has_query() const;

  bool is_dialog_participant_suitable(const Td *td, const DialogParticipant &participant) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const DialogParticipantFilter &filter);

}  // namespace td
