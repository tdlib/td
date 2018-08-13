//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AuthManager.h"
#include "td/telegram/AuthManager.hpp"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"

namespace td {

// SendCodeHelper
void SendCodeHelper::on_sent_code(telegram_api::object_ptr<telegram_api::auth_sentCode> sent_code) {
  phone_registered_ = (sent_code->flags_ & SENT_CODE_FLAG_IS_USER_REGISTERED) != 0;
  phone_code_hash_ = sent_code->phone_code_hash_;
  sent_code_info_ = get_authentication_code_info(std::move(sent_code->type_));
  next_code_info_ = get_authentication_code_info(std::move(sent_code->next_type_));
  next_code_timestamp_ = Timestamp::in((sent_code->flags_ & SENT_CODE_FLAG_HAS_TIMEOUT) != 0 ? sent_code->timeout_ : 0);
}

td_api::object_ptr<td_api::authorizationStateWaitCode> SendCodeHelper::get_authorization_state_wait_code(
    const TermsOfService &terms_of_service) const {
  return make_tl_object<td_api::authorizationStateWaitCode>(
      phone_registered_, terms_of_service.get_terms_of_service_object(), get_authentication_code_info_object());
}

td_api::object_ptr<td_api::authenticationCodeInfo> SendCodeHelper::get_authentication_code_info_object() const {
  return make_tl_object<td_api::authenticationCodeInfo>(
      phone_number_, get_authentication_code_type_object(sent_code_info_),
      get_authentication_code_type_object(next_code_info_),
      max(static_cast<int32>(next_code_timestamp_.in() + 1 - 1e-9), 0));
}

Result<telegram_api::auth_resendCode> SendCodeHelper::resend_code() {
  if (next_code_info_.type == AuthenticationCodeInfo::Type::None) {
    return Status::Error(8, "Authentication code can't be resend");
  }
  sent_code_info_ = next_code_info_;
  next_code_info_ = {};
  next_code_timestamp_ = {};
  return telegram_api::auth_resendCode(phone_number_, phone_code_hash_);
}

Result<telegram_api::auth_sendCode> SendCodeHelper::send_code(Slice phone_number, bool allow_flash_call,
                                                              bool is_current_phone_number, int32 api_id,
                                                              const string &api_hash) {
  if (!phone_number_.empty()) {
    return Status::Error(8, "Can't change phone");
  }
  phone_number_ = phone_number.str();
  int32 flags = 0;
  if (allow_flash_call) {
    flags |= AUTH_SEND_CODE_FLAG_ALLOW_FLASH_CALL;
  }
  return telegram_api::auth_sendCode(flags, false /*ignored*/, phone_number_, is_current_phone_number, api_id,
                                     api_hash);
}

Result<telegram_api::account_sendChangePhoneCode> SendCodeHelper::send_change_phone_code(Slice phone_number,
                                                                                         bool allow_flash_call,
                                                                                         bool is_current_phone_number) {
  phone_number_ = phone_number.str();
  int32 flags = 0;
  if (allow_flash_call) {
    flags |= AUTH_SEND_CODE_FLAG_ALLOW_FLASH_CALL;
  }
  return telegram_api::account_sendChangePhoneCode(flags, false /*ignored*/, phone_number_, is_current_phone_number);
}

Result<telegram_api::account_sendVerifyPhoneCode> SendCodeHelper::send_verify_phone_code(const string &hash,
                                                                                         Slice phone_number,
                                                                                         bool allow_flash_call,
                                                                                         bool is_current_phone_number) {
  phone_number_ = phone_number.str();
  int32 flags = 0;
  if (allow_flash_call) {
    flags |= AUTH_SEND_CODE_FLAG_ALLOW_FLASH_CALL;
  }
  return telegram_api::account_sendVerifyPhoneCode(flags, false /*ignored*/, hash, is_current_phone_number);
}

Result<telegram_api::account_sendConfirmPhoneCode> SendCodeHelper::send_confirm_phone_code(
    Slice phone_number, bool allow_flash_call, bool is_current_phone_number) {
  phone_number_ = phone_number.str();
  int32 flags = 0;
  if (allow_flash_call) {
    flags |= AUTH_SEND_CODE_FLAG_ALLOW_FLASH_CALL;
  }
  return telegram_api::account_sendConfirmPhoneCode(flags, false /*ignored*/, phone_number_, is_current_phone_number);
}

SendCodeHelper::AuthenticationCodeInfo SendCodeHelper::get_authentication_code_info(
    tl_object_ptr<telegram_api::auth_CodeType> &&code_type_ptr) {
  if (code_type_ptr == nullptr) {
    return AuthenticationCodeInfo();
  }

  switch (code_type_ptr->get_id()) {
    case telegram_api::auth_codeTypeSms::ID:
      return {AuthenticationCodeInfo::Type::Sms, 0, ""};
    case telegram_api::auth_codeTypeCall::ID:
      return {AuthenticationCodeInfo::Type::Call, 0, ""};
    case telegram_api::auth_codeTypeFlashCall::ID:
      return {AuthenticationCodeInfo::Type::FlashCall, 0, ""};
    default:
      UNREACHABLE();
      return AuthenticationCodeInfo();
  }
}

SendCodeHelper::AuthenticationCodeInfo SendCodeHelper::get_authentication_code_info(
    tl_object_ptr<telegram_api::auth_SentCodeType> &&sent_code_type_ptr) {
  CHECK(sent_code_type_ptr != nullptr);
  switch (sent_code_type_ptr->get_id()) {
    case telegram_api::auth_sentCodeTypeApp::ID: {
      auto code_type = move_tl_object_as<telegram_api::auth_sentCodeTypeApp>(sent_code_type_ptr);
      return AuthenticationCodeInfo{AuthenticationCodeInfo::Type::Message, code_type->length_, ""};
    }
    case telegram_api::auth_sentCodeTypeSms::ID: {
      auto code_type = move_tl_object_as<telegram_api::auth_sentCodeTypeSms>(sent_code_type_ptr);
      return AuthenticationCodeInfo{AuthenticationCodeInfo::Type::Sms, code_type->length_, ""};
    }
    case telegram_api::auth_sentCodeTypeCall::ID: {
      auto code_type = move_tl_object_as<telegram_api::auth_sentCodeTypeCall>(sent_code_type_ptr);
      return AuthenticationCodeInfo{AuthenticationCodeInfo::Type::Call, code_type->length_, ""};
    }
    case telegram_api::auth_sentCodeTypeFlashCall::ID: {
      auto code_type = move_tl_object_as<telegram_api::auth_sentCodeTypeFlashCall>(sent_code_type_ptr);
      return AuthenticationCodeInfo{AuthenticationCodeInfo::Type::FlashCall, 0, code_type->pattern_};
    }
    default:
      UNREACHABLE();
      return AuthenticationCodeInfo();
  }
}

tl_object_ptr<td_api::AuthenticationCodeType> SendCodeHelper::get_authentication_code_type_object(
    const AuthenticationCodeInfo &authentication_code_info) {
  switch (authentication_code_info.type) {
    case AuthenticationCodeInfo::Type::None:
      return nullptr;
    case AuthenticationCodeInfo::Type::Message:
      return make_tl_object<td_api::authenticationCodeTypeTelegramMessage>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::Sms:
      return make_tl_object<td_api::authenticationCodeTypeSms>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::Call:
      return make_tl_object<td_api::authenticationCodeTypeCall>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::FlashCall:
      return make_tl_object<td_api::authenticationCodeTypeFlashCall>(authentication_code_info.pattern);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

// PhoneNumberManager
void PhoneNumberManager::get_state(uint64 query_id) {
  tl_object_ptr<td_api::Object> obj;
  switch (state_) {
    case State::Ok:
      obj = make_tl_object<td_api::ok>();
      break;
    case State::WaitCode:
      obj = send_code_helper_.get_authentication_code_info_object();
      break;
  }
  CHECK(obj);
  send_closure(G()->td(), &Td::send_result, query_id, std::move(obj));
}

PhoneNumberManager::PhoneNumberManager(PhoneNumberManager::Type type, ActorShared<> parent)
    : type_(type), parent_(std::move(parent)) {
}

template <class T>
void PhoneNumberManager::process_send_code_result(uint64 query_id, T r_send_code) {
  if (r_send_code.is_error()) {
    return on_query_error(query_id, r_send_code.move_as_error());
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create(create_storer(r_send_code.move_as_ok())));
}

void PhoneNumberManager::set_phone_number(uint64 query_id, string phone_number, bool allow_flash_call,
                                          bool is_current_phone_number) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }

  switch (type_) {
    case Type::ChangePhone:
      return process_send_code_result(
          query_id, send_code_helper_.send_change_phone_code(phone_number, allow_flash_call, is_current_phone_number));
    case Type::ConfirmPhone:
      return process_send_code_result(
          query_id, send_code_helper_.send_confirm_phone_code(phone_number, allow_flash_call, is_current_phone_number));
    case Type::VerifyPhone:
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::set_phone_number_and_hash(uint64 query_id, string hash, string phone_number,
                                                   bool allow_flash_call, bool is_current_phone_number) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }
  if (hash.empty()) {
    return on_query_error(query_id, Status::Error(8, "Hash can't be empty"));
  }

  switch (type_) {
    case Type::VerifyPhone:
      return process_send_code_result(query_id, send_code_helper_.send_verify_phone_code(
                                                    hash, phone_number, allow_flash_call, is_current_phone_number));
    case Type::ChangePhone:
    case Type::ConfirmPhone:
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::resend_authentication_code(uint64 query_id) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(8, "resendAuthenticationCode unexpected"));
  }

  auto r_resend_code = send_code_helper_.resend_code();
  if (r_resend_code.is_error()) {
    return on_query_error(query_id, r_resend_code.move_as_error());
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode,
                  G()->net_query_creator().create(create_storer(r_resend_code.move_as_ok()), DcId::main(),
                                                  NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

template <class T>
void PhoneNumberManager::send_new_check_code_query(const T &query) {
  start_net_query(NetQueryType::CheckCode, G()->net_query_creator().create(create_storer(query)));
}

void PhoneNumberManager::check_code(uint64 query_id, string code) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(8, "checkAuthenticationCode unexpected"));
  }

  on_new_query(query_id);

  switch (type_) {
    case Type::ChangePhone:
      return send_new_check_code_query(telegram_api::account_changePhone(
          send_code_helper_.phone_number().str(), send_code_helper_.phone_code_hash().str(), code));
    case Type::ConfirmPhone:
      return send_new_check_code_query(
          telegram_api::account_confirmPhone(send_code_helper_.phone_code_hash().str(), code));
    case Type::VerifyPhone:
      return send_new_check_code_query(telegram_api::account_verifyPhone(
          send_code_helper_.phone_number().str(), send_code_helper_.phone_code_hash().str(), code));
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::on_new_query(uint64 query_id) {
  if (query_id_ != 0) {
    on_query_error(Status::Error(9, "Another authorization query has started"));
  }
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  query_id_ = query_id;
  // TODO: cancel older net_query
}

void PhoneNumberManager::on_query_error(Status status) {
  CHECK(query_id_ != 0);
  auto id = query_id_;
  query_id_ = 0;
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  on_query_error(id, std::move(status));
}

void PhoneNumberManager::on_query_error(uint64 id, Status status) {
  send_closure(G()->td(), &Td::send_error, id, std::move(status));
}

void PhoneNumberManager::on_query_ok() {
  CHECK(query_id_ != 0);
  auto id = query_id_;
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  query_id_ = 0;
  get_state(id);
}

void PhoneNumberManager::start_net_query(NetQueryType net_query_type, NetQueryPtr net_query) {
  // TODO: cancel old net_query?
  net_query_type_ = net_query_type;
  net_query_id_ = net_query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this));
}

template <class T>
void PhoneNumberManager::process_check_code_result(T result) {
  if (result.is_error()) {
    return on_query_error(result.move_as_error());
  }
  state_ = State::Ok;
  on_query_ok();
}

void PhoneNumberManager::on_check_code_result(NetQueryPtr &result) {
  switch (type_) {
    case Type::ChangePhone:
      return process_check_code_result(fetch_result<telegram_api::account_changePhone>(result->ok()));
    case Type::VerifyPhone:
      return process_check_code_result(fetch_result<telegram_api::account_verifyPhone>(result->ok()));
    case Type::ConfirmPhone:
      return process_check_code_result(fetch_result<telegram_api::account_confirmPhone>(result->ok()));
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::on_send_code_result(NetQueryPtr &result) {
  auto r_sent_code = [&] {
    switch (type_) {
      case Type::ChangePhone:
        return fetch_result<telegram_api::account_sendChangePhoneCode>(result->ok());
      case Type::VerifyPhone:
        return fetch_result<telegram_api::account_sendVerifyPhoneCode>(result->ok());
      case Type::ConfirmPhone:
        return fetch_result<telegram_api::account_sendConfirmPhoneCode>(result->ok());
      default:
        UNREACHABLE();
        return fetch_result<telegram_api::account_sendChangePhoneCode>(result->ok());
    }
  }();
  if (r_sent_code.is_error()) {
    return on_query_error(r_sent_code.move_as_error());
  }
  auto sent_code = r_sent_code.move_as_ok();

  LOG(INFO) << "Receive " << to_string(sent_code);

  send_code_helper_.on_sent_code(std::move(sent_code));

  state_ = State::WaitCode;
  on_query_ok();
}

void PhoneNumberManager::on_result(NetQueryPtr result) {
  SCOPE_EXIT {
    result->clear();
  };
  NetQueryType type = NetQueryType::None;
  if (result->id() == net_query_id_) {
    net_query_id_ = 0;
    type = net_query_type_;
    net_query_type_ = NetQueryType::None;
    if (result->is_error()) {
      if (query_id_ != 0) {
        on_query_error(std::move(result->error()));
      }
      return;
    }
  }
  switch (type) {
    case NetQueryType::None:
      result->ignore();
      break;
    case NetQueryType::SendCode:
      on_send_code_result(result);
      break;
    case NetQueryType::CheckCode:
      on_check_code_result(result);
      break;
  }
}

void PhoneNumberManager::tear_down() {
  parent_.reset();
}

// AuthManager
AuthManager::AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent)
    : parent_(std::move(parent)), api_id_(api_id), api_hash_(api_hash) {
  string auth_str = G()->td_db()->get_binlog_pmc()->get("auth");
  if (auth_str == "ok") {
    string is_bot_str = G()->td_db()->get_binlog_pmc()->get("auth_is_bot");
    if (is_bot_str == "true") {
      is_bot_ = true;
    }
    auto my_id = ContactsManager::load_my_id();
    if (my_id.is_valid()) {
      // just in case
      G()->shared_config().set_option_integer("my_id", my_id.get());
      update_state(State::Ok);
    } else {
      LOG(ERROR) << "Restore unknown my_id";
      ContactsManager::send_get_me_query(
          G()->td().get_actor_unsafe(),
          PromiseCreator::lambda([this](Result<Unit> result) { update_state(State::Ok); }));
    }
  } else if (auth_str == "logout") {
    update_state(State::LoggingOut);
  } else {
    if (!load_state()) {
      update_state(State::WaitPhoneNumber);
    }
  }
}

void AuthManager::start_up() {
  if (state_ == State::LoggingOut) {
    start_net_query(NetQueryType::LogOut, G()->net_query_creator().create(create_storer(telegram_api::auth_logOut())));
  }
}
void AuthManager::tear_down() {
  parent_.reset();
}

bool AuthManager::is_bot() const {
  return is_authorized() && is_bot_;
}

void AuthManager::set_is_bot(bool is_bot) {
  if (!is_bot_ && is_bot && api_id_ == 23818) {
    LOG(ERROR) << "Fix is_bot to " << is_bot;
    G()->td_db()->get_binlog_pmc()->set("auth_is_bot", "true");
    is_bot_ = true;
  }
}

bool AuthManager::is_authorized() const {
  return state_ == State::Ok;
}

tl_object_ptr<td_api::AuthorizationState> AuthManager::get_authorization_state_object(State authorization_state) const {
  switch (authorization_state) {
    case State::Ok:
      return make_tl_object<td_api::authorizationStateReady>();
    case State::WaitCode:
      return send_code_helper_.get_authorization_state_wait_code(terms_of_service_);
    case State::WaitPhoneNumber:
      return make_tl_object<td_api::authorizationStateWaitPhoneNumber>();
    case State::WaitPassword:
      return make_tl_object<td_api::authorizationStateWaitPassword>(
          wait_password_state_.hint_, wait_password_state_.has_recovery_, wait_password_state_.email_address_pattern_);
    case State::LoggingOut:
      return make_tl_object<td_api::authorizationStateLoggingOut>();
    case State::Closing:
      return make_tl_object<td_api::authorizationStateClosing>();
    case State::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void AuthManager::get_state(uint64 query_id) {
  if (state_ == State::None) {
    pending_get_authorization_state_requests_.push_back(query_id);
  } else {
    send_closure(G()->td(), &Td::send_result, query_id, get_authorization_state_object(state_));
  }
}

void AuthManager::check_bot_token(uint64 query_id, string bot_token) {
  if (state_ != State::WaitPhoneNumber && state_ != State::Ok) {
    // TODO do not allow State::Ok
    return on_query_error(query_id, Status::Error(8, "checkAuthenticationBotToken unexpected"));
  }
  if (!send_code_helper_.phone_number().empty()) {
    return on_query_error(
        query_id, Status::Error(8, "Cannot set bot token after authentication beginning. You need to log out first"));
  }
  if (was_check_bot_token_ && bot_token_ != bot_token) {
    return on_query_error(query_id, Status::Error(8, "Cannot change bot token. You need to log out first"));
  }
  if (state_ == State::Ok) {
    if (!is_bot_) {
      // fix old bots
      const int32 AUTH_IS_BOT_FIXED_DATE = 1500940800;
      if (G()->shared_config().get_option_integer("authorization_date") < AUTH_IS_BOT_FIXED_DATE) {
        G()->td_db()->get_binlog_pmc()->set("auth_is_bot", "true");
        is_bot_ = true;
      }
    }
    return send_ok(query_id);
  }

  on_new_query(query_id);
  bot_token_ = bot_token;
  was_check_bot_token_ = true;
  start_net_query(NetQueryType::BotAuthentication,
                  G()->net_query_creator().create(
                      create_storer(telegram_api::auth_importBotAuthorization(0, api_id_, api_hash_, bot_token_)),
                      DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::set_phone_number(uint64 query_id, string phone_number, bool allow_flash_call,
                                   bool is_current_phone_number) {
  if (state_ != State::WaitPhoneNumber) {
    if ((state_ == State::WaitCode || state_ == State::WaitPassword) && net_query_id_ == 0) {
      // ok
    } else {
      return on_query_error(query_id, Status::Error(8, "setAuthenticationPhoneNumber unexpected"));
    }
  }
  if (was_check_bot_token_) {
    return on_query_error(
        query_id, Status::Error(8, "Cannot set phone number after bot token was entered. You need to log out first"));
  }
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }

  auto r_send_code =
      send_code_helper_.send_code(phone_number, allow_flash_call, is_current_phone_number, api_id_, api_hash_);
  if (r_send_code.is_error()) {
    send_code_helper_ = SendCodeHelper();
    terms_of_service_ = TermsOfService();
    r_send_code =
        send_code_helper_.send_code(phone_number, allow_flash_call, is_current_phone_number, api_id_, api_hash_);
    if (r_send_code.is_error()) {
      return on_query_error(query_id, r_send_code.move_as_error());
    }
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode,
                  G()->net_query_creator().create(create_storer(r_send_code.move_as_ok()), DcId::main(),
                                                  NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::resend_authentication_code(uint64 query_id) {
  if (state_ != State::WaitCode || was_check_bot_token_) {
    return on_query_error(query_id, Status::Error(8, "resendAuthenticationCode unexpected"));
  }

  auto r_resend_code = send_code_helper_.resend_code();
  if (r_resend_code.is_error()) {
    return on_query_error(query_id, r_resend_code.move_as_error());
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode,
                  G()->net_query_creator().create(create_storer(r_resend_code.move_as_ok()), DcId::main(),
                                                  NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::check_code(uint64 query_id, string code, string first_name, string last_name) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(8, "checkAuthenticationCode unexpected"));
  }

  code_ = code;
  on_new_query(query_id);
  if (send_code_helper_.phone_registered() || first_name.empty()) {
    start_net_query(NetQueryType::SignIn,
                    G()->net_query_creator().create(
                        create_storer(telegram_api::auth_signIn(send_code_helper_.phone_number().str(),
                                                                send_code_helper_.phone_code_hash().str(), code)),
                        DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
  } else {
    first_name = clean_name(first_name, MAX_NAME_LENGTH);
    if (first_name.empty()) {
      return on_query_error(Status::Error(8, "First name can't be empty"));
    }

    last_name = clean_name(last_name, MAX_NAME_LENGTH);
    start_net_query(
        NetQueryType::SignUp,
        G()->net_query_creator().create(create_storer(telegram_api::auth_signUp(
                                            send_code_helper_.phone_number().str(),
                                            send_code_helper_.phone_code_hash().str(), code, first_name, last_name)),
                                        DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
  }
}

void AuthManager::check_password(uint64 query_id, string password) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "checkAuthenticationPassword unexpected"));
  }

  LOG(INFO) << "Have SRP id " << wait_password_state_.srp_id_;
  on_new_query(query_id);
  password_ = std::move(password);
  start_net_query(NetQueryType::GetPassword,
                  G()->net_query_creator().create(create_storer(telegram_api::account_getPassword()), DcId::main(),
                                                  NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::request_password_recovery(uint64 query_id) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "requestAuthenticationPasswordRecovery unexpected"));
  }

  on_new_query(query_id);
  start_net_query(NetQueryType::RequestPasswordRecovery,
                  G()->net_query_creator().create(create_storer(telegram_api::auth_requestPasswordRecovery()),
                                                  DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::recover_password(uint64 query_id, string code) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "recoverAuthenticationPassword unexpected"));
  }

  on_new_query(query_id);
  start_net_query(NetQueryType::RecoverPassword,
                  G()->net_query_creator().create(create_storer(telegram_api::auth_recoverPassword(code)), DcId::main(),
                                                  NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::logout(uint64 query_id) {
  if (state_ == State::Closing) {
    return on_query_error(query_id, Status::Error(8, "Already logged out"));
  }
  if (state_ == State::LoggingOut) {
    return on_query_error(query_id, Status::Error(8, "Already logging out"));
  }
  on_new_query(query_id);
  if (state_ != State::Ok) {
    update_state(State::LoggingOut);
    // TODO: could skip full logout if still no authorization
    // TODO: send auth.cancelCode if state_ == State::WaitCode
    send_closure_later(G()->td(), &Td::destroy);
    on_query_ok();
  } else {
    LOG(INFO) << "Logging out";
    G()->td_db()->get_binlog_pmc()->set("auth", "logout");
    update_state(State::LoggingOut);
    start_net_query(NetQueryType::LogOut, G()->net_query_creator().create(create_storer(telegram_api::auth_logOut())));
  }
}

void AuthManager::delete_account(uint64 query_id, const string &reason) {
  if (state_ != State::Ok && state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "Need to log in first"));
  }
  on_new_query(query_id);
  LOG(INFO) << "Deleting account";
  start_net_query(NetQueryType::DeleteAccount,
                  G()->net_query_creator().create(create_storer(telegram_api::account_deleteAccount(reason)),
                                                  DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
}

void AuthManager::on_closing() {
  update_state(State::Closing);
}

void AuthManager::on_new_query(uint64 query_id) {
  if (query_id_ != 0) {
    on_query_error(Status::Error(9, "Another authorization query has started"));
  }
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  query_id_ = query_id;
  // TODO: cancel older net_query
}

void AuthManager::on_query_error(Status status) {
  CHECK(query_id_ != 0);
  auto id = query_id_;
  query_id_ = 0;
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  on_query_error(id, std::move(status));
}

void AuthManager::on_query_error(uint64 id, Status status) {
  send_closure(G()->td(), &Td::send_error, id, std::move(status));
}

void AuthManager::on_query_ok() {
  CHECK(query_id_ != 0);
  auto id = query_id_;
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  query_id_ = 0;
  send_ok(id);
}

void AuthManager::send_ok(uint64 query_id) {
  send_closure(G()->td(), &Td::send_result, query_id, td_api::make_object<td_api::ok>());
}

void AuthManager::start_net_query(NetQueryType net_query_type, NetQueryPtr net_query) {
  // TODO: cancel old net_query?
  net_query_type_ = net_query_type;
  net_query_id_ = net_query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this));
}

void AuthManager::on_send_code_result(NetQueryPtr &result) {
  auto r_sent_code = fetch_result<telegram_api::auth_sendCode>(result->ok());
  if (r_sent_code.is_error()) {
    return on_query_error(r_sent_code.move_as_error());
  }
  auto sent_code = r_sent_code.move_as_ok();

  LOG(INFO) << "Receive " << to_string(sent_code);

  terms_of_service_ = TermsOfService(std::move(sent_code->terms_of_service_));

  send_code_helper_.on_sent_code(std::move(sent_code));

  update_state(State::WaitCode, true);
  on_query_ok();
}

void AuthManager::on_get_password_result(NetQueryPtr &result) {
  auto r_password = fetch_result<telegram_api::account_getPassword>(result->ok());
  if (r_password.is_error()) {
    return on_query_error(r_password.move_as_error());
  }
  auto password = r_password.move_as_ok();
  LOG(INFO) << "Receive password info: " << to_string(password);

  wait_password_state_ = WaitPasswordState();
  if (password->current_algo_ != nullptr) {
    switch (password->current_algo_->get_id()) {
      case telegram_api::passwordKdfAlgoUnknown::ID:
        return on_query_error(Status::Error(400, "Application update is needed to log in"));
      case telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow::ID: {
        auto algo = move_tl_object_as<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
            password->current_algo_);
        wait_password_state_.current_client_salt_ = algo->salt1_.as_slice().str();
        wait_password_state_.current_server_salt_ = algo->salt2_.as_slice().str();
        wait_password_state_.srp_g_ = algo->g_;
        wait_password_state_.srp_p_ = algo->p_.as_slice().str();
        wait_password_state_.srp_B_ = password->srp_B_.as_slice().str();
        wait_password_state_.srp_id_ = password->srp_id_;
        wait_password_state_.hint_ = std::move(password->hint_);
        wait_password_state_.has_recovery_ =
            (password->flags_ & telegram_api::account_password::HAS_RECOVERY_MASK) != 0;
        break;
      }
      default:
        UNREACHABLE();
    }
  } else {
    start_net_query(NetQueryType::SignIn,
                    G()->net_query_creator().create(
                        create_storer(telegram_api::auth_signIn(send_code_helper_.phone_number().str(),
                                                                send_code_helper_.phone_code_hash().str(), code_)),
                        DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
    return;
  }

  if (state_ == State::WaitPassword) {
    LOG(INFO) << "Have SRP id " << wait_password_state_.srp_id_;
    auto hash = PasswordManager::get_input_check_password(password_, wait_password_state_.current_client_salt_,
                                                          wait_password_state_.current_server_salt_,
                                                          wait_password_state_.srp_g_, wait_password_state_.srp_p_,
                                                          wait_password_state_.srp_B_, wait_password_state_.srp_id_);

    start_net_query(NetQueryType::CheckPassword,
                    G()->net_query_creator().create(create_storer(telegram_api::auth_checkPassword(std::move(hash))),
                                                    DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
  } else {
    update_state(State::WaitPassword);
    on_query_ok();
  }
}

void AuthManager::on_request_password_recovery_result(NetQueryPtr &result) {
  auto r_email_address_pattern = fetch_result<telegram_api::auth_requestPasswordRecovery>(result->ok());
  if (r_email_address_pattern.is_error()) {
    return on_query_error(r_email_address_pattern.move_as_error());
  }
  auto email_address_pattern = r_email_address_pattern.move_as_ok();
  CHECK(email_address_pattern->get_id() == telegram_api::auth_passwordRecovery::ID);
  wait_password_state_.email_address_pattern_ = std::move(email_address_pattern->email_pattern_);
  update_state(State::WaitPassword, true);
  on_query_ok();
}

void AuthManager::on_authentication_result(NetQueryPtr &result, bool expected_flag) {
  auto r_sign_in = fetch_result<telegram_api::auth_signIn>(result->ok());
  if (r_sign_in.is_error()) {
    if (expected_flag && query_id_ != 0) {
      return on_query_error(r_sign_in.move_as_error());
    }
    return;
  }
  auto sign_in = r_sign_in.move_as_ok();
  CHECK(sign_in->get_id() == telegram_api::auth_authorization::ID);
  on_authorization(std::move(sign_in));
}

void AuthManager::on_log_out_result(NetQueryPtr &result) {
  Status status;
  if (result->is_ok()) {
    auto r_log_out = fetch_result<telegram_api::auth_logOut>(result->ok());
    if (r_log_out.is_ok()) {
      if (!r_log_out.ok()) {
        status = Status::Error(500, "auth.logOut returned false!");
      }
    } else {
      status = r_log_out.move_as_error();
    }
  } else {
    status = std::move(result->error());
  }
  LOG_IF(ERROR, status.is_error()) << "auth.logOut failed: " << status;
  // state_ will stay logout, so no queries will work.
  send_closure_later(G()->td(), &Td::destroy);
  if (query_id_ != 0) {
    on_query_ok();
  }
}

void AuthManager::on_delete_account_result(NetQueryPtr &result) {
  Status status;
  if (result->is_ok()) {
    auto r_delete_account = fetch_result<telegram_api::account_deleteAccount>(result->ok());
    if (r_delete_account.is_ok()) {
      if (!r_delete_account.ok()) {
        // status = Status::Error(500, "Receive false as result of the request");
      }
    } else {
      status = r_delete_account.move_as_error();
    }
  } else {
    status = std::move(result->error());
  }
  if (status.is_error() && status.error().message() != "USER_DEACTIVATED") {
    LOG(WARNING) << "account.deleteAccount failed: " << status;
    // TODO handle some errors
    if (query_id_ != 0) {
      on_query_error(std::move(status));
    }
  } else {
    update_state(State::LoggingOut);
    send_closure_later(G()->td(), &Td::destroy);
    if (query_id_ != 0) {
      on_query_ok();
    }
  }
}

void AuthManager::on_authorization(tl_object_ptr<telegram_api::auth_authorization> auth) {
  G()->shared_config().set_option_integer("authorization_date", G()->unix_time());
  if (was_check_bot_token_) {
    is_bot_ = true;
    G()->td_db()->get_binlog_pmc()->set("auth_is_bot", "true");
  }
  G()->td_db()->get_binlog_pmc()->set("auth", "ok");
  code_.clear();
  password_.clear();
  state_ = State::Ok;
  td->contacts_manager_->on_get_user(std::move(auth->user_), true);
  update_state(State::Ok, true);
  if (!td->contacts_manager_->get_my_id("on_authorization").is_valid()) {
    LOG(ERROR) << "Server doesn't send proper authorization";
    if (query_id_ != 0) {
      on_query_error(Status::Error(500, "Server doesn't send proper authorization"));
    }
    logout(0);
    return;
  }
  if ((auth->flags_ & telegram_api::auth_authorization::TMP_SESSIONS_MASK) != 0) {
    G()->shared_config().set_option_integer("session_count", auth->tmp_sessions_);
  }
  td->updates_manager_->get_difference("on_authorization");
  td->on_online_updated(true, true);
  td->schedule_get_terms_of_service(0);
  if (!is_bot()) {
    G()->td_db()->get_binlog_pmc()->set("fetched_marks_as_unread", "1");
  }
  send_closure(G()->config_manager(), &ConfigManager::request_config);
  if (query_id_ != 0) {
    on_query_ok();
  }
}

void AuthManager::on_result(NetQueryPtr result) {
  SCOPE_EXIT {
    result->clear();
  };
  NetQueryType type = NetQueryType::None;
  if (result->id() == net_query_id_) {
    net_query_id_ = 0;
    type = net_query_type_;
    net_query_type_ = NetQueryType::None;
    if (result->is_error()) {
      if (type == NetQueryType::SignIn && result->error().code() == 401 &&
          result->error().message() == CSlice("SESSION_PASSWORD_NEEDED")) {
        start_net_query(NetQueryType::GetPassword,
                        G()->net_query_creator().create(create_storer(telegram_api::account_getPassword()),
                                                        DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::Off));
        return;
      }
      if (type != NetQueryType::LogOut) {
        if (query_id_ != 0) {
          if (state_ == State::WaitPhoneNumber) {
            send_code_helper_ = SendCodeHelper();
            terms_of_service_ = TermsOfService();
          }
          on_query_error(std::move(result->error()));
        }
        return;
      }
    }
  } else if (result->is_ok() && result->ok_tl_constructor() == telegram_api::auth_authorization::ID) {
    type = NetQueryType::Authentication;
  }
  switch (type) {
    case NetQueryType::None:
      result->ignore();
      break;
    case NetQueryType::SignIn:
    case NetQueryType::SignUp:
    case NetQueryType::BotAuthentication:
    case NetQueryType::CheckPassword:
    case NetQueryType::RecoverPassword:
      on_authentication_result(result, true);
      break;
    case NetQueryType::Authentication:
      on_authentication_result(result, false);
      break;
    case NetQueryType::SendCode:
      on_send_code_result(result);
      break;
    case NetQueryType::GetPassword:
      on_get_password_result(result);
      break;
    case NetQueryType::RequestPasswordRecovery:
      on_request_password_recovery_result(result);
      break;
    case NetQueryType::LogOut:
      on_log_out_result(result);
      break;
    case NetQueryType::DeleteAccount:
      on_delete_account_result(result);
      break;
  }
}

void AuthManager::update_state(State new_state, bool force, bool should_save_state) {
  if (state_ == new_state && !force) {
    return;
  }
  state_ = new_state;
  if (should_save_state) {
    save_state();
  }
  send_closure(G()->td(), &Td::send_update,
               make_tl_object<td_api::updateAuthorizationState>(get_authorization_state_object(state_)));

  if (!pending_get_authorization_state_requests_.empty()) {
    auto query_ids = std::move(pending_get_authorization_state_requests_);
    for (auto query_id : query_ids) {
      send_closure(G()->td(), &Td::send_result, query_id, get_authorization_state_object(state_));
    }
  }
}

bool AuthManager::load_state() {
  auto data = G()->td_db()->get_binlog_pmc()->get("auth_state");
  if (data.empty()) {
    return false;
  }
  DbState db_state;
  auto status = log_event_parse(db_state, data);
  if (status.is_error()) {
    LOG(INFO) << "Ignore auth_state: " << status;
    return false;
  }
  if (db_state.api_id_ != api_id_ || db_state.api_hash_ != api_hash_) {
    LOG(INFO) << "Ignore auth_state: api_id or api_hash changed";
    return false;
  }
  if (!db_state.state_timestamp_.is_in_past()) {
    LOG(INFO) << "Ignore auth_state: timestamp in future";
    return false;
  }
  if (Timestamp::at(db_state.state_timestamp_.at() + 5 * 60).is_in_past()) {
    LOG(INFO) << "Ignore auth_state: expired " << db_state.state_timestamp_.in();
    return false;
  }

  LOG(INFO) << "Load auth_state from db: " << tag("state", static_cast<int32>(db_state.state_));
  if (db_state.state_ == State::WaitCode) {
    send_code_helper_ = std::move(db_state.send_code_helper_);
    terms_of_service_ = std::move(db_state.terms_of_service_);
  } else if (db_state.state_ == State::WaitPassword) {
    wait_password_state_ = std::move(db_state.wait_password_state_);
  } else {
    UNREACHABLE();
  }
  update_state(db_state.state_, false, false);
  return true;
}

void AuthManager::save_state() {
  if (state_ != State::WaitCode && state_ != State::WaitPassword) {
    if (state_ != State::Closing) {
      G()->td_db()->get_binlog_pmc()->erase("auth_state");
    }
    return;
  }

  DbState db_state;
  if (state_ == State::WaitCode) {
    db_state = DbState::wait_code(api_id_, api_hash_, send_code_helper_, terms_of_service_);
  } else if (state_ == State::WaitPassword) {
    db_state = DbState::wait_password(api_id_, api_hash_, wait_password_state_);
  } else {
    UNREACHABLE();
  }
  G()->td_db()->get_binlog_pmc()->set("auth_state", log_event_store(db_state).as_slice().str());
}

}  // namespace td
