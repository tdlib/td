//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetActor.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/SendCodeHelper.h"
#include "td/telegram/TermsOfService.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class AuthManager : public NetActor {
 public:
  AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent);

  bool is_bot() const;

  bool is_authorized() const;
  bool was_authorized() const;
  void get_state(uint64 query_id);

  void set_phone_number(uint64 query_id, string phone_number,
                        td_api::object_ptr<td_api::phoneNumberAuthenticationSettings> settings);
  void resend_authentication_code(uint64 query_id);
  void check_code(uint64 query_id, string code);
  void register_user(uint64 query_id, string first_name, string last_name);
  void request_qr_code_authentication(uint64 query_id, vector<int32> other_user_ids);
  void check_bot_token(uint64 query_id, string bot_token);
  void check_password(uint64 query_id, string password);
  void request_password_recovery(uint64 query_id);
  void recover_password(uint64 query_id, string code);
  void log_out(uint64 query_id);
  void delete_account(uint64 query_id, const string &reason);

  void on_update_login_token();

  void on_authorization_lost();
  void on_closing(bool destroy_flag);

  // can return nullptr if state isn't initialized yet
  tl_object_ptr<td_api::AuthorizationState> get_current_authorization_state_object() const;

 private:
  static constexpr size_t MAX_NAME_LENGTH = 64;  // server side limit

  enum class State : int32 {
    None,
    WaitPhoneNumber,
    WaitCode,
    WaitQrCodeConfirmation,
    WaitPassword,
    WaitRegistration,
    Ok,
    LoggingOut,
    DestroyingKeys,
    Closing
  } state_ = State::None;
  enum class NetQueryType : int32 {
    None,
    SignIn,
    SignUp,
    SendCode,
    RequestQrCode,
    ImportQrCode,
    GetPassword,
    CheckPassword,
    RequestPasswordRecovery,
    RecoverPassword,
    BotAuthentication,
    Authentication,
    LogOut,
    DeleteAccount
  };

  struct WaitPasswordState {
    string current_client_salt_;
    string current_server_salt_;
    int32 srp_g_ = 0;
    string srp_p_;
    string srp_B_;
    int64 srp_id_ = 0;
    string hint_;
    bool has_recovery_ = false;
    string email_address_pattern_;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct DbState {
    State state_;
    int32 api_id_;
    string api_hash_;
    Timestamp state_timestamp_;

    // WaitCode
    SendCodeHelper send_code_helper_;

    // WaitQrCodeConfirmation
    vector<int32> other_user_ids_;
    string login_token_;
    double login_token_expires_at_ = 0;

    // WaitPassword
    WaitPasswordState wait_password_state_;

    // WaitRegistration
    TermsOfService terms_of_service_;

    DbState() = default;
    static DbState wait_code(int32 api_id, string api_hash, SendCodeHelper send_code_helper) {
      DbState state(State::WaitCode, api_id, api_hash);
      state.send_code_helper_ = std::move(send_code_helper);
      return state;
    }

    static DbState wait_qr_code_confirmation(int32 api_id, string api_hash, vector<int32> other_user_ids,
                                             string login_token, double login_token_expires_at) {
      DbState state(State::WaitQrCodeConfirmation, api_id, api_hash);
      state.other_user_ids_ = std::move(other_user_ids);
      state.login_token_ = std::move(login_token);
      state.login_token_expires_at_ = login_token_expires_at;
      return state;
    }

    static DbState wait_password(int32 api_id, string api_hash, WaitPasswordState wait_password_state) {
      DbState state(State::WaitPassword, api_id, api_hash);
      state.wait_password_state_ = std::move(wait_password_state);
      return state;
    }

    static DbState wait_registration(int32 api_id, string api_hash, SendCodeHelper send_code_helper,
                                     TermsOfService terms_of_service) {
      DbState state(State::WaitRegistration, api_id, api_hash);
      state.send_code_helper_ = std::move(send_code_helper);
      state.terms_of_service_ = std::move(terms_of_service);
      return state;
    }

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);

   private:
    DbState(State state, int32 api_id, string api_hash)
        : state_(state), api_id_(api_id), api_hash_(api_hash), state_timestamp_(Timestamp::now()) {
    }
  };

  bool load_state();
  void save_state();

  ActorShared<> parent_;

  // STATE
  // from contructor
  int32 api_id_;
  string api_hash_;

  // State::WaitCode
  SendCodeHelper send_code_helper_;
  string code_;

  // State::WaitQrCodeConfirmation
  vector<int32> other_user_ids_;
  string login_token_;
  double login_token_expires_at_ = 0.0;
  int32 imported_dc_id_ = -1;

  // State::WaitPassword
  string password_;

  // State::WaitRegistration
  TermsOfService terms_of_service_;

  // for bots
  string bot_token_;

  uint64 query_id_ = 0;

  WaitPasswordState wait_password_state_;

  int32 login_code_retry_delay_ = 0;
  Timeout poll_export_login_code_timeout_;

  bool was_qr_code_request_ = false;
  bool was_check_bot_token_ = false;
  bool is_bot_ = false;
  uint64 net_query_id_ = 0;
  NetQueryType net_query_type_;

  vector<uint64> pending_get_authorization_state_requests_;

  void on_new_query(uint64 query_id);
  void on_query_error(Status status);
  void on_query_error(uint64 id, Status status);
  void on_query_ok();
  void start_net_query(NetQueryType net_query_type, NetQueryPtr net_query);

  static void on_update_login_token_static(void *td);
  void send_export_login_token_query();
  void set_login_token_expires_at(double login_token_expires_at);
  void destroy_auth_keys();

  void on_send_code_result(NetQueryPtr &result);
  void on_request_qr_code_result(NetQueryPtr &result, bool is_import);
  void on_get_password_result(NetQueryPtr &result);
  void on_request_password_recovery_result(NetQueryPtr &result);
  void on_authentication_result(NetQueryPtr &result, bool expected_flag);
  void on_log_out_result(NetQueryPtr &result);
  void on_delete_account_result(NetQueryPtr &result);
  void on_get_login_token(tl_object_ptr<telegram_api::auth_LoginToken> login_token);
  void on_get_authorization(tl_object_ptr<telegram_api::auth_Authorization> auth_ptr);

  void on_result(NetQueryPtr result) override;

  void update_state(State new_state, bool force = false, bool should_save_state = true);
  tl_object_ptr<td_api::AuthorizationState> get_authorization_state_object(State authorization_state) const;
  void send_ok(uint64 query_id);

  void start_up() override;
  void tear_down() override;
};

}  // namespace td
