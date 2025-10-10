//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ChannelParticipantFilter.h"

namespace td {

telegram_api::object_ptr<telegram_api::ChannelParticipantsFilter>
ChannelParticipantFilter::get_input_channel_participants_filter() const {
  switch (type_) {
    case Type::Recent:
      return telegram_api::make_object<telegram_api::channelParticipantsRecent>();
    case Type::Contacts:
      return telegram_api::make_object<telegram_api::channelParticipantsContacts>(query_);
    case Type::Administrators:
      return telegram_api::make_object<telegram_api::channelParticipantsAdmins>();
    case Type::Search:
      return telegram_api::make_object<telegram_api::channelParticipantsSearch>(query_);
    case Type::Mention: {
      int32 flags = 0;
      if (!query_.empty()) {
        flags |= telegram_api::channelParticipantsMentions::Q_MASK;
      }
      auto top_msg_id = message_topic_.get_input_top_msg_id();
      if (top_msg_id != 0) {
        flags |= telegram_api::channelParticipantsMentions::TOP_MSG_ID_MASK;
      }
      return telegram_api::make_object<telegram_api::channelParticipantsMentions>(flags, query_, top_msg_id);
    }
    case Type::Restricted:
      return telegram_api::make_object<telegram_api::channelParticipantsBanned>(query_);
    case Type::Banned:
      return telegram_api::make_object<telegram_api::channelParticipantsKicked>(query_);
    case Type::Bots:
      return telegram_api::make_object<telegram_api::channelParticipantsBots>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

ChannelParticipantFilter::ChannelParticipantFilter(Td *td, DialogId dialog_id,
                                                   const td_api::object_ptr<td_api::SupergroupMembersFilter> &filter) {
  if (filter == nullptr) {
    type_ = Type::Recent;
    return;
  }
  switch (filter->get_id()) {
    case td_api::supergroupMembersFilterRecent::ID:
      type_ = Type::Recent;
      return;
    case td_api::supergroupMembersFilterContacts::ID:
      type_ = Type::Contacts;
      query_ = static_cast<const td_api::supergroupMembersFilterContacts *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterAdministrators::ID:
      type_ = Type::Administrators;
      return;
    case td_api::supergroupMembersFilterSearch::ID:
      type_ = Type::Search;
      query_ = static_cast<const td_api::supergroupMembersFilterSearch *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterMention::ID: {
      auto mention_filter = static_cast<const td_api::supergroupMembersFilterMention *>(filter.get());
      type_ = Type::Mention;
      query_ = mention_filter->query_;
      auto r_message_topic = MessageTopic::get_message_topic(td, dialog_id, mention_filter->topic_id_);
      if (r_message_topic.is_ok()) {
        message_topic_ = r_message_topic.move_as_ok();
      }
      return;
    }
    case td_api::supergroupMembersFilterRestricted::ID:
      type_ = Type::Restricted;
      query_ = static_cast<const td_api::supergroupMembersFilterRestricted *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBanned::ID:
      type_ = Type::Banned;
      query_ = static_cast<const td_api::supergroupMembersFilterBanned *>(filter.get())->query_;
      return;
    case td_api::supergroupMembersFilterBots::ID:
      type_ = Type::Bots;
      return;
    default:
      UNREACHABLE();
      type_ = Type::Recent;
  }
}

ChannelParticipantFilter ChannelParticipantFilter::recent() {
  return ChannelParticipantFilter(nullptr, DialogId(), nullptr);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChannelParticipantFilter &filter) {
  switch (filter.type_) {
    case ChannelParticipantFilter::Type::Recent:
      return string_builder << "Recent";
    case ChannelParticipantFilter::Type::Contacts:
      return string_builder << "Contacts \"" << filter.query_ << '"';
    case ChannelParticipantFilter::Type::Administrators:
      return string_builder << "Administrators";
    case ChannelParticipantFilter::Type::Search:
      return string_builder << "Search \"" << filter.query_ << '"';
    case ChannelParticipantFilter::Type::Mention:
      return string_builder << "Mention \"" << filter.query_ << "\" in " << filter.message_topic_;
    case ChannelParticipantFilter::Type::Restricted:
      return string_builder << "Restricted \"" << filter.query_ << '"';
    case ChannelParticipantFilter::Type::Banned:
      return string_builder << "Banned \"" << filter.query_ << '"';
    case ChannelParticipantFilter::Type::Bots:
      return string_builder << "Bots";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
