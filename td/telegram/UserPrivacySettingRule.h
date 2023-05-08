//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class UserPrivacySettingRule {
 public:
  UserPrivacySettingRule() = default;

  explicit UserPrivacySettingRule(const td_api::UserPrivacySettingRule &rule);

  static Result<UserPrivacySettingRule> get_user_privacy_setting_rule(
      telegram_api::object_ptr<telegram_api::PrivacyRule> rule);

  td_api::object_ptr<td_api::UserPrivacySettingRule> get_user_privacy_setting_rule_object() const;

  telegram_api::object_ptr<telegram_api::InputPrivacyRule> get_input_privacy_rule() const;

  bool operator==(const UserPrivacySettingRule &other) const {
    return type_ == other.type_ && user_ids_ == other.user_ids_ && chat_ids_ == other.chat_ids_;
  }

  vector<UserId> get_restricted_user_ids() const;

 private:
  enum class Type : int32 {
    AllowContacts,
    AllowCloseFriends,
    AllowAll,
    AllowUsers,
    AllowChatParticipants,
    RestrictContacts,
    RestrictAll,
    RestrictUsers,
    RestrictChatParticipants
  } type_ = Type::RestrictAll;

  vector<UserId> user_ids_;
  vector<int64> chat_ids_;

  vector<telegram_api::object_ptr<telegram_api::InputUser>> get_input_users() const;

  void set_chat_ids(const vector<int64> &dialog_ids);

  vector<int64> chat_ids_as_dialog_ids() const;

  explicit UserPrivacySettingRule(const telegram_api::PrivacyRule &rule);
};

class UserPrivacySettingRules {
 public:
  UserPrivacySettingRules() = default;

  static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
      telegram_api::object_ptr<telegram_api::account_privacyRules> rules);

  static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
      vector<telegram_api::object_ptr<telegram_api::PrivacyRule>> rules);

  static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
      td_api::object_ptr<td_api::userPrivacySettingRules> rules);

  td_api::object_ptr<td_api::userPrivacySettingRules> get_user_privacy_setting_rules_object() const;

  vector<telegram_api::object_ptr<telegram_api::InputPrivacyRule>> get_input_privacy_rules() const;

  bool operator==(const UserPrivacySettingRules &other) const {
    return rules_ == other.rules_;
  }

  vector<UserId> get_restricted_user_ids() const;

 private:
  vector<UserPrivacySettingRule> rules_;
};

}  // namespace td
