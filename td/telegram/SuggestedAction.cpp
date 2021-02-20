//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedAction.h"

#include "td/utils/algorithm.h"

namespace td {

SuggestedAction get_suggested_action(Slice action_str) {
  if (action_str == Slice("AUTOARCHIVE_POPULAR")) {
    return SuggestedAction::EnableArchiveAndMuteNewChats;
  }
  if (action_str == Slice("NEWCOMER_TICKS")) {
    return SuggestedAction::SeeTicksHint;
  }
  return SuggestedAction::Empty;
}

string get_suggested_action_str(SuggestedAction action) {
  switch (action) {
    case SuggestedAction::EnableArchiveAndMuteNewChats:
      return "AUTOARCHIVE_POPULAR";
    case SuggestedAction::SeeTicksHint:
      return "NEWCOMER_TICKS";
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
    case td_api::suggestedActionSeeTicksHint::ID:
      return SuggestedAction::SeeTicksHint;
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
    case SuggestedAction::SeeTicksHint:
      return td_api::make_object<td_api::suggestedActionSeeTicksHint>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::updateSuggestedActions> get_update_suggested_actions_object(
    const vector<SuggestedAction> &added_actions, const vector<SuggestedAction> &removed_actions) {
  return td_api::make_object<td_api::updateSuggestedActions>(transform(added_actions, get_suggested_action_object),
                                                             transform(removed_actions, get_suggested_action_object));
}

}  // namespace td
