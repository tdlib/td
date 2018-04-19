//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/SecureValue.h"
#include "td/telegram/UserId.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/Container.h"
#include "td/utils/optional.h"
#include "td/utils/Status.h"

#include <map>
#include <memory>

namespace td {

class Td;

using TdApiSecureValue = td_api::object_ptr<td_api::PassportData>;
using TdApiAllSecureValues = td_api::object_ptr<td_api::allPassportData>;
using TdApiAuthorizationForm = td_api::object_ptr<td_api::passportAuthorizationForm>;

class SecureManager : public NetQueryCallback {
 public:
  explicit SecureManager(ActorShared<> parent);

  void get_secure_value(std::string password, SecureValueType type, Promise<TdApiSecureValue> promise);
  void get_all_secure_values(std::string password, Promise<TdApiAllSecureValues> promise);
  void set_secure_value(string password, SecureValue secure_value, Promise<TdApiSecureValue> promise);
  void delete_secure_value(SecureValueType type, Promise<Unit> promise);
  void set_secure_value_errors(Td *td, tl_object_ptr<telegram_api::InputUser> input_user,
                               vector<tl_object_ptr<td_api::inputPassportDataError>> errors, Promise<Unit> promise);

  void get_passport_authorization_form(string password, UserId bot_user_id, string scope, string public_key,
                                       string payload, Promise<TdApiAuthorizationForm> promise);
  void send_passport_authorization_form(string password, int32 authorization_form_id,
                                        std::vector<SecureValueType> types, Promise<> promise);

 private:
  ActorShared<> parent_;
  int32 refcnt_{1};
  std::map<SecureValueType, ActorOwn<>> set_secure_value_queries_;

  struct AuthorizationForm {
    UserId bot_user_id;
    string scope;
    string public_key;
    string payload;
    bool is_selfie_required;
    bool is_received;
  };

  std::map<int32, AuthorizationForm> authorization_forms_;
  int32 authorization_form_id_{0};

  void hangup() override;
  void hangup_shared() override;
  void dec_refcnt();
  void do_get_secure_value(std::string password, SecureValueType type, Promise<SecureValueWithCredentials> promise);
  void on_delete_secure_value(SecureValueType type, Promise<Unit> promise, Result<Unit> result);
  void on_get_passport_authorization_form(int32 authorization_form_id, Promise<TdApiAuthorizationForm> promise,
                                          Result<TdApiAuthorizationForm> r_authorization_form);
  void do_send_passport_authorization_form(int32 authorization_form_id, vector<SecureValueCredentials> credentials,
                                           Promise<> promise);

  void on_result(NetQueryPtr query) override;
  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

}  // namespace td
