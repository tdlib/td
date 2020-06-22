//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PrivacyManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <algorithm>
#include <iterator>

namespace td {

Result<PrivacyManager::UserPrivacySetting> PrivacyManager::UserPrivacySetting::get_user_privacy_setting(
    tl_object_ptr<td_api::UserPrivacySetting> key) {
  if (!key) {
    return Status::Error(5, "UserPrivacySetting must be non-empty");
  }
  return UserPrivacySetting(*key);
}

PrivacyManager::UserPrivacySetting::UserPrivacySetting(const telegram_api::PrivacyKey &key) {
  switch (key.get_id()) {
    case telegram_api::privacyKeyStatusTimestamp::ID:
      type_ = Type::UserStatus;
      break;
    case telegram_api::privacyKeyChatInvite::ID:
      type_ = Type::ChatInvite;
      break;
    case telegram_api::privacyKeyPhoneCall::ID:
      type_ = Type::Call;
      break;
    case telegram_api::privacyKeyPhoneP2P::ID:
      type_ = Type::PeerToPeerCall;
      break;
    case telegram_api::privacyKeyForwards::ID:
      type_ = Type::LinkInForwardedMessages;
      break;
    case telegram_api::privacyKeyProfilePhoto::ID:
      type_ = Type::UserProfilePhoto;
      break;
    case telegram_api::privacyKeyPhoneNumber::ID:
      type_ = Type::UserPhoneNumber;
      break;
    case telegram_api::privacyKeyAddedByPhone::ID:
      type_ = Type::FindByPhoneNumber;
      break;
    default:
      UNREACHABLE();
      type_ = Type::UserStatus;
  }
}

tl_object_ptr<td_api::UserPrivacySetting> PrivacyManager::UserPrivacySetting::get_user_privacy_setting_object() const {
  switch (type_) {
    case Type::UserStatus:
      return make_tl_object<td_api::userPrivacySettingShowStatus>();
    case Type::ChatInvite:
      return make_tl_object<td_api::userPrivacySettingAllowChatInvites>();
    case Type::Call:
      return make_tl_object<td_api::userPrivacySettingAllowCalls>();
    case Type::PeerToPeerCall:
      return make_tl_object<td_api::userPrivacySettingAllowPeerToPeerCalls>();
    case Type::LinkInForwardedMessages:
      return make_tl_object<td_api::userPrivacySettingShowLinkInForwardedMessages>();
    case Type::UserProfilePhoto:
      return make_tl_object<td_api::userPrivacySettingShowProfilePhoto>();
    case Type::UserPhoneNumber:
      return make_tl_object<td_api::userPrivacySettingShowPhoneNumber>();
    case Type::FindByPhoneNumber:
      return make_tl_object<td_api::userPrivacySettingAllowFindingByPhoneNumber>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}
tl_object_ptr<telegram_api::InputPrivacyKey> PrivacyManager::UserPrivacySetting::get_input_privacy_key() const {
  switch (type_) {
    case Type::UserStatus:
      return make_tl_object<telegram_api::inputPrivacyKeyStatusTimestamp>();
    case Type::ChatInvite:
      return make_tl_object<telegram_api::inputPrivacyKeyChatInvite>();
    case Type::Call:
      return make_tl_object<telegram_api::inputPrivacyKeyPhoneCall>();
    case Type::PeerToPeerCall:
      return make_tl_object<telegram_api::inputPrivacyKeyPhoneP2P>();
    case Type::LinkInForwardedMessages:
      return make_tl_object<telegram_api::inputPrivacyKeyForwards>();
    case Type::UserProfilePhoto:
      return make_tl_object<telegram_api::inputPrivacyKeyProfilePhoto>();
    case Type::UserPhoneNumber:
      return make_tl_object<telegram_api::inputPrivacyKeyPhoneNumber>();
    case Type::FindByPhoneNumber:
      return make_tl_object<telegram_api::inputPrivacyKeyAddedByPhone>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

PrivacyManager::UserPrivacySetting::UserPrivacySetting(const td_api::UserPrivacySetting &key) {
  switch (key.get_id()) {
    case td_api::userPrivacySettingShowStatus::ID:
      type_ = Type::UserStatus;
      break;
    case td_api::userPrivacySettingAllowChatInvites::ID:
      type_ = Type::ChatInvite;
      break;
    case td_api::userPrivacySettingAllowCalls::ID:
      type_ = Type::Call;
      break;
    case td_api::userPrivacySettingAllowPeerToPeerCalls::ID:
      type_ = Type::PeerToPeerCall;
      break;
    case td_api::userPrivacySettingShowLinkInForwardedMessages::ID:
      type_ = Type::LinkInForwardedMessages;
      break;
    case td_api::userPrivacySettingShowProfilePhoto::ID:
      type_ = Type::UserProfilePhoto;
      break;
    case td_api::userPrivacySettingShowPhoneNumber::ID:
      type_ = Type::UserPhoneNumber;
      break;
    case td_api::userPrivacySettingAllowFindingByPhoneNumber::ID:
      type_ = Type::FindByPhoneNumber;
      break;
    default:
      UNREACHABLE();
      type_ = Type::UserStatus;
  }
}

void PrivacyManager::UserPrivacySettingRule::set_chat_ids(const vector<int64> &dialog_ids) {
  chat_ids_.clear();
  auto td = G()->td().get_actor_unsafe();
  for (auto dialog_id_int : dialog_ids) {
    DialogId dialog_id(dialog_id_int);
    if (!td->messages_manager_->have_dialog_force(dialog_id)) {
      LOG(ERROR) << "Ignore not found " << dialog_id;
      continue;
    }

    switch (dialog_id.get_type()) {
      case DialogType::Chat:
        chat_ids_.push_back(dialog_id.get_chat_id().get());
        break;
      case DialogType::Channel: {
        auto channel_id = dialog_id.get_channel_id();
        if (td->contacts_manager_->get_channel_type(channel_id) != ChannelType::Megagroup) {
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
    case td_api::userPrivacySettingRuleAllowChatMembers::ID:
      type_ = Type::AllowChatParticipants;
      set_chat_ids(static_cast<const td_api::userPrivacySettingRuleAllowChatMembers &>(rule).chat_ids_);
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
    case td_api::userPrivacySettingRuleRestrictChatMembers::ID:
      type_ = Type::RestrictChatParticipants;
      set_chat_ids(static_cast<const td_api::userPrivacySettingRuleRestrictChatMembers &>(rule).chat_ids_);
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
      user_ids_ = static_cast<const telegram_api::privacyValueDisallowUsers &>(rule).users_;
      break;
    case telegram_api::privacyValueDisallowChatParticipants::ID:
      type_ = Type::RestrictChatParticipants;
      chat_ids_ = static_cast<const telegram_api::privacyValueDisallowChatParticipants &>(rule).chats_;
      break;
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<td_api::UserPrivacySettingRule>
PrivacyManager::UserPrivacySettingRule::get_user_privacy_setting_rule_object() const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<td_api::userPrivacySettingRuleAllowContacts>();
    case Type::AllowAll:
      return make_tl_object<td_api::userPrivacySettingRuleAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<td_api::userPrivacySettingRuleAllowUsers>(vector<int32>{user_ids_});
    case Type::AllowChatParticipants:
      return make_tl_object<td_api::userPrivacySettingRuleAllowChatMembers>(chat_ids_as_dialog_ids());
    case Type::RestrictContacts:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictContacts>();
    case Type::RestrictAll:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictAll>();
    case Type::RestrictUsers:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictUsers>(vector<int32>{user_ids_});
    case Type::RestrictChatParticipants:
      return make_tl_object<td_api::userPrivacySettingRuleRestrictChatMembers>(chat_ids_as_dialog_ids());
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<telegram_api::InputPrivacyRule> PrivacyManager::UserPrivacySettingRule::get_input_privacy_rule() const {
  switch (type_) {
    case Type::AllowContacts:
      return make_tl_object<telegram_api::inputPrivacyValueAllowContacts>();
    case Type::AllowAll:
      return make_tl_object<telegram_api::inputPrivacyValueAllowAll>();
    case Type::AllowUsers:
      return make_tl_object<telegram_api::inputPrivacyValueAllowUsers>(get_input_users());
    case Type::AllowChatParticipants:
      return make_tl_object<telegram_api::inputPrivacyValueAllowChatParticipants>(vector<int32>{chat_ids_});
    case Type::RestrictContacts:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowContacts>();
    case Type::RestrictAll:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowAll>();
    case Type::RestrictUsers:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowUsers>(get_input_users());
    case Type::RestrictChatParticipants:
      return make_tl_object<telegram_api::inputPrivacyValueDisallowChatParticipants>(vector<int32>{chat_ids_});
    default:
      UNREACHABLE();
  }
}

Result<PrivacyManager::UserPrivacySettingRule> PrivacyManager::UserPrivacySettingRule::get_user_privacy_setting_rule(
    tl_object_ptr<telegram_api::PrivacyRule> rule) {
  CHECK(rule != nullptr);
  UserPrivacySettingRule result(*rule);
  auto td = G()->td().get_actor_unsafe();
  for (auto user_id : result.user_ids_) {
    if (!td->contacts_manager_->have_user(UserId(user_id))) {
      return Status::Error(500, "Got inaccessible user from the server");
    }
  }
  for (auto chat_id_int : result.chat_ids_) {
    ChatId chat_id(chat_id_int);
    DialogId dialog_id(chat_id);
    if (!td->contacts_manager_->have_chat(chat_id)) {
      ChannelId channel_id(chat_id_int);
      dialog_id = DialogId(channel_id);
      if (!td->contacts_manager_->have_channel(channel_id)) {
        return Status::Error(500, "Got inaccessible chat from the server");
      }
    }
    td->messages_manager_->force_create_dialog(dialog_id, "UserPrivacySettingRule");
  }
  return result;
}

vector<tl_object_ptr<telegram_api::InputUser>> PrivacyManager::UserPrivacySettingRule::get_input_users() const {
  vector<tl_object_ptr<telegram_api::InputUser>> result;
  for (auto user_id : user_ids_) {
    auto input_user = G()->td().get_actor_unsafe()->contacts_manager_->get_input_user(UserId(user_id));
    if (input_user != nullptr) {
      result.push_back(std::move(input_user));
    } else {
      LOG(ERROR) << "Have no access to " << user_id;
    }
  }
  return result;
}

vector<int64> PrivacyManager::UserPrivacySettingRule::chat_ids_as_dialog_ids() const {
  vector<int64> result;
  auto td = G()->td().get_actor_unsafe();
  for (auto chat_id_int : chat_ids_) {
    ChatId chat_id(chat_id_int);
    DialogId dialog_id(chat_id);
    if (!td->contacts_manager_->have_chat(chat_id)) {
      ChannelId channel_id(chat_id_int);
      dialog_id = DialogId(channel_id);
      CHECK(td->contacts_manager_->have_channel(channel_id));
    }
    CHECK(td->messages_manager_->have_dialog(dialog_id));
    result.push_back(dialog_id.get());
  }
  return result;
}

vector<int32> PrivacyManager::UserPrivacySettingRule::get_restricted_user_ids() const {
  if (type_ == Type::RestrictUsers) {
    return user_ids_;
  }
  return {};
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::get_user_privacy_setting_rules(
    tl_object_ptr<telegram_api::account_privacyRules> rules) {
  G()->td().get_actor_unsafe()->contacts_manager_->on_get_users(std::move(rules->users_), "on get privacy rules");
  G()->td().get_actor_unsafe()->contacts_manager_->on_get_chats(std::move(rules->chats_), "on get privacy rules");
  return get_user_privacy_setting_rules(std::move(rules->rules_));
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::get_user_privacy_setting_rules(
    vector<tl_object_ptr<telegram_api::PrivacyRule>> rules) {
  UserPrivacySettingRules result;
  for (auto &rule : rules) {
    TRY_RESULT(new_rule, UserPrivacySettingRule::get_user_privacy_setting_rule(std::move(rule)));
    result.rules_.push_back(new_rule);
  }
  if (!result.rules_.empty() && result.rules_.back().get_user_privacy_setting_rule_object()->get_id() ==
                                    td_api::userPrivacySettingRuleRestrictAll::ID) {
    result.rules_.pop_back();
  }
  return result;
}

Result<PrivacyManager::UserPrivacySettingRules> PrivacyManager::UserPrivacySettingRules::get_user_privacy_setting_rules(
    tl_object_ptr<td_api::userPrivacySettingRules> rules) {
  if (!rules) {
    return Status::Error(5, "UserPrivacySettingRules must be non-empty");
  }
  UserPrivacySettingRules result;
  for (auto &rule : rules->rules_) {
    if (!rule) {
      return Status::Error(5, "UserPrivacySettingRule must be non-empty");
    }
    result.rules_.emplace_back(*rule);
  }
  return result;
}

tl_object_ptr<td_api::userPrivacySettingRules>
PrivacyManager::UserPrivacySettingRules::get_user_privacy_setting_rules_object() const {
  return make_tl_object<td_api::userPrivacySettingRules>(
      transform(rules_, [](const auto &rule) { return rule.get_user_privacy_setting_rule_object(); }));
}

vector<tl_object_ptr<telegram_api::InputPrivacyRule>> PrivacyManager::UserPrivacySettingRules::get_input_privacy_rules()
    const {
  auto result = transform(rules_, [](const auto &rule) { return rule.get_input_privacy_rule(); });
  if (!result.empty() && result.back()->get_id() == telegram_api::inputPrivacyValueDisallowAll::ID) {
    result.pop_back();
  }
  return result;
}

vector<int32> PrivacyManager::UserPrivacySettingRules::get_restricted_user_ids() const {
  vector<int32> result;
  for (auto &rule : rules_) {
    combine(result, rule.get_restricted_user_ids());
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

void PrivacyManager::get_privacy(tl_object_ptr<td_api::UserPrivacySetting> key,
                                 Promise<tl_object_ptr<td_api::userPrivacySettingRules>> promise) {
  auto r_user_privacy_setting = UserPrivacySetting::get_user_privacy_setting(std::move(key));
  if (r_user_privacy_setting.is_error()) {
    return promise.set_error(r_user_privacy_setting.move_as_error());
  }
  auto user_privacy_setting = r_user_privacy_setting.move_as_ok();
  auto &info = get_info(user_privacy_setting);
  if (info.is_synchronized) {
    return promise.set_value(info.rules.get_user_privacy_setting_rules_object());
  }
  info.get_promises.push_back(std::move(promise));
  if (info.get_promises.size() > 1u) {
    // query has already been sent, just wait for the result
    return;
  }
  auto net_query =
      G()->net_query_creator().create(telegram_api::account_getPrivacy(user_privacy_setting.get_input_privacy_key()));

  send_with_promise(std::move(net_query),
                    PromiseCreator::lambda([this, user_privacy_setting](Result<NetQueryPtr> x_net_query) {
                      on_get_result(user_privacy_setting, [&]() -> Result<UserPrivacySettingRules> {
                        TRY_RESULT(net_query, std::move(x_net_query));
                        TRY_RESULT(rules, fetch_result<telegram_api::account_getPrivacy>(std::move(net_query)));
                        LOG(INFO) << "Receive " << to_string(rules);
                        return UserPrivacySettingRules::get_user_privacy_setting_rules(std::move(rules));
                      }());
                    }));
}

void PrivacyManager::set_privacy(tl_object_ptr<td_api::UserPrivacySetting> key,
                                 tl_object_ptr<td_api::userPrivacySettingRules> rules, Promise<Unit> promise) {
  auto r_user_privacy_setting = UserPrivacySetting::get_user_privacy_setting(std::move(key));
  if (r_user_privacy_setting.is_error()) {
    return promise.set_error(r_user_privacy_setting.move_as_error());
  }
  auto user_privacy_setting = r_user_privacy_setting.move_as_ok();

  auto r_privacy_rules = UserPrivacySettingRules::get_user_privacy_setting_rules(std::move(rules));
  if (r_privacy_rules.is_error()) {
    return promise.set_error(r_privacy_rules.move_as_error());
  }
  auto privacy_rules = r_privacy_rules.move_as_ok();

  auto &info = get_info(user_privacy_setting);
  if (info.has_set_query) {
    // TODO cancel previous query
    return promise.set_error(Status::Error(5, "Another set_privacy query is active"));
  }
  auto net_query = G()->net_query_creator().create(telegram_api::account_setPrivacy(
      user_privacy_setting.get_input_privacy_key(), privacy_rules.get_input_privacy_rules()));

  info.has_set_query = true;
  send_with_promise(
      std::move(net_query), PromiseCreator::lambda([this, user_privacy_setting, promise = std::move(promise)](
                                                       Result<NetQueryPtr> x_net_query) mutable {
        promise.set_result([&]() -> Result<Unit> {
          get_info(user_privacy_setting).has_set_query = false;
          TRY_RESULT(net_query, std::move(x_net_query));
          TRY_RESULT(rules, fetch_result<telegram_api::account_setPrivacy>(std::move(net_query)));
          LOG(INFO) << "Receive " << to_string(rules);
          TRY_RESULT(privacy_rules, UserPrivacySettingRules::get_user_privacy_setting_rules(std::move(rules)));
          do_update_privacy(user_privacy_setting, std::move(privacy_rules), true);
          return Unit();
        }());
      }));
}

void PrivacyManager::update_privacy(tl_object_ptr<telegram_api::updatePrivacy> update) {
  CHECK(update != nullptr);
  CHECK(update->key_ != nullptr);
  UserPrivacySetting user_privacy_setting(*update->key_);
  auto r_privacy_rules = UserPrivacySettingRules::get_user_privacy_setting_rules(std::move(update->rules_));
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
      promise.set_value(privacy_rules.ok().get_user_privacy_setting_rules_object());
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
    if ((from_update || was_synchronized) && !G()->close_flag()) {
      switch (user_privacy_setting.type()) {
        case UserPrivacySetting::Type::UserStatus: {
          send_closure_later(G()->contacts_manager(), &ContactsManager::on_update_online_status_privacy);

          auto old_restricted = info.rules.get_restricted_user_ids();
          auto new_restricted = privacy_rules.get_restricted_user_ids();
          if (old_restricted != new_restricted) {
            // if a user was unrestricted, it is not received from the server anymore
            // we need to reget their online status manually
            std::vector<int32> unrestricted;
            std::set_difference(old_restricted.begin(), old_restricted.end(), new_restricted.begin(),
                                new_restricted.end(), std::back_inserter(unrestricted));
            for (auto &user_id : unrestricted) {
              send_closure_later(G()->contacts_manager(), &ContactsManager::reload_user, UserId(user_id),
                                 Promise<Unit>());
            }
          }
          break;
        }
        case UserPrivacySetting::Type::UserPhoneNumber:
          send_closure_later(G()->contacts_manager(), &ContactsManager::on_update_phone_number_privacy);
          break;
        default:
          break;
      }
    }

    info.rules = std::move(privacy_rules);
    send_closure(
        G()->td(), &Td::send_update,
        make_tl_object<td_api::updateUserPrivacySettingRules>(user_privacy_setting.get_user_privacy_setting_object(),
                                                              info.rules.get_user_privacy_setting_rules_object()));
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
