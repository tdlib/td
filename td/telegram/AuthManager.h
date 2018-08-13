//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetActor.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/TermsOfService.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class SendCodeHelper {
 public:
  void on_sent_code(telegram_api::object_ptr<telegram_api::auth_sentCode> sent_code);
  td_api::object_ptr<td_api::authorizationStateWaitCode> get_authorization_state_wait_code(
      const TermsOfService &terms_of_service) const;
  td_api::object_ptr<td_api::authenticationCodeInfo> get_authentication_code_info_object() const;
  Result<telegram_api::auth_resendCode> resend_code();

  Result<telegram_api::auth_sendCode> send_code(Slice phone_number, bool allow_flash_call, bool is_current_phone_number,
                                                int32 api_id, const string &api_hash);

  Result<telegram_api::account_sendChangePhoneCode> send_change_phone_code(Slice phone_number, bool allow_flash_call,
                                                                           bool is_current_phone_number);

  Result<telegram_api::account_sendVerifyPhoneCode> send_verify_phone_code(const string &hash, Slice phone_number,
                                                                           bool allow_flash_call,
                                                                           bool is_current_phone_number);

  Result<telegram_api::account_sendConfirmPhoneCode> send_confirm_phone_code(Slice phone_number, bool allow_flash_call,
                                                                             bool is_current_phone_number);

  Slice phone_number() const {
    return phone_number_;
  }
  Slice phone_code_hash() const {
    return phone_code_hash_;
  }
  bool phone_registered() const {
    return phone_registered_;
  }

  template <class T>
  void store(T &storer) const;
  template <class T>
  void parse(T &parser);

 private:
  static constexpr int32 AUTH_SEND_CODE_FLAG_ALLOW_FLASH_CALL = 1 << 0;

  static constexpr int32 SENT_CODE_FLAG_IS_USER_REGISTERED = 1 << 0;
  static constexpr int32 SENT_CODE_FLAG_HAS_NEXT_TYPE = 1 << 1;
  static constexpr int32 SENT_CODE_FLAG_HAS_TIMEOUT = 1 << 2;

  struct AuthenticationCodeInfo {
    enum class Type : int32 { None, Message, Sms, Call, FlashCall };
    Type type = Type::None;
    int32 length = 0;
    string pattern;

    AuthenticationCodeInfo() = default;
    AuthenticationCodeInfo(Type type, int length, string pattern)
        : type(type), length(length), pattern(std::move(pattern)) {
    }

    template <class T>
    void store(T &storer) const;
    template <class T>
    void parse(T &parser);
  };

  string phone_number_;
  bool phone_registered_;
  string phone_code_hash_;

  SendCodeHelper::AuthenticationCodeInfo sent_code_info_;
  SendCodeHelper::AuthenticationCodeInfo next_code_info_;
  Timestamp next_code_timestamp_;

  static AuthenticationCodeInfo get_authentication_code_info(
      tl_object_ptr<telegram_api::auth_CodeType> &&code_type_ptr);
  static AuthenticationCodeInfo get_authentication_code_info(
      tl_object_ptr<telegram_api::auth_SentCodeType> &&sent_code_type_ptr);

  static tl_object_ptr<td_api::AuthenticationCodeType> get_authentication_code_type_object(
      const AuthenticationCodeInfo &authentication_code_info);
};

class PhoneNumberManager : public NetActor {
 public:
  enum class Type : int32 { ChangePhone, VerifyPhone, ConfirmPhone };
  PhoneNumberManager(Type type, ActorShared<> parent);
  void get_state(uint64 query_id);

  void set_phone_number(uint64 query_id, string phone_number, bool allow_flash_call, bool is_current_phone_number);
  void set_phone_number_and_hash(uint64 query_id, string hash, string phone_number, bool allow_flash_call,
                                 bool is_current_phone_number);

  void resend_authentication_code(uint64 query_id);
  void check_code(uint64 query_id, string code);

 private:
  Type type_;

  enum class State : int32 { Ok, WaitCode } state_ = State::Ok;
  enum class NetQueryType : int32 { None, SendCode, CheckCode };

  ActorShared<> parent_;
  uint64 query_id_ = 0;
  uint64 net_query_id_ = 0;
  NetQueryType net_query_type_;

  SendCodeHelper send_code_helper_;

  void on_new_query(uint64 query_id);
  void on_query_error(Status status);
  void on_query_error(uint64 id, Status status);
  void on_query_ok();
  void start_net_query(NetQueryType net_query_type, NetQueryPtr net_query);

  template <class T>
  void process_send_code_result(uint64 query_id, T r_send_code);

  template <class T>
  void send_new_check_code_query(const T &query);

  template <class T>
  void process_check_code_result(T result);

  void on_check_code_result(NetQueryPtr &result);
  void on_send_code_result(NetQueryPtr &result);
  void on_result(NetQueryPtr result) override;
  void tear_down() override;
};

class AuthManager : public NetActor {
 public:
  AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent);

  bool is_bot() const;
  void set_is_bot(bool is_bot);

  bool is_authorized() const;
  void get_state(uint64 query_id);

  void set_phone_number(uint64 query_id, string phone_number, bool allow_flash_call, bool is_current_phone_number);
  void resend_authentication_code(uint64 query_id);
  void check_code(uint64 query_id, string code, string first_name, string last_name);
  void check_bot_token(uint64 query_id, string bot_token);
  void check_password(uint64 query_id, string password);
  void request_password_recovery(uint64 query_id);
  void recover_password(uint64 query_id, string code);
  void logout(uint64 query_id);
  void delete_account(uint64 query_id, const string &reason);

  void on_closing();

 private:
  static constexpr size_t MAX_NAME_LENGTH = 255;  // server side limit

  enum class State : int32 {
    None,
    WaitPhoneNumber,
    WaitCode,
    WaitPassword,
    Ok,
    LoggingOut,
    Closing
  } state_ = State::None;
  enum class NetQueryType : int32 {
    None,
    SignIn,
    SignUp,
    SendCode,
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

    template <class T>
    void store(T &storer) const;
    template <class T>
    void parse(T &parser);
  };

  struct DbState {
    State state_;
    int32 api_id_;
    string api_hash_;
    Timestamp state_timestamp_;

    // WaitCode
    SendCodeHelper send_code_helper_;
    TermsOfService terms_of_service_;

    // WaitPassword
    WaitPasswordState wait_password_state_;

    static DbState wait_code(int32 api_id, string api_hash, SendCodeHelper send_code_helper,
                             TermsOfService terms_of_service) {
      DbState state;
      state.state_ = State::WaitCode;
      state.api_id_ = api_id;
      state.api_hash_ = api_hash;
      state.send_code_helper_ = std::move(send_code_helper);
      state.terms_of_service_ = std::move(terms_of_service);
      state.state_timestamp_ = Timestamp::now();
      return state;
    }

    static DbState wait_password(int32 api_id, string api_hash, WaitPasswordState wait_password_state) {
      DbState state;
      state.state_ = State::WaitPassword;
      state.api_id_ = api_id;
      state.api_hash_ = api_hash;
      state.wait_password_state_ = std::move(wait_password_state);
      state.state_timestamp_ = Timestamp::now();
      return state;
    }

    template <class T>
    void store(T &storer) const;
    template <class T>
    void parse(T &parser);
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
  string password_;
  TermsOfService terms_of_service_;

  // for bots
  string bot_token_;
  uint64 query_id_ = 0;

  WaitPasswordState wait_password_state_;

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

  void on_send_code_result(NetQueryPtr &result);
  void on_get_password_result(NetQueryPtr &result);
  void on_request_password_recovery_result(NetQueryPtr &result);
  void on_authentication_result(NetQueryPtr &result, bool expected_flag);
  void on_log_out_result(NetQueryPtr &result);
  void on_delete_account_result(NetQueryPtr &result);
  void on_authorization(tl_object_ptr<telegram_api::auth_authorization> auth);

  void on_result(NetQueryPtr result) override;

  void update_state(State new_state, bool force = false, bool should_save_state = true);
  tl_object_ptr<td_api::AuthorizationState> get_authorization_state_object(State authorization_state) const;
  void send_ok(uint64 query_id);

  void start_up() override;
  void tear_down() override;
};
}  // namespace td
