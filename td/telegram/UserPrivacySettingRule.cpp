//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/UserPrivacySettingRule.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

#include <algorithm>

namespace td {

void UserPrivacySettingRule::set_chat_ids(Td *td, const vector<int64> &dialog_ids) {
  chat_ids_.clear();
  for (auto dialog_id_int : dialog_ids) {
    DialogId dialog_id(dialog_id_int);
    if (!td->messages_manager_->have_dialog_force(dialog_id, "UserPrivacySettingRule::set_chat_ids")) {
      LOG(ERROR) << "Ignore not found " << dialog_id;
      continue;
    }

    switch (dialog_id.get_type()) {
      case DialogType::Chat:
        chat_ids_.push_back(dialog_id.get_chat_id().get());
        break;
      case DialogType::Channel: {
        auto channel_id = dialog_id.get_channel_id();
        if (!td->contacts_manager_->is_megagroup_channel(channel_id)) {
          LOG(ERROR) << "Ignore broadcast " << channel_id;
          break;
        }
        chat_ids_.push_back(channel_id.get());
        break;
      }
      default:
        LOG(ERROR) << "Ignore " << dialog_id;
    }
  }
}

UserPrivacySettingRule::UserPrivacySettingRule(Td *td, const td_api::UserPrivacySettingRule &rule) {
  switch (rule.get_id()) {
    case td_api::userPrivacySettingRuleAllowContacts::ID:
      type_ = Type::AllowContacts;
      break;
    case td_api::userPrivacySettingRuleAllowCloseFriends::ID:
      type_ = Type::AllowCloseFriends;
      break;
    case td_api::userPrivacySettingRuleAllowAll::ID:
      type_ = Type::AllowAll;
      break;
    case td_api::userPrivacySettingRuleAllowUsers::ID:
      type_ = Type::AllowUsers;
      user_ids_ = UserId::get_user_ids(static_cast<const td_api::userPrivacySettingRuleAllowUsers &>(rule).user_ids_);
      break;
    case td_api::userPrivacySettingRuleAllowChatMembers::ID:
      type_ = Type::AllowChatParticipants;
      set_chat_ids(td, static_cast<const td_api::userPrivacySettingRuleAllowChatMembers &>(rule).chat_ids_);
      break;
    case td_api::userPrivacySettingRuleRestrictContacts::ID:
      type_ = Type::RestrictContacts;
      break;
    case td_api::userPrivacySettingRuleRestrictAll::ID:
      type_ = Type::RestrictAll;
      break;
    case td_api::userPrivacySettingRuleRestrictUsers::ID:
      type_ = Type::RestrictUsers;
      user_ids_ =
          UserId::get_user_ids(static_cast<const td_api::userPrivacySettingRuleRestrictUsers &>(rule).user_ids_);
      break;
    case td_api::userPrivacySettingRuleRestrictChatMembers::ID:
      type_ = Type::RestrictChatParticipants;
      set_chat_ids(td, static_cast<const td_api::userPrivacySettingRuleRestrictChatMembers &>(rule).chat_ids_);
      break;
    default:
      UNREACHABLE();
  }
  td::remove_if(user_ids_, [](UserId user_id) { return !user_id.is_valid(); });
}

UserPrivacySettingRule::UserPrivacySettingRule(const telegram_api::PrivacyRule &rule) {
  switch (rule.get_id()) {
    case telegram_api::privacyValueAllowContacts::ID:
      type_ = Type::AllowContacts;
      break;
    case telegram_api::privacyValueAllowCloseFriends::ID:
      type_ = Type::AllowCloseFriends;
      break;
    case telegram_api::privacyValueAllowAll::ID:
      type_ = Type::AllowAll;
      break;
    case telegram_api::privacyValueAllowUsers::ID:
      type_ = Type::AllowUsers;
      user_ids_ = UserId::get_user_ids(static_cast<const telegram_api::privacyValueAllowUsers &>(rule).users_);
      break;
    case telegram_api::privacyValueAllowChatParticipants::ID:
      type_ = Type::AllowChatParticipants;
      chat_ids_ = static_cast<const telegram_api::privacyValueAllowChatParticipants &>(rule).chats_;
      break;
    case telegram_api::privacyValueDisallowContacts::ID:
      type_ = Type::RestrictContacts;
      break;
    case telegram_api::privacyValueDisallowAll::ID:
      type_ = Type::RestrictAll;
      break;
    case telegram_api::privacyValueDisallowUsers::ID:
      type_ = Type::RestrictUsers;
      user_ids_ = UserId::get_user_ids(static_cast<const telegram_api::privacyValueDisallowUsers &>(rule).users_);
      break;
    case telegram_api::privacyValueDisallowChatParticipants::ID:
      type_ = Type::RestrictChatParticipants;
      chat_ids_ = static_cast<const telegram_api::privacyValueDisallowChatParticipants &>(rule).chats_;
      break;
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::UserPrivacySettingRule> UserPrivacySettingRule::get_user_privacy_setting_rule_object(
    Td *td) const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<td_api::userPrivacySettingRuleAllowContacts>();
    case Type::AllowCloseFriends:
      return make_tl_object<td_api::userPrivacySettingRuleAllowCloseFriends>();
    case Type::AllowAll:
      return make_tl_object<td_api::userPrivacySettingRuleAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<td_api::userPrivacySettingRuleAllowUsers>(
          td->contacts_manager_->get_user_ids_object(user_ids_, "userPrivacySettingRuleAllowUsers"));
    case Type::AllowChatParticipants:
      return make_tl_object<td_api::userPrivacySettingRuleAllowChatMembers>(chat_ids_as_dialog_ids(td));
    case Type::RestrictContacts:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictContacts>();
    case Type::RestrictAll:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictAll>();
    case Type::RestrictUsers:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictUsers>(
          td->contacts_manager_->get_user_ids_object(user_ids_, "userPrivacySettingRuleRestrictUsers"));
    case Type::RestrictChatParticipants:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictChatMembers>(chat_ids_as_dialog_ids(td));
    default:
      UNREACHABLE();
  }
}

telegram_api::object_ptr<telegram_api::InputPrivacyRule> UserPrivacySettingRule::get_input_privacy_rule(Td *td) const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<telegram_api::inputPrivacyValueAllowContacts>();
    case Type::AllowCloseFriends:
      return make_tl_object<telegram_api::inputPrivacyValueAllowCloseFriends>();
    case Type::AllowAll:
      return make_tl_object<telegram_api::inputPrivacyValueAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<telegram_api::inputPrivacyValueAllowUsers>(get_input_users(td));
    case Type::AllowChatParticipants:
      return make_tl_object<telegram_api::inputPrivacyValueAllowChatParticipants>(vector<int64>{chat_ids_});
    case Type::RestrictContacts:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowContacts>();
    case Type::RestrictAll:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowAll>();
    case Type::RestrictUsers:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowUsers>(get_input_users(td));
    case Type::RestrictChatParticipants:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowChatParticipants>(vector<int64>{chat_ids_});
    default:
      UNREACHABLE();
  }
}

UserPrivacySettingRule UserPrivacySettingRule::get_user_privacy_setting_rule(
    Td *td, telegram_api::object_ptr<telegram_api::PrivacyRule> rule) {
  CHECK(rule != nullptr);
  UserPrivacySettingRule result(*rule);
  td::remove_if(result.user_ids_, [td](UserId user_id) {
    if (!td->contacts_manager_->have_user(user_id)) {
      LOG(ERROR) << "Receive unknown " << user_id << " from the server";
      return true;
    }
    return false;
  });
  td::remove_if(result.chat_ids_, [td](int64 chat_id_int) {
    ChatId chat_id(chat_id_int);
    DialogId dialog_id(chat_id);
    if (!td->contacts_manager_->have_chat(chat_id)) {
      ChannelId channel_id(chat_id_int);
      dialog_id = DialogId(channel_id);
      if (!td->contacts_manager_->have_channel(channel_id)) {
        LOG(ERROR) << "Receive unknown group " << channel_id << " from the server";
        return true;
      }
    }
    td->messages_manager_->force_create_dialog(dialog_id, "UserPrivacySettingRule");
    return false;
  });
  return result;
}

vector<telegram_api::object_ptr<telegram_api::InputUser>> UserPrivacySettingRule::get_input_users(Td *td) const {
  vector<telegram_api::object_ptr<telegram_api::InputUser>> result;
  for (auto user_id : user_ids_) {
    auto r_input_user = td->contacts_manager_->get_input_user(user_id);
    if (r_input_user.is_ok()) {
      result.push_back(r_input_user.move_as_ok());
    } else {
      LOG(ERROR) << "Have no access to " << user_id;
    }
  }
  return result;
}

vector<int64> UserPrivacySettingRule::chat_ids_as_dialog_ids(Td *td) const {
  vector<int64> result;
  for (auto chat_id_int : chat_ids_) {
    ChatId chat_id(chat_id_int);
    DialogId dialog_id(chat_id);
    if (!td->contacts_manager_->have_chat(chat_id)) {
      ChannelId channel_id(chat_id_int);
      dialog_id = DialogId(channel_id);
      CHECK(td->contacts_manager_->have_channel(channel_id));
    }
    CHECK(td->messages_manager_->have_dialog(dialog_id));
    result.push_back(td->messages_manager_->get_chat_id_object(dialog_id, "UserPrivacySettingRule"));
  }
  return result;
}

vector<UserId> UserPrivacySettingRule::get_restricted_user_ids() const {
  if (type_ == Type::RestrictUsers) {
    return user_ids_;
  }
  return {};
}

UserPrivacySettingRules UserPrivacySettingRules::get_user_privacy_setting_rules(
    Td *td, telegram_api::object_ptr<telegram_api::account_privacyRules> rules) {
  td->contacts_manager_->on_get_users(std::move(rules->users_), "on get privacy rules");
  td->contacts_manager_->on_get_chats(std::move(rules->chats_), "on get privacy rules");
  return get_user_privacy_setting_rules(td, std::move(rules->rules_));
}

UserPrivacySettingRules UserPrivacySettingRules::get_user_privacy_setting_rules(
    Td *td, vector<telegram_api::object_ptr<telegram_api::PrivacyRule>> rules) {
  UserPrivacySettingRules result;
  for (auto &rule : rules) {
    result.rules_.push_back(UserPrivacySettingRule::get_user_privacy_setting_rule(td, std::move(rule)));
  }
  if (!result.rules_.empty() && result.rules_.back().get_user_privacy_setting_rule_object(td)->get_id() ==
                                    td_api::userPrivacySettingRuleRestrictAll::ID) {
    result.rules_.pop_back();
  }
  return result;
}

Result<UserPrivacySettingRules> UserPrivacySettingRules::get_user_privacy_setting_rules(
    Td *td, td_api::object_ptr<td_api::userPrivacySettingRules> rules) {
  if (rules == nullptr) {
    return Status::Error(400, "UserPrivacySettingRules must be non-empty");
  }
  UserPrivacySettingRules result;
  for (auto &rule : rules->rules_) {
    if (rule == nullptr) {
      return Status::Error(400, "UserPrivacySettingRule must be non-empty");
    }
    result.rules_.emplace_back(td, *rule);
  }
  return result;
}

td_api::object_ptr<td_api::userPrivacySettingRules> UserPrivacySettingRules::get_user_privacy_setting_rules_object(
    Td *td) const {
  return make_tl_object<td_api::userPrivacySettingRules>(
      transform(rules_, [td](const auto &rule) { return rule.get_user_privacy_setting_rule_object(td); }));
}

vector<telegram_api::object_ptr<telegram_api::InputPrivacyRule>> UserPrivacySettingRules::get_input_privacy_rules(
    Td *td) const {
  auto result = transform(rules_, [td](const auto &rule) { return rule.get_input_privacy_rule(td); });
  if (!result.empty() && result.back()->get_id() == telegram_api::inputPrivacyValueDisallowAll::ID) {
    result.pop_back();
  }
  return result;
}

vector<UserId> UserPrivacySettingRules::get_restricted_user_ids() const {
  vector<UserId> result;
  for (auto &rule : rules_) {
    combine(result, rule.get_restricted_user_ids());
  }
  std::sort(result.begin(), result.end(), [](UserId lhs, UserId rhs) { return lhs.get() < rhs.get(); });
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

}  // namespace td
