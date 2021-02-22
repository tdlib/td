//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedAction.h"

#include "td/telegram/ChannelId.h"

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

SuggestedAction::SuggestedAction(Slice action_str, DialogId dialog_id) {
  CHECK(dialog_id.is_valid());
  if (action_str == Slice("CONVERT_GIGAGROUP")) {
    type_ = Type::ConvertToGigagroup;
    dialog_id_ = dialog_id;
  }
}

SuggestedAction::SuggestedAction(const td_api::object_ptr<td_api::SuggestedAction> &suggested_action) {
  if (suggested_action == nullptr) {
    return;
  }
  switch (suggested_action->get_id()) {
    case td_api::suggestedActionEnableArchiveAndMuteNewChats::ID:
      init(Type::EnableArchiveAndMuteNewChats);
      break;
    case td_api::suggestedActionCheckPhoneNumber::ID:
      init(Type::CheckPhoneNumber);
      break;
    case td_api::suggestedActionSeeTicksHint::ID:
      init(Type::SeeTicksHint);
      break;
    case td_api::suggestedActionConvertToBroadcastGroup::ID: {
      auto action = static_cast<const td_api::suggestedActionConvertToBroadcastGroup *>(suggested_action.get());
      type_ = Type::ConvertToGigagroup;
      dialog_id_ = DialogId(ChannelId(action->supergroup_id_));
      break;
    }
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
    case Type::ConvertToGigagroup:
      return "CONVERT_GIGAGROUP";
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
    case Type::ConvertToGigagroup:
      return td_api::make_object<td_api::suggestedActionConvertToBroadcastGroup>(dialog_id_.get_channel_id().get());
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
