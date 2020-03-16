//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PhoneNumberManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"

namespace td {

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
void PhoneNumberManager::process_send_code_result(uint64 query_id, const T &send_code) {
  on_new_query(query_id);
  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create(send_code));
}

void PhoneNumberManager::set_phone_number(uint64 query_id, string phone_number, Settings settings) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }

  switch (type_) {
    case Type::ChangePhone:
      return process_send_code_result(query_id, send_code_helper_.send_change_phone_code(phone_number, settings));
    case Type::VerifyPhone:
      return process_send_code_result(query_id, send_code_helper_.send_verify_phone_code(phone_number, settings));
    case Type::ConfirmPhone:
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::set_phone_number_and_hash(uint64 query_id, string hash, string phone_number,
                                                   Settings settings) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(8, "Phone number can't be empty"));
  }
  if (hash.empty()) {
    return on_query_error(query_id, Status::Error(8, "Hash can't be empty"));
  }

  switch (type_) {
    case Type::ConfirmPhone:
      return process_send_code_result(query_id,
                                      send_code_helper_.send_confirm_phone_code(hash, phone_number, settings));
    case Type::ChangePhone:
    case Type::VerifyPhone:
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

  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create_unauth(r_resend_code.move_as_ok()));
}

template <class T>
void PhoneNumberManager::send_new_check_code_query(const T &query) {
  start_net_query(NetQueryType::CheckCode, G()->net_query_creator().create(query));
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

}  // namespace td
