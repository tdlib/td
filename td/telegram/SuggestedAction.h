//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

struct SuggestedAction {
  enum class Type : int32 { Empty, EnableArchiveAndMuteNewChats, CheckPhoneNumber, SeeTicksHint, ConvertToGigagroup };
  Type type_ = Type::Empty;
  DialogId dialog_id_;

  void init(Type type);

  SuggestedAction() = default;

  explicit SuggestedAction(Type type, DialogId dialog_id = DialogId()) : type_(type), dialog_id_(dialog_id) {
  }

  explicit SuggestedAction(Slice action_str);

  SuggestedAction(Slice action_str, DialogId dialog_id);

  explicit SuggestedAction(const td_api::object_ptr<td_api::SuggestedAction> &suggested_action);

  bool is_empty() const {
    return type_ == Type::Empty;
  }

  string get_suggested_action_str() const;

  td_api::object_ptr<td_api::SuggestedAction> get_suggested_action_object() const;
};

inline bool operator==(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  CHECK(lhs.dialog_id_ == rhs.dialog_id_);
  return lhs.type_ == rhs.type_;
}

inline bool operator!=(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const SuggestedAction &lhs, const SuggestedAction &rhs) {
  CHECK(lhs.dialog_id_ == rhs.dialog_id_);
  return static_cast<int32>(lhs.type_) < static_cast<int32>(rhs.type_);
}

td_api::object_ptr<td_api::updateSuggestedActions> get_update_suggested_actions_object(
    const vector<SuggestedAction> &added_actions, const vector<SuggestedAction> &removed_actions);

void update_suggested_actions(vector<SuggestedAction> &suggested_actions,
                              vector<SuggestedAction> &&new_suggested_actions);

void remove_suggested_action(vector<SuggestedAction> &suggested_actions, SuggestedAction suggested_action);

}  // namespace td
