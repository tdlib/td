//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/SecureStorage.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/Container.h"
#include "td/utils/logging.h"
#include "td/utils/optional.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

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

  static tl_object_ptr<telegram_api::InputCheckPasswordSRP> get_input_check_password(Slice password, Slice client_salt,
                                                                                     Slice server_salt, int32 g,
                                                                                     Slice p, Slice B, int64 id);

  void get_state(Promise<State> promise);
  void set_password(string current_password, string new_password, string new_hint, bool set_recovery_email_address,
                    string recovery_email_address, Promise<State> promise);
  void set_recovery_email_address(string password, string new_recovery_email_address, Promise<State> promise);
  void get_recovery_email_address(string password, Promise<tl_object_ptr<td_api::recoveryEmailAddress>> promise);

  void send_email_address_verification_code(
      string email, Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise);
  void resend_email_address_verification_code(
      Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise);
  void check_email_address_verification_code(string code, Promise<td_api::object_ptr<td_api::ok>> promise);

  void request_password_recovery(Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise);
  void recover_password(string code, Promise<State> promise);

  void get_secure_secret(string password, Promise<secure_storage::Secret> promise);

  void get_temp_password_state(Promise<TempState> promise) /*const*/;
  void create_temp_password(string password, int32 timeout, Promise<TempState> promise);
  void drop_temp_password();

  static TempPasswordState get_temp_password_state_sync();

 private:
  static constexpr size_t MIN_NEW_SALT_SIZE = 8;
  static constexpr size_t MIN_NEW_SECURE_SALT_SIZE = 8;

  ActorShared<> parent_;

  struct PasswordState {
    bool has_password = false;
    string password_hint;
    bool has_recovery_email_address = false;
    bool has_secure_values = false;
    string unconfirmed_recovery_email_address_pattern;

    string current_client_salt;
    string current_server_salt;
    int32 current_srp_g;
    string current_srp_p;
    string current_srp_B;
    int64 current_srp_id;
    string new_client_salt;
    string new_server_salt;
    int32 new_srp_g;
    string new_srp_p;

    string new_secure_salt;

    State as_td_api() const {
      return td_api::make_object<td_api::passwordState>(has_password, password_hint, has_recovery_email_address,
                                                        has_secure_values, unconfirmed_recovery_email_address_pattern);
    }
  };

  struct PasswordPrivateState {
    string email;
    optional<secure_storage::Secret> secret;
  };

  struct PasswordFullState {
    PasswordState state;
    PasswordPrivateState private_state;
  };

  struct UpdateSettings {
    string current_password;

    bool update_password = false;
    string new_password;
    string new_hint;

    bool update_secure_secret = false;

    bool update_recovery_email_address = false;
    string recovery_email_address;
  };

  optional<secure_storage::Secret> secret_;
  TempPasswordState temp_password_state_;
  Promise<TempState> create_temp_password_promise_;

  string last_verified_email_address_;

  static Result<secure_storage::Secret> decrypt_secure_secret(
      Slice password, tl_object_ptr<telegram_api::SecurePasswordKdfAlgo> algo_ptr, Slice secret, int64 secret_id);

  static BufferSlice calc_password_hash(Slice password, Slice client_salt, Slice server_salt);

  static Result<BufferSlice> calc_password_srp_hash(Slice password, Slice client_salt, Slice server_salt, int32 g,
                                                    Slice p);

  tl_object_ptr<telegram_api::InputCheckPasswordSRP> get_input_check_password(Slice password,
                                                                              const PasswordState &state) const;

  void update_password_settings(UpdateSettings update_settings, Promise<State> promise);
  void do_update_password_settings(UpdateSettings update_settings, PasswordFullState full_state, Promise<bool> promise);
  void do_update_password_settings_impl(UpdateSettings update_settings, PasswordState state,
                                        PasswordPrivateState private_state, Promise<bool> promise);
  void do_get_state(Promise<PasswordState> promise);
  void get_full_state(string password, Promise<PasswordFullState> promise);
  void do_get_secure_secret(bool recursive, string passwod, Promise<secure_storage::Secret> promise);
  void do_get_full_state(string password, PasswordState state, Promise<PasswordFullState> promise);
  void cache_secret(secure_storage::Secret secret);

  void do_create_temp_password(string password, int32 timeout, PasswordState &&password_state,
                               Promise<TempPasswordState> promise);
  void on_finish_create_temp_password(Result<TempPasswordState> result, bool dummy);

  void on_result(NetQueryPtr query) override;

  void start_up() override;
  void hangup() override;

  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

}  // namespace td
