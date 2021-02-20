//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedAction.h"

#include "td/utils/algorithm.h"

namespace td {

void SuggestedAction::init(Type type) {
  type_ = type;
}

SuggestedAction::SuggestedAction(Slice action_str) {
  if (action_str == Slice("AUTOARCHIVE_POPULAR")) {
    init(Type::EnableArchiveAndMuteNewChats);
  } else if (action_str == Slice("NEWCOMER_TICKS")) {
    init(Type::SeeTicksHint);
  }
}

SuggestedAction::SuggestedAction(const td_api::object_ptr<td_api::SuggestedAction> &action_object) {
  if (action_object == nullptr) {
    return;
  }
  switch (action_object->get_id()) {
    case td_api::suggestedActionEnableArchiveAndMuteNewChats::ID:
      init(Type::EnableArchiveAndMuteNewChats);
      break;
    case td_api::suggestedActionCheckPhoneNumber::ID:
      init(Type::CheckPhoneNumber);
      break;
    case td_api::suggestedActionSeeTicksHint::ID:
      init(Type::SeeTicksHint);
      break;
    default:
      UNREACHABLE();
  }
}

string SuggestedAction::get_suggested_action_str() const {
  switch (type_) {
    case Type::EnableArchiveAndMuteNewChats:
      return "AUTOARCHIVE_POPULAR";
    case Type::SeeTicksHint:
      return "NEWCOMER_TICKS";
    default:
      return string();
  }
}

td_api::object_ptr<td_api::SuggestedAction> SuggestedAction::get_suggested_action_object() const {
  switch (type_) {
    case Type::Empty:
      return nullptr;
    case Type::EnableArchiveAndMuteNewChats:
      return td_api::make_object<td_api::suggestedActionEnableArchiveAndMuteNewChats>();
    case Type::CheckPhoneNumber:
      return td_api::make_object<td_api::suggestedActionCheckPhoneNumber>();
    case Type::SeeTicksHint:
      return td_api::make_object<td_api::suggestedActionSeeTicksHint>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::updateSuggestedActions> get_update_suggested_actions_object(
    const vector<SuggestedAction> &added_actions, const vector<SuggestedAction> &removed_actions) {
  auto get_object = [](const SuggestedAction &action) {
    return action.get_suggested_action_object();
  };
  return td_api::make_object<td_api::updateSuggestedActions>(transform(added_actions, get_object),
                                                             transform(removed_actions, get_object));
}

}  // namespace td
