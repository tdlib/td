//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/Status.h"

#include <array>

namespace td {

class PrivacyManager : public NetQueryCallback {
 public:
  explicit PrivacyManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void get_privacy(tl_object_ptr<td_api::UserPrivacySetting> key,
                   Promise<tl_object_ptr<td_api::userPrivacySettingRules>> promise);

  void set_privacy(tl_object_ptr<td_api::UserPrivacySetting> key, tl_object_ptr<td_api::userPrivacySettingRules> rules,
                   Promise<Unit> promise);

  void update_privacy(tl_object_ptr<telegram_api::updatePrivacy> update);

 private:
  class UserPrivacySetting {
   public:
    enum class Type : int32 {
      UserStatus,
      ChatInvite,
      Call,
      PeerToPeerCall,
      LinkInForwardedMessages,
      UserProfilePhoto,
      UserPhoneNumber,
      FindByPhoneNumber,
      Size
    };

    explicit UserPrivacySetting(const telegram_api::PrivacyKey &key);

    static Result<UserPrivacySetting> get_user_privacy_setting(tl_object_ptr<td_api::UserPrivacySetting> key);

    tl_object_ptr<td_api::UserPrivacySetting> get_user_privacy_setting_object() const;

    tl_object_ptr<telegram_api::InputPrivacyKey> get_input_privacy_key() const;

    Type type() const {
      return type_;
    }

   private:
    Type type_;

    explicit UserPrivacySetting(const td_api::UserPrivacySetting &key);
  };

  class UserPrivacySettingRule {
   public:
    UserPrivacySettingRule() = default;

    explicit UserPrivacySettingRule(const td_api::UserPrivacySettingRule &rule);

    static Result<UserPrivacySettingRule> get_user_privacy_setting_rule(tl_object_ptr<telegram_api::PrivacyRule> rule);

    tl_object_ptr<td_api::UserPrivacySettingRule> get_user_privacy_setting_rule_object() const;

    tl_object_ptr<telegram_api::InputPrivacyRule> get_input_privacy_rule() const;

    bool operator==(const UserPrivacySettingRule &other) const {
      return type_ == other.type_ && user_ids_ == other.user_ids_ && chat_ids_ == other.chat_ids_;
    }

    vector<int32> get_restricted_user_ids() const;

   private:
    enum class Type : int32 {
      AllowContacts,
      AllowAll,
      AllowUsers,
      AllowChatParticipants,
      RestrictContacts,
      RestrictAll,
      RestrictUsers,
      RestrictChatParticipants
    } type_ = Type::RestrictAll;

    vector<int32> user_ids_;
    vector<int32> chat_ids_;

    vector<tl_object_ptr<telegram_api::InputUser>> get_input_users() const;

    void set_chat_ids(const vector<int64> &dialog_ids);

    vector<int64> chat_ids_as_dialog_ids() const;

    explicit UserPrivacySettingRule(const telegram_api::PrivacyRule &rule);
  };

  class UserPrivacySettingRules {
   public:
    UserPrivacySettingRules() = default;

    static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
        tl_object_ptr<telegram_api::account_privacyRules> rules);

    static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
        vector<tl_object_ptr<telegram_api::PrivacyRule>> rules);

    static Result<UserPrivacySettingRules> get_user_privacy_setting_rules(
        tl_object_ptr<td_api::userPrivacySettingRules> rules);

    tl_object_ptr<td_api::userPrivacySettingRules> get_user_privacy_setting_rules_object() const;

    vector<tl_object_ptr<telegram_api::InputPrivacyRule>> get_input_privacy_rules() const;

    bool operator==(const UserPrivacySettingRules &other) const {
      return rules_ == other.rules_;
    }

    vector<int32> get_restricted_user_ids() const;

   private:
    vector<UserPrivacySettingRule> rules_;
  };

  ActorShared<> parent_;

  struct PrivacyInfo {
    UserPrivacySettingRules rules;
    vector<Promise<tl_object_ptr<td_api::userPrivacySettingRules>>> get_promises;
    bool has_set_query = false;
    bool is_synchronized = false;
  };
  std::array<PrivacyInfo, static_cast<size_t>(UserPrivacySetting::Type::Size)> info_;

  PrivacyInfo &get_info(UserPrivacySetting key) {
    return info_[static_cast<size_t>(key.type())];
  }

  void on_get_result(UserPrivacySetting user_privacy_setting, Result<UserPrivacySettingRules> privacy_rules);
  void do_update_privacy(UserPrivacySetting user_privacy_setting, UserPrivacySettingRules &&privacy_rules,
                         bool from_update);

  void on_result(NetQueryPtr query) override;
  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);

  void hangup() override;
};

}  // namespace td
