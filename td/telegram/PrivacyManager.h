//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/net/NetQuery.h"

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
                   Promise<tl_object_ptr<td_api::ok>> promise);

  void update_privacy(tl_object_ptr<telegram_api::updatePrivacy> update);

 private:
  class UserPrivacySetting {
   public:
    enum class Type : int32 { UserState, ChatInvite, Call, Size };

    static Result<UserPrivacySetting> from_td_api(tl_object_ptr<td_api::UserPrivacySetting> key);
    explicit UserPrivacySetting(const telegram_api::PrivacyKey &key);
    tl_object_ptr<td_api::UserPrivacySetting> as_td_api() const;
    tl_object_ptr<telegram_api::InputPrivacyKey> as_telegram_api() const;

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
    static Result<UserPrivacySettingRule> from_telegram_api(tl_object_ptr<telegram_api::PrivacyRule> rule);
    explicit UserPrivacySettingRule(const td_api::UserPrivacySettingRule &rule);
    tl_object_ptr<td_api::UserPrivacySettingRule> as_td_api() const;
    tl_object_ptr<telegram_api::InputPrivacyRule> as_telegram_api() const;

    bool operator==(const UserPrivacySettingRule &other) const {
      return type_ == other.type_ && user_ids_ == other.user_ids_;
    }

   private:
    enum class Type : int32 {
      AllowContacts,
      AllowAll,
      AllowUsers,
      RestrictContacts,
      RestrictAll,
      RestrictUsers
    } type_ = Type::RestrictAll;

    vector<int32> user_ids_;

    vector<int32> user_ids_as_td_api() const;

    vector<tl_object_ptr<telegram_api::InputUser>> user_ids_as_telegram_api() const;

    explicit UserPrivacySettingRule(const telegram_api::PrivacyRule &rule);
  };

  class UserPrivacySettingRules {
   public:
    UserPrivacySettingRules() = default;
    static Result<UserPrivacySettingRules> from_telegram_api(tl_object_ptr<telegram_api::account_privacyRules> rules);
    static Result<UserPrivacySettingRules> from_telegram_api(vector<tl_object_ptr<telegram_api::PrivacyRule>> rules);
    static Result<UserPrivacySettingRules> from_td_api(tl_object_ptr<td_api::userPrivacySettingRules> rules);
    tl_object_ptr<td_api::userPrivacySettingRules> as_td_api() const;
    vector<tl_object_ptr<telegram_api::InputPrivacyRule>> as_telegram_api() const;

    bool operator==(const UserPrivacySettingRules &other) const {
      return rules_ == other.rules_;
    }

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
