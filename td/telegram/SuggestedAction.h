//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

struct SuggestedAction {
  enum class Type : int32 { Empty, EnableArchiveAndMuteNewChats, CheckPhoneNumber, SeeTicksHint };
  Type type_ = Type::Empty;

  void init(Type type);

  SuggestedAction() = default;

  explicit SuggestedAction(Type type) : type_(type) {
  }

  explicit SuggestedAction(Slice action_str);

  explicit SuggestedAction(const td_api::object_ptr<td_api::SuggestedAction> &action_object);

  string get_suggested_action_str() const;

  td_api::object_ptr<td_api::SuggestedAction> get_suggested_action_object() const;
};

inline bool operator==(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  return lhs.type_ == rhs.type_;
}

inline bool operator!=(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  return static_cast<int32>(lhs.type_) < static_cast<int32>(rhs.type_);
}

td_api::object_ptr<td_api::updateSuggestedActions> get_update_suggested_actions_object(
    const vector<SuggestedAction> &added_actions, const vector<SuggestedAction> &removed_actions);

}  // namespace td
