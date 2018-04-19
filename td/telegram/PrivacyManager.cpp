//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PrivacyManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

Result<PrivacyManager::UserPrivacySetting> PrivacyManager::UserPrivacySetting::from_td_api(
    tl_object_ptr<td_api::UserPrivacySetting> key) {
  if (!key) {
    return Status::Error(5, "UserPrivacySetting should not be empty");
  }
  return UserPrivacySetting(*key);
}
PrivacyManager::UserPrivacySetting::UserPrivacySetting(const telegram_api::PrivacyKey &key) {
  switch (key.get_id()) {
    case telegram_api::privacyKeyStatusTimestamp::ID:
      type_ = Type::UserState;
      break;
    case telegram_api::privacyKeyChatInvite::ID:
      type_ = Type::ChatInvite;
      break;
    case telegram_api::privacyKeyPhoneCall::ID:
      type_ = Type::Call;
      break;
    default:
      UNREACHABLE();
      type_ = Type::UserState;
  }
}
tl_object_ptr<td_api::UserPrivacySetting> PrivacyManager::UserPrivacySetting::as_td_api() const {
  switch (type_) {
    case Type::UserState:
      return make_tl_object<td_api::userPrivacySettingShowStatus>();
    case Type::ChatInvite:
      return make_tl_object<td_api::userPrivacySettingAllowChatInvites>();
    case Type::Call:
      return make_tl_object<td_api::userPrivacySettingAllowCalls>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}
tl_object_ptr<telegram_api::InputPrivacyKey> PrivacyManager::UserPrivacySetting::as_telegram_api() const {
  switch (type_) {
    case Type::UserState:
      return make_tl_object<telegram_api::inputPrivacyKeyStatusTimestamp>();
    case Type::ChatInvite:
      return make_tl_object<telegram_api::inputPrivacyKeyChatInvite>();
    case Type::Call:
      return make_tl_object<telegram_api::inputPrivacyKeyPhoneCall>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

PrivacyManager::UserPrivacySetting::UserPrivacySetting(const td_api::UserPrivacySetting &key) {
  switch (key.get_id()) {
    case td_api::userPrivacySettingShowStatus::ID:
      type_ = Type::UserState;
      break;
    case td_api::userPrivacySettingAllowChatInvites::ID:
      type_ = Type::ChatInvite;
      break;
    case td_api::userPrivacySettingAllowCalls::ID:
      type_ = Type::Call;
      break;
    default:
      UNREACHABLE();
      type_ = Type::UserState;
  }
}

PrivacyManager::UserPrivacySettingRule::UserPrivacySettingRule(const td_api::UserPrivacySettingRule &rule) {
  switch (rule.get_id()) {
    case td_api::userPrivacySettingRuleAllowContacts::ID:
      type_ = Type::AllowContacts;
      break;
    case td_api::userPrivacySettingRuleAllowAll::ID:
      type_ = Type::AllowAll;
      break;
    case td_api::userPrivacySettingRuleAllowUsers::ID:
      type_ = Type::AllowUsers;
      user_ids_ = static_cast<const td_api::userPrivacySettingRuleAllowUsers &>(rule).user_ids_;
      break;
    case td_api::userPrivacySettingRuleRestrictContacts::ID:
      type_ = Type::RestrictContacts;
      break;
    case td_api::userPrivacySettingRuleRestrictAll::ID:
      type_ = Type::RestrictAll;
      break;
    case td_api::userPrivacySettingRuleRestrictUsers::ID:
      type_ = Type::RestrictUsers;
      user_ids_ = static_cast<const td_api::userPrivacySettingRuleRestrictUsers &>(rule).user_ids_;
      break;
    default:
      UNREACHABLE();
  }
}

PrivacyManager::UserPrivacySettingRule::UserPrivacySettingRule(const telegram_api::PrivacyRule &rule) {
  switch (rule.get_id()) {
    case telegram_api::privacyValueAllowContacts::ID:
      type_ = Type::AllowContacts;
      break;
    case telegram_api::privacyValueAllowAll::ID:
      type_ = Type::AllowAll;
      break;
    case telegram_api::privacyValueAllowUsers::ID:
      type_ = Type::AllowUsers;
      user_ids_ = static_cast<const telegram_api::privacyValueAllowUsers &>(rule).users_;
      break;
    case telegram_api::privacyValueDisallowContacts::ID:
      type_ = Type::RestrictContacts;
      break;
    case telegram_api::privacyValueDisallowAll::ID:
      type_ = Type::RestrictAll;
      break;
    case telegram_api::privacyValueDisallowUsers::ID:
      type_ = Type::RestrictUsers;
      user_ids_ = static_cast<const telegram_api::privacyValueDisallowUsers &>(rule).users_;
      break;
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<td_api::UserPrivacySettingRule> PrivacyManager::UserPrivacySettingRule::as_td_api() const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<td_api::userPrivacySettingRuleAllowContacts>();
    case Type::AllowAll:
      return make_tl_object<td_api::userPrivacySettingRuleAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<td_api::userPrivacySettingRuleAllowUsers>(user_ids_as_td_api());
    case Type::RestrictContacts:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictContacts>();
    case Type::RestrictAll:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictAll>();
    case Type::RestrictUsers:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictUsers>(user_ids_as_td_api());
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<telegram_api::InputPrivacyRule> PrivacyManager::UserPrivacySettingRule::as_telegram_api() const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<telegram_api::inputPrivacyValueAllowContacts>();
    case Type::AllowAll:
      return make_tl_object<telegram_api::inputPrivacyValueAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<telegram_api::inputPrivacyValueAllowUsers>(user_ids_as_telegram_api());
    case Type::RestrictContacts:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowContacts>();
    case Type::RestrictAll:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowAll>();
    case Type::RestrictUsers:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowUsers>(user_ids_as_telegram_api());
    default:
      UNREACHABLE();
  }
}

Result<PrivacyManager::UserPrivacySettingRule> PrivacyManager::UserPrivacySettingRule::from_telegram_api(
    tl_object_ptr<telegram_api::PrivacyRule> rule) {
  CHECK(rule != nullptr);
  UserPrivacySettingRule res(*rule);
  for (auto user_id : res.user_ids_) {
    if (!G()->td().get_actor_unsafe()->contacts_manager_->have_user(UserId(user_id))) {
      return Status::Error(500, "Got unaccessible user from the server");
    }
  }
  return res;
}

vector<tl_object_ptr<telegram_api::InputUser>> PrivacyManager::UserPrivacySettingRule::user_ids_as_telegram_api()
    const {
  vector<tl_object_ptr<telegram_api::InputUser>> res;
  for (auto user_id : user_ids_) {
    auto input_user = G()->td().get_actor_unsafe()->contacts_manager_->get_input_user(UserId(user_id));
    if (input_user != nullptr) {
      res.push_back(std::move(input_user));
    } else {
      LOG(ERROR) << "Have no access to " << user_id;
    }
  }
  return res;
}

vector<int32> PrivacyManager::UserPrivacySettingRule::user_ids_as_td_api() const {
  return user_ids_;
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::from_telegram_api(
    tl_object_ptr<telegram_api::account_privacyRules> rules) {
  G()->td().get_actor_unsafe()->contacts_manager_->on_get_users(std::move(rules->users_));
  return from_telegram_api(std::move(rules->rules_));
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::from_telegram_api(
    vector<tl_object_ptr<telegram_api::PrivacyRule>> rules) {
  UserPrivacySettingRules res;
  for (auto &rule : rules) {
    TRY_RESULT(new_rule, UserPrivacySettingRule::from_telegram_api(std::move(rule)));
    res.rules_.push_back(new_rule);
  }
  return res;
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::from_td_api(
    tl_object_ptr<td_api::userPrivacySettingRules> rules) {
  if (!rules) {
    return Status::Error(5, "UserPrivacySettingRules should not be empty");
  }
  UserPrivacySettingRules res;
  for (auto &rule : rules->rules_) {
    if (!rule) {
      return Status::Error(5, "UserPrivacySettingRule should not be empty");
    }
    res.rules_.emplace_back(*rule);
  }
  return res;
}

tl_object_ptr<td_api::userPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::as_td_api() const {
  return make_tl_object<td_api::userPrivacySettingRules>(
      transform(rules_, [](const auto &rule) { return rule.as_td_api(); }));
}

vector<tl_object_ptr<telegram_api::InputPrivacyRule>> PrivacyManager::UserPrivacySettingRules::as_telegram_api() const {
  return transform(rules_, [](const auto &rule) { return rule.as_telegram_api(); });
}

void PrivacyManager::get_privacy(tl_object_ptr<td_api::UserPrivacySetting> key,
                                 Promise<tl_object_ptr<td_api::userPrivacySettingRules>> promise) {
  auto r_user_privacy_setting = UserPrivacySetting::from_td_api(std::move(key));
  if (r_user_privacy_setting.is_error()) {
    return promise.set_error(r_user_privacy_setting.move_as_error());
  }
  auto user_privacy_setting = r_user_privacy_setting.move_as_ok();
  auto &info = get_info(user_privacy_setting);
  if (info.is_synchronized) {
    return promise.set_value(info.rules.as_td_api());
  }
  info.get_promises.push_back(std::move(promise));
  if (info.get_promises.size() > 1u) {
    // query has already been sent, just wait for the result
    return;
  }
  auto net_query = G()->net_query_creator().create(
      create_storer(telegram_api::account_getPrivacy(user_privacy_setting.as_telegram_api())));

  send_with_promise(std::move(net_query),
                    PromiseCreator::lambda([this, user_privacy_setting](Result<NetQueryPtr> x_net_query) mutable {
                      on_get_result(user_privacy_setting, [&]() -> Result<UserPrivacySettingRules> {
                        TRY_RESULT(net_query, std::move(x_net_query));
                        TRY_RESULT(rules, fetch_result<telegram_api::account_getPrivacy>(std::move(net_query)));
                        return UserPrivacySettingRules::from_telegram_api(std::move(rules));
                      }());
                    }));
}

void PrivacyManager::set_privacy(tl_object_ptr<td_api::UserPrivacySetting> key,
                                 tl_object_ptr<td_api::userPrivacySettingRules> rules,
                                 Promise<tl_object_ptr<td_api::ok>> promise) {
  auto r_user_privacy_setting = UserPrivacySetting::from_td_api(std::move(key));
  if (r_user_privacy_setting.is_error()) {
    return promise.set_error(r_user_privacy_setting.move_as_error());
  }
  auto user_privacy_setting = r_user_privacy_setting.move_as_ok();

  auto r_privacy_rules = UserPrivacySettingRules::from_td_api(std::move(rules));
  if (r_privacy_rules.is_error()) {
    return promise.set_error(r_privacy_rules.move_as_error());
  }
  auto privacy_rules = r_privacy_rules.move_as_ok();

  auto &info = get_info(user_privacy_setting);
  if (info.has_set_query) {
    // TODO cancel previous query
    return promise.set_error(Status::Error(5, "Another set_privacy query is active"));
  }
  auto net_query = G()->net_query_creator().create(create_storer(
      telegram_api::account_setPrivacy(user_privacy_setting.as_telegram_api(), privacy_rules.as_telegram_api())));

  info.has_set_query = true;
  send_with_promise(std::move(net_query),
                    PromiseCreator::lambda([this, user_privacy_setting,
                                            promise = std::move(promise)](Result<NetQueryPtr> x_net_query) mutable {
                      promise.set_result([&]() -> Result<tl_object_ptr<td_api::ok>> {
                        TRY_RESULT(net_query, std::move(x_net_query));
                        TRY_RESULT(rules, fetch_result<telegram_api::account_setPrivacy>(std::move(net_query)));
                        TRY_RESULT(privacy_rules, UserPrivacySettingRules::from_telegram_api(std::move(rules)));
                        get_info(user_privacy_setting).has_set_query = false;
                        do_update_privacy(user_privacy_setting, std::move(privacy_rules), true);
                        return make_tl_object<td_api::ok>();
                      }());
                    }));
}

void PrivacyManager::update_privacy(tl_object_ptr<telegram_api::updatePrivacy> update) {
  CHECK(update != nullptr);
  CHECK(update->key_ != nullptr);
  UserPrivacySetting user_privacy_setting(*update->key_);
  auto r_privacy_rules = UserPrivacySettingRules::from_telegram_api(std::move(update->rules_));
  if (r_privacy_rules.is_error()) {
    LOG(INFO) << "Skip updatePrivacy: " << r_privacy_rules.error().message();
    auto &info = get_info(user_privacy_setting);
    info.is_synchronized = false;
  } else {
    do_update_privacy(user_privacy_setting, r_privacy_rules.move_as_ok(), true);
  }
}

void PrivacyManager::on_get_result(UserPrivacySetting user_privacy_setting,
                                   Result<UserPrivacySettingRules> privacy_rules) {
  auto &info = get_info(user_privacy_setting);
  auto promises = std::move(info.get_promises);
  reset_to_empty(info.get_promises);
  for (auto &promise : promises) {
    if (privacy_rules.is_error()) {
      promise.set_error(privacy_rules.error().clone());
    } else {
      promise.set_value(privacy_rules.ok().as_td_api());
    }
  }
  if (privacy_rules.is_ok()) {
    do_update_privacy(user_privacy_setting, privacy_rules.move_as_ok(), false);
  }
}

void PrivacyManager::do_update_privacy(UserPrivacySetting user_privacy_setting, UserPrivacySettingRules &&privacy_rules,
                                       bool from_update) {
  auto &info = get_info(user_privacy_setting);
  bool was_synchronized = info.is_synchronized;
  info.is_synchronized = true;

  if (!(info.rules == privacy_rules)) {
    info.rules = std::move(privacy_rules);
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateUserPrivacySettingRules>(user_privacy_setting.as_td_api(),
                                                                       info.rules.as_td_api()));

    if ((from_update || was_synchronized) && user_privacy_setting.type() == UserPrivacySetting::Type::UserState) {
      send_closure(G()->contacts_manager(), &ContactsManager::on_update_online_status_privacy);
    }
  }
}

void PrivacyManager::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
}

void PrivacyManager::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

void PrivacyManager::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
  stop();
}

}  // namespace td
