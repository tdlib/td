//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PrivacyManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

#include <algorithm>
#include <iterator>

namespace td {

Result<PrivacyManager::UserPrivacySetting> PrivacyManager::UserPrivacySetting::get_user_privacy_setting(
    tl_object_ptr<td_api::UserPrivacySetting> key) {
  if (key == nullptr) {
    return Status::Error(400, "UserPrivacySetting must be non-empty");
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
    case telegram_api::privacyKeyVoiceMessages::ID:
      type_ = Type::VoiceMessages;
      break;
    case telegram_api::privacyKeyAbout::ID:
      type_ = Type::UserBio;
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
    case Type::VoiceMessages:
      return make_tl_object<td_api::userPrivacySettingAllowPrivateVoiceAndVideoNoteMessages>();
    case Type::UserBio:
      return make_tl_object<td_api::userPrivacySettingShowBio>();
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
    case Type::VoiceMessages:
      return make_tl_object<telegram_api::inputPrivacyKeyVoiceMessages>();
    case Type::UserBio:
      return make_tl_object<telegram_api::inputPrivacyKeyAbout>();
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
    case td_api::userPrivacySettingAllowPrivateVoiceAndVideoNoteMessages::ID:
      type_ = Type::VoiceMessages;
      break;
    case td_api::userPrivacySettingShowBio::ID:
      type_ = Type::UserBio;
      break;
    default:
      UNREACHABLE();
      type_ = Type::UserStatus;
  }
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
  TRY_RESULT_PROMISE(promise, user_privacy_setting, UserPrivacySetting::get_user_privacy_setting(std::move(key)));
  TRY_RESULT_PROMISE(promise, privacy_rules, UserPrivacySettingRules::get_user_privacy_setting_rules(std::move(rules)));

  auto &info = get_info(user_privacy_setting);
  if (info.has_set_query) {
    // TODO cancel previous query
    return promise.set_error(Status::Error(400, "Another set_privacy query is active"));
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
                                   Result<UserPrivacySettingRules> r_privacy_rules) {
  auto &info = get_info(user_privacy_setting);
  auto promises = std::move(info.get_promises);
  reset_to_empty(info.get_promises);
  for (auto &promise : promises) {
    if (r_privacy_rules.is_error()) {
      promise.set_error(r_privacy_rules.error().clone());
    } else {
      promise.set_value(r_privacy_rules.ok().get_user_privacy_setting_rules_object());
    }
  }
  if (r_privacy_rules.is_ok()) {
    do_update_privacy(user_privacy_setting, r_privacy_rules.move_as_ok(), false);
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
            std::vector<UserId> unrestricted;
            std::set_difference(old_restricted.begin(), old_restricted.end(), new_restricted.begin(),
                                new_restricted.end(), std::back_inserter(unrestricted),
                                [](UserId lhs, UserId rhs) { return lhs.get() < rhs.get(); });
            for (auto &user_id : unrestricted) {
              send_closure_later(G()->contacts_manager(), &ContactsManager::reload_user, user_id, Promise<Unit>());
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
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Global::request_aborted_error()); });
  stop();
}

}  // namespace td
