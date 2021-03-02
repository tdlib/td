//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AuthManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/AuthManager.hpp"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/UpdatesManager.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/base64.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

namespace td {

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
          td, PromiseCreator::lambda([this](Result<Unit> result) { update_state(State::Ok); }));
    }
  } else if (auth_str == "logout") {
    update_state(State::LoggingOut);
  } else if (auth_str == "destroy") {
    update_state(State::DestroyingKeys);
  } else {
    if (!load_state()) {
      update_state(State::WaitPhoneNumber);
    }
  }
}

void AuthManager::start_up() {
  if (state_ == State::LoggingOut) {
    send_log_out_query();
  } else if (state_ == State::DestroyingKeys) {
    destroy_auth_keys();
  }
}
void AuthManager::tear_down() {
  parent_.reset();
}

bool AuthManager::is_bot() const {
  if (net_query_id_ != 0 && net_query_type_ == NetQueryType::BotAuthentication) {
    return true;
  }
  return is_bot_ && was_authorized();
}

bool AuthManager::was_authorized() const {
  return state_ == State::Ok || state_ == State::LoggingOut || state_ == State::DestroyingKeys ||
         state_ == State::Closing;
}

bool AuthManager::is_authorized() const {
  return state_ == State::Ok;
}

tl_object_ptr<td_api::AuthorizationState> AuthManager::get_authorization_state_object(State authorization_state) const {
  switch (authorization_state) {
    case State::WaitPhoneNumber:
      return make_tl_object<td_api::authorizationStateWaitPhoneNumber>();
    case State::WaitCode:
      return send_code_helper_.get_authorization_state_wait_code();
    case State::WaitQrCodeConfirmation:
      return make_tl_object<td_api::authorizationStateWaitOtherDeviceConfirmation>("tg://login?token=" +
                                                                                   base64url_encode(login_token_));
    case State::WaitPassword:
      return make_tl_object<td_api::authorizationStateWaitPassword>(
          wait_password_state_.hint_, wait_password_state_.has_recovery_, wait_password_state_.email_address_pattern_);
    case State::WaitRegistration:
      return make_tl_object<td_api::authorizationStateWaitRegistration>(
          terms_of_service_.get_terms_of_service_object());
    case State::Ok:
      return make_tl_object<td_api::authorizationStateReady>();
    case State::LoggingOut:
    case State::DestroyingKeys:
      return make_tl_object<td_api::authorizationStateLoggingOut>();
    case State::Closing:
      return make_tl_object<td_api::authorizationStateClosing>();
    case State::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<td_api::AuthorizationState> AuthManager::get_current_authorization_state_object() const {
  if (state_ == State::None) {
    return nullptr;
  } else {
    return get_authorization_state_object(state_);
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
  if (state_ == State::WaitPhoneNumber && net_query_id_ == 0) {
    // can ignore previous checks
    was_check_bot_token_ = false;  // TODO can we remove was_check_bot_token_?
  }
  if (state_ != State::WaitPhoneNumber) {
    return on_query_error(query_id, Status::Error(400, "Call to checkAuthenticationBotToken unexpected"));
  }
  if (!send_code_helper_.phone_number().empty() || was_qr_code_request_) {
    return on_query_error(
        query_id, Status::Error(400, "Cannot set bot token after authentication began. You need to log out first"));
  }
  if (was_check_bot_token_ && bot_token_ != bot_token) {
    return on_query_error(query_id, Status::Error(8, "Cannot change bot token. You need to log out first"));
  }

  on_new_query(query_id);
  bot_token_ = bot_token;
  was_check_bot_token_ = true;
  start_net_query(NetQueryType::BotAuthentication,
                  G()->net_query_creator().create_unauth(
                      telegram_api::auth_importBotAuthorization(0, api_id_, api_hash_, bot_token_)));
}

void AuthManager::request_qr_code_authentication(uint64 query_id, vector<int32> other_user_ids) {
  if (state_ != State::WaitPhoneNumber) {
    if ((state_ == State::WaitCode || state_ == State::WaitPassword || state_ == State::WaitRegistration) &&
        net_query_id_ == 0) {
      // ok
    } else {
      return on_query_error(query_id, Status::Error(8, "Call to requestQrCodeAuthentication unexpected"));
    }
  }
  if (was_check_bot_token_) {
    return on_query_error(
        query_id,
        Status::Error(8,
                      "Cannot request QR code authentication after bot token was entered. You need to log out first"));
  }
  for (auto &other_user_id : other_user_ids) {
    UserId user_id(other_user_id);
    if (!user_id.is_valid()) {
      return on_query_error(query_id, Status::Error(400, "Invalid user_id among other user_ids"));
    }
  }

  other_user_ids_ = std::move(other_user_ids);
  send_code_helper_ = SendCodeHelper();
  terms_of_service_ = TermsOfService();
  was_qr_code_request_ = true;

  on_new_query(query_id);

  send_export_login_token_query();
}

void AuthManager::send_export_login_token_query() {
  poll_export_login_code_timeout_.cancel_timeout();
  start_net_query(NetQueryType::RequestQrCode,
                  G()->net_query_creator().create_unauth(
                      telegram_api::auth_exportLoginToken(api_id_, api_hash_, vector<int32>(other_user_ids_))));
}

void AuthManager::set_login_token_expires_at(double login_token_expires_at) {
  login_token_expires_at_ = login_token_expires_at;
  poll_export_login_code_timeout_.cancel_timeout();
  poll_export_login_code_timeout_.set_callback(std::move(on_update_login_token_static));
  poll_export_login_code_timeout_.set_callback_data(static_cast<void *>(td));
  poll_export_login_code_timeout_.set_timeout_at(login_token_expires_at_);
}

void AuthManager::on_update_login_token_static(void *td) {
  static_cast<Td *>(td)->auth_manager_->on_update_login_token();
}

void AuthManager::on_update_login_token() {
  if (G()->close_flag()) {
    return;
  }
  if (state_ != State::WaitQrCodeConfirmation) {
    return;
  }

  send_export_login_token_query();
}

void AuthManager::set_phone_number(uint64 query_id, string phone_number,
                                   td_api::object_ptr<td_api::phoneNumberAuthenticationSettings> settings) {
  if (state_ != State::WaitPhoneNumber) {
    if ((state_ == State::WaitCode || state_ == State::WaitPassword || state_ == State::WaitRegistration) &&
        net_query_id_ == 0) {
      // ok
    } else {
      return on_query_error(query_id, Status::Error(8, "Call to setAuthenticationPhoneNumber unexpected"));
    }
  }
  if (was_check_bot_token_) {
    return on_query_error(
        query_id, Status::Error(8, "Cannot set phone number after bot token was entered. You need to log out first"));
  }
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }

  other_user_ids_.clear();
  was_qr_code_request_ = false;

  if (send_code_helper_.phone_number() != phone_number) {
    send_code_helper_ = SendCodeHelper();
    terms_of_service_ = TermsOfService();
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create_unauth(
                                              send_code_helper_.send_code(phone_number, settings, api_id_, api_hash_)));
}

void AuthManager::resend_authentication_code(uint64 query_id) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(8, "Call to resendAuthenticationCode unexpected"));
  }

  auto r_resend_code = send_code_helper_.resend_code();
  if (r_resend_code.is_error()) {
    return on_query_error(query_id, r_resend_code.move_as_error());
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create_unauth(r_resend_code.move_as_ok()));
}

void AuthManager::check_code(uint64 query_id, string code) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(8, "Call to checkAuthenticationCode unexpected"));
  }

  code_ = std::move(code);
  on_new_query(query_id);
  start_net_query(NetQueryType::SignIn,
                  G()->net_query_creator().create_unauth(telegram_api::auth_signIn(
                      send_code_helper_.phone_number().str(), send_code_helper_.phone_code_hash().str(), code_)));
}

void AuthManager::register_user(uint64 query_id, string first_name, string last_name) {
  if (state_ != State::WaitRegistration) {
    return on_query_error(query_id, Status::Error(8, "Call to registerUser unexpected"));
  }

  on_new_query(query_id);
  first_name = clean_name(first_name, MAX_NAME_LENGTH);
  if (first_name.empty()) {
    return on_query_error(Status::Error(8, "First name can't be empty"));
  }

  last_name = clean_name(last_name, MAX_NAME_LENGTH);
  start_net_query(NetQueryType::SignUp, G()->net_query_creator().create_unauth(telegram_api::auth_signUp(
                                            send_code_helper_.phone_number().str(),
                                            send_code_helper_.phone_code_hash().str(), first_name, last_name)));
}

void AuthManager::check_password(uint64 query_id, string password) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "Call to checkAuthenticationPassword unexpected"));
  }

  LOG(INFO) << "Have SRP id " << wait_password_state_.srp_id_;
  on_new_query(query_id);
  password_ = std::move(password);
  start_net_query(NetQueryType::GetPassword,
                  G()->net_query_creator().create_unauth(telegram_api::account_getPassword()));
}

void AuthManager::request_password_recovery(uint64 query_id) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "Call to requestAuthenticationPasswordRecovery unexpected"));
  }

  on_new_query(query_id);
  start_net_query(NetQueryType::RequestPasswordRecovery,
                  G()->net_query_creator().create_unauth(telegram_api::auth_requestPasswordRecovery()));
}

void AuthManager::recover_password(uint64 query_id, string code) {
  if (state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "Call to recoverAuthenticationPassword unexpected"));
  }

  on_new_query(query_id);
  start_net_query(NetQueryType::RecoverPassword,
                  G()->net_query_creator().create_unauth(telegram_api::auth_recoverPassword(code)));
}

void AuthManager::log_out(uint64 query_id) {
  if (state_ == State::Closing) {
    return on_query_error(query_id, Status::Error(8, "Already logged out"));
  }
  if (state_ == State::LoggingOut || state_ == State::DestroyingKeys) {
    return on_query_error(query_id, Status::Error(8, "Already logging out"));
  }
  on_new_query(query_id);
  if (state_ != State::Ok) {
    // TODO: could skip full logout if still no authorization
    // TODO: send auth.cancelCode if state_ == State::WaitCode
    destroy_auth_keys();
    on_query_ok();
  } else {
    LOG(INFO) << "Logging out";
    G()->td_db()->get_binlog_pmc()->set("auth", "logout");
    update_state(State::LoggingOut);
    send_log_out_query();
  }
}

void AuthManager::send_log_out_query() {
  auto query = G()->net_query_creator().create(telegram_api::auth_logOut());
  query->set_priority(1);
  start_net_query(NetQueryType::LogOut, std::move(query));
}

void AuthManager::delete_account(uint64 query_id, const string &reason) {
  if (state_ != State::Ok && state_ != State::WaitPassword) {
    return on_query_error(query_id, Status::Error(8, "Need to log in first"));
  }
  on_new_query(query_id);
  LOG(INFO) << "Deleting account";
  start_net_query(NetQueryType::DeleteAccount,
                  G()->net_query_creator().create_unauth(telegram_api::account_deleteAccount(reason)));
}

void AuthManager::on_closing(bool destroy_flag) {
  if (destroy_flag) {
    update_state(State::LoggingOut);
  } else {
    update_state(State::Closing);
  }
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

  send_code_helper_.on_sent_code(std::move(sent_code));

  update_state(State::WaitCode, true);
  on_query_ok();
}

void AuthManager::on_request_qr_code_result(NetQueryPtr &result, bool is_import) {
  Status status;
  if (result->is_ok()) {
    auto r_login_token = fetch_result<telegram_api::auth_exportLoginToken>(result->ok());
    if (r_login_token.is_ok()) {
      auto login_token = r_login_token.move_as_ok();

      if (is_import) {
        CHECK(DcId::is_valid(imported_dc_id_));
        G()->net_query_dispatcher().set_main_dc_id(imported_dc_id_);
        imported_dc_id_ = -1;
      }

      on_get_login_token(std::move(login_token));
      return;
    }

    status = r_login_token.move_as_error();
  } else {
    status = std::move(result->error());
  }
  CHECK(status.is_error());

  LOG(INFO) << "Receive " << status << " for login token " << (is_import ? "import" : "export");
  if (is_import) {
    imported_dc_id_ = -1;
  }
  if (query_id_ != 0) {
    on_query_error(std::move(status));
  } else {
    login_code_retry_delay_ = clamp(2 * login_code_retry_delay_, 1, 60);
    set_login_token_expires_at(Time::now() + login_code_retry_delay_);
  }
}

void AuthManager::on_get_login_token(tl_object_ptr<telegram_api::auth_LoginToken> login_token) {
  LOG(INFO) << "Receive " << to_string(login_token);

  login_code_retry_delay_ = 0;

  CHECK(login_token != nullptr);
  switch (login_token->get_id()) {
    case telegram_api::auth_loginToken::ID: {
      auto token = move_tl_object_as<telegram_api::auth_loginToken>(login_token);
      login_token_ = token->token_.as_slice().str();
      set_login_token_expires_at(Time::now() + td::max(token->expires_ - G()->server_time(), 1.0));
      update_state(State::WaitQrCodeConfirmation, true);
      if (query_id_ != 0) {
        on_query_ok();
      }
      break;
    }
    case telegram_api::auth_loginTokenMigrateTo::ID: {
      auto token = move_tl_object_as<telegram_api::auth_loginTokenMigrateTo>(login_token);
      if (!DcId::is_valid(token->dc_id_)) {
        LOG(ERROR) << "Receive wrong DC " << token->dc_id_;
        return;
      }
      if (query_id_ != 0) {
        on_query_ok();
      }

      imported_dc_id_ = token->dc_id_;
      start_net_query(NetQueryType::ImportQrCode, G()->net_query_creator().create_unauth(
                                                      telegram_api::auth_importLoginToken(std::move(token->token_)),
                                                      DcId::internal(token->dc_id_)));
      break;
    }
    case telegram_api::auth_loginTokenSuccess::ID: {
      auto token = move_tl_object_as<telegram_api::auth_loginTokenSuccess>(login_token);
      on_get_authorization(std::move(token->authorization_));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void AuthManager::on_get_password_result(NetQueryPtr &result) {
  Result<telegram_api::object_ptr<telegram_api::account_password>> r_password;
  if (result->is_error()) {
    r_password = std::move(result->error());
  } else {
    r_password = fetch_result<telegram_api::account_getPassword>(result->ok());
  }
  if (r_password.is_error() && query_id_ != 0) {
    return on_query_error(r_password.move_as_error());
  }
  auto password = r_password.is_ok() ? r_password.move_as_ok() : nullptr;
  LOG(INFO) << "Receive password info: " << to_string(password);

  wait_password_state_ = WaitPasswordState();
  if (password != nullptr && password->current_algo_ != nullptr) {
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
  } else if (was_qr_code_request_) {
    imported_dc_id_ = -1;
    login_code_retry_delay_ = clamp(2 * login_code_retry_delay_, 1, 60);
    set_login_token_expires_at(Time::now() + login_code_retry_delay_);
    return;
  } else {
    start_net_query(NetQueryType::SignIn,
                    G()->net_query_creator().create_unauth(telegram_api::auth_signIn(
                        send_code_helper_.phone_number().str(), send_code_helper_.phone_code_hash().str(), code_)));
    return;
  }

  if (imported_dc_id_ != -1) {
    G()->net_query_dispatcher().set_main_dc_id(imported_dc_id_);
    imported_dc_id_ = -1;
  }

  if (state_ == State::WaitPassword) {
    LOG(INFO) << "Have SRP id " << wait_password_state_.srp_id_;
    auto hash = PasswordManager::get_input_check_password(password_, wait_password_state_.current_client_salt_,
                                                          wait_password_state_.current_server_salt_,
                                                          wait_password_state_.srp_g_, wait_password_state_.srp_p_,
                                                          wait_password_state_.srp_B_, wait_password_state_.srp_id_);

    start_net_query(NetQueryType::CheckPassword,
                    G()->net_query_creator().create_unauth(telegram_api::auth_checkPassword(std::move(hash))));
  } else {
    update_state(State::WaitPassword);
    if (query_id_ != 0) {
      on_query_ok();
    }
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

void AuthManager::on_authentication_result(NetQueryPtr &result, bool is_from_current_query) {
  auto r_sign_in = fetch_result<telegram_api::auth_signIn>(result->ok());
  if (r_sign_in.is_error()) {
    if (is_from_current_query && query_id_ != 0) {
      return on_query_error(r_sign_in.move_as_error());
    }
    return;
  }
  on_get_authorization(r_sign_in.move_as_ok());
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
  LOG_IF(ERROR, status.is_error()) << "Receive error for auth.logOut: " << status;
  // state_ will stay LoggingOut, so no queries will work.
  destroy_auth_keys();
  if (query_id_ != 0) {
    on_query_ok();
  }
}
void AuthManager::on_authorization_lost() {
  destroy_auth_keys();
}

void AuthManager::destroy_auth_keys() {
  if (state_ == State::Closing) {
    return;
  }
  update_state(State::DestroyingKeys);
  auto promise = PromiseCreator::lambda(
      [](Unit) {
        G()->net_query_dispatcher().destroy_auth_keys(PromiseCreator::lambda(
            [](Unit) { send_closure_later(G()->td(), &Td::destroy); }, PromiseCreator::Ignore()));
      },
      PromiseCreator::Ignore());
  G()->td_db()->get_binlog_pmc()->set("auth", "destroy");
  G()->td_db()->get_binlog_pmc()->force_sync(std::move(promise));
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
    LOG(WARNING) << "Request account.deleteAccount failed: " << status;
    // TODO handle some errors
    if (query_id_ != 0) {
      on_query_error(std::move(status));
    }
  } else {
    destroy_auth_keys();
    if (query_id_ != 0) {
      on_query_ok();
    }
  }
}

void AuthManager::on_get_authorization(tl_object_ptr<telegram_api::auth_Authorization> auth_ptr) {
  if (state_ == State::Ok) {
    LOG(WARNING) << "Ignore duplicated auth.Authorization";
    if (query_id_ != 0) {
      on_query_ok();
    }
    return;
  }
  CHECK(auth_ptr != nullptr);
  if (auth_ptr->get_id() == telegram_api::auth_authorizationSignUpRequired::ID) {
    auto sign_up_required = telegram_api::move_object_as<telegram_api::auth_authorizationSignUpRequired>(auth_ptr);
    terms_of_service_ = TermsOfService(std::move(sign_up_required->terms_of_service_));
    update_state(State::WaitRegistration);
    if (query_id_ != 0) {
      on_query_ok();
    }
    return;
  }
  auto auth = telegram_api::move_object_as<telegram_api::auth_authorization>(auth_ptr);

  G()->shared_config().set_option_integer("authorization_date", G()->unix_time());
  if (was_check_bot_token_) {
    is_bot_ = true;
    G()->td_db()->get_binlog_pmc()->set("auth_is_bot", "true");
  }
  G()->td_db()->get_binlog_pmc()->set("auth", "ok");
  code_.clear();
  password_.clear();
  state_ = State::Ok;
  td->contacts_manager_->on_get_user(std::move(auth->user_), "on_get_authorization", true);
  update_state(State::Ok, true);
  if (!td->contacts_manager_->get_my_id().is_valid()) {
    LOG(ERROR) << "Server doesn't send proper authorization";
    if (query_id_ != 0) {
      on_query_error(Status::Error(500, "Server doesn't send proper authorization"));
    }
    log_out(0);
    return;
  }
  if ((auth->flags_ & telegram_api::auth_authorization::TMP_SESSIONS_MASK) != 0) {
    G()->shared_config().set_option_integer("session_count", auth->tmp_sessions_);
  }
  td->messages_manager_->on_authorization_success();
  td->notification_manager_->init();
  td->stickers_manager_->init();
  send_closure(td->top_dialog_manager_, &TopDialogManager::do_start_up);
  td->updates_manager_->get_difference("on_get_authorization");
  td->on_online_updated(false, true);
  if (!is_bot()) {
    td->schedule_get_terms_of_service(0);
    td->schedule_get_promo_data(0);
    G()->td_db()->get_binlog_pmc()->set("fetched_marks_as_unread", "1");
  } else {
    td->set_is_bot_online(true);
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
  LOG(INFO) << "Receive result of query " << result->id() << ", expecting " << net_query_id_ << " with type "
            << static_cast<int32>(net_query_type_);
  if (result->id() == net_query_id_) {
    net_query_id_ = 0;
    type = net_query_type_;
    net_query_type_ = NetQueryType::None;
    if (result->is_error()) {
      if ((type == NetQueryType::SignIn || type == NetQueryType::RequestQrCode || type == NetQueryType::ImportQrCode) &&
          result->error().code() == 401 && result->error().message() == CSlice("SESSION_PASSWORD_NEEDED")) {
        auto dc_id = DcId::main();
        if (type == NetQueryType::ImportQrCode) {
          CHECK(DcId::is_valid(imported_dc_id_));
          dc_id = DcId::internal(imported_dc_id_);
        }
        start_net_query(NetQueryType::GetPassword,
                        G()->net_query_creator().create_unauth(telegram_api::account_getPassword(), dc_id));
        return;
      }
      if (result->error().message() == CSlice("PHONE_NUMBER_BANNED")) {
        LOG(PLAIN)
            << "Your phone number was banned for suspicious activity. If you think that this is a mistake, please "
               "write to recover@telegram.org your phone number and other details to recover the account.";
      }
      if (type != NetQueryType::LogOut && type != NetQueryType::DeleteAccount) {
        if (query_id_ != 0) {
          if (state_ == State::WaitPhoneNumber) {
            other_user_ids_.clear();
            send_code_helper_ = SendCodeHelper();
            terms_of_service_ = TermsOfService();
            was_qr_code_request_ = false;
            was_check_bot_token_ = false;
          }
          on_query_error(std::move(result->error()));
          return;
        }
        if (type != NetQueryType::RequestQrCode && type != NetQueryType::ImportQrCode &&
            type != NetQueryType::GetPassword) {
          LOG(INFO) << "Ignore error for net query of type " << static_cast<int32>(net_query_type_);
          return;
        }
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
    case NetQueryType::RequestQrCode:
      on_request_qr_code_result(result, false);
      break;
    case NetQueryType::ImportQrCode:
      on_request_qr_code_result(result, true);
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
  bool skip_update = (state_ == State::LoggingOut || state_ == State::DestroyingKeys) &&
                     (new_state == State::LoggingOut || new_state == State::DestroyingKeys);
  state_ = new_state;
  if (should_save_state) {
    save_state();
  }
  if (new_state == State::LoggingOut || new_state == State::DestroyingKeys) {
    send_closure(G()->state_manager(), &StateManager::on_logging_out, true);
  }
  if (!skip_update) {
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateAuthorizationState>(get_authorization_state_object(state_)));
  }

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
    LOG(INFO) << "Ignore auth_state: timestamp in the future";
    return false;
  }
  auto state_timeout = [state = db_state.state_] {
    switch (state) {
      case State::WaitPassword:
      case State::WaitRegistration:
        return 86400;
      case State::WaitCode:
      case State::WaitQrCodeConfirmation:
        return 5 * 60;
      default:
        UNREACHABLE();
        return 0;
    }
  }();

  if (Timestamp::at(db_state.state_timestamp_.at() + state_timeout).is_in_past()) {
    LOG(INFO) << "Ignore auth_state: expired " << db_state.state_timestamp_.in();
    return false;
  }

  LOG(INFO) << "Load auth_state from database: " << tag("state", static_cast<int32>(db_state.state_));
  if (db_state.state_ == State::WaitCode) {
    send_code_helper_ = std::move(db_state.send_code_helper_);
  } else if (db_state.state_ == State::WaitQrCodeConfirmation) {
    other_user_ids_ = std::move(db_state.other_user_ids_);
    login_token_ = std::move(db_state.login_token_);
    set_login_token_expires_at(db_state.login_token_expires_at_);
  } else if (db_state.state_ == State::WaitPassword) {
    wait_password_state_ = std::move(db_state.wait_password_state_);
  } else if (db_state.state_ == State::WaitRegistration) {
    send_code_helper_ = std::move(db_state.send_code_helper_);
    terms_of_service_ = std::move(db_state.terms_of_service_);
  } else {
    UNREACHABLE();
  }
  update_state(db_state.state_, false, false);
  return true;
}

void AuthManager::save_state() {
  if (state_ != State::WaitCode && state_ != State::WaitQrCodeConfirmation && state_ != State::WaitPassword &&
      state_ != State::WaitRegistration) {
    if (state_ != State::Closing) {
      G()->td_db()->get_binlog_pmc()->erase("auth_state");
    }
    return;
  }

  DbState db_state = [&] {
    if (state_ == State::WaitCode) {
      return DbState::wait_code(api_id_, api_hash_, send_code_helper_);
    } else if (state_ == State::WaitQrCodeConfirmation) {
      return DbState::wait_qr_code_confirmation(api_id_, api_hash_, other_user_ids_, login_token_,
                                                login_token_expires_at_);
    } else if (state_ == State::WaitPassword) {
      return DbState::wait_password(api_id_, api_hash_, wait_password_state_);
    } else {
      CHECK(state_ == State::WaitRegistration);
      return DbState::wait_registration(api_id_, api_hash_, send_code_helper_, terms_of_service_);
    }
  }();
  G()->td_db()->get_binlog_pmc()->set("auth_state", log_event_store(db_state).as_slice().str());
}

}  // namespace td
