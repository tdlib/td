//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/utils/Container.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

#include "td/telegram/td_api.h"

namespace td {

struct TempPasswordState {
  bool has_temp_password = false;
  string temp_password;
  int32 valid_until = 0;  // unix_time

  tl_object_ptr<td_api::temporaryPasswordState> as_td_api() const;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    CHECK(has_temp_password);
    store(temp_password, storer);
    store(valid_until, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    has_temp_password = true;
    parse(temp_password, parser);
    parse(valid_until, parser);
  }
};

class PasswordManager : public NetQueryCallback {
 public:
  using State = tl_object_ptr<td_api::passwordState>;
  using TempState = tl_object_ptr<td_api::temporaryPasswordState>;

  explicit PasswordManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void get_state(Promise<State> promise);
  void set_password(string current_password, string new_password, string new_hint, bool set_recovery_email_address,
                    string recovery_email_address, Promise<State> promise);
  void set_recovery_email_address(string password, string new_recovery_email_address, Promise<State> promise);
  void get_recovery_email_address(string password, Promise<tl_object_ptr<td_api::recoveryEmailAddress>> promise);

  void request_password_recovery(Promise<tl_object_ptr<td_api::passwordRecoveryInfo>> promise);
  void recover_password(string code, Promise<State> promise);

  void get_temp_password_state(Promise<TempState> promise) /*const*/;
  void create_temp_password(string password, int32 timeout, Promise<TempState> promise);
  void drop_temp_password();

  static TempPasswordState get_temp_password_state_sync();

 private:
  ActorShared<> parent_;

  struct PasswordState {
    bool has_password = false;
    string password_hint;
    bool has_recovery_email_address = false;
    string unconfirmed_recovery_email_address_pattern = "";

    string current_salt;
    string new_salt;

    State as_td_api() const {
      return td_api::make_object<td_api::passwordState>(has_password, password_hint, has_recovery_email_address,
                                                        unconfirmed_recovery_email_address_pattern);
    }
  };

  struct UpdateSettings {
    string current_password;

    bool update_password = false;
    string new_password;
    string new_hint;

    bool update_recovery_email_address = false;
    string recovery_email_address;
  };

  TempPasswordState temp_password_state_;
  Promise<TempState> create_temp_password_promise_;

  void update_password_settings(UpdateSettings update_settings, Promise<State> promise);
  void do_update_password_settings(UpdateSettings update_settings, PasswordState state, Promise<bool> promise);
  void do_get_state(Promise<PasswordState> promise);
  void do_get_recovery_email_address(string password, PasswordState state,
                                     Promise<tl_object_ptr<td_api::recoveryEmailAddress>> promise);

  void do_create_temp_password(string password, int32 timeout, PasswordState &&password_state,
                               Promise<TempPasswordState> promise);
  void on_finish_create_temp_password(Result<TempPasswordState> result, bool dummy);

  void on_result(NetQueryPtr query) override;

  void start_up() override;

  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

}  // namespace td
