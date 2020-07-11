//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedAction.h"

namespace td {

SuggestedAction get_suggested_action(Slice action_str) {
  if (action_str == Slice("AUTOARCHIVE_POPULAR")) {
    return SuggestedAction::EnableArchiveAndMuteNewChats;
  }
  return SuggestedAction::Empty;
}

string get_suggested_action_str(SuggestedAction action) {
  switch (action) {
    case SuggestedAction::EnableArchiveAndMuteNewChats:
      return "AUTOARCHIVE_POPULAR";
    default:
      return string();
  }
}

SuggestedAction get_suggested_action(const td_api::object_ptr<td_api::SuggestedAction> &action_object) {
  if (action_object == nullptr) {
    return SuggestedAction::Empty;
  }
  switch (action_object->get_id()) {
    case td_api::suggestedActionEnableArchiveAndMuteNewChats::ID:
      return SuggestedAction::EnableArchiveAndMuteNewChats;
    case td_api::suggestedActionCheckPhoneNumber::ID:
      return SuggestedAction::CheckPhoneNumber;
    default:
      UNREACHABLE();
      return SuggestedAction::Empty;
  }
}

td_api::object_ptr<td_api::SuggestedAction> get_suggested_action_object(SuggestedAction action) {
  switch (action) {
    case SuggestedAction::Empty:
      return nullptr;
    case SuggestedAction::EnableArchiveAndMuteNewChats:
      return td_api::make_object<td_api::suggestedActionEnableArchiveAndMuteNewChats>();
    case SuggestedAction::CheckPhoneNumber:
      return td_api::make_object<td_api::suggestedActionCheckPhoneNumber>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
