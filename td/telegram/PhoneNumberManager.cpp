//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PhoneNumberManager.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"

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

void PhoneNumberManager::send_new_send_code_query(uint64 query_id, const telegram_api::Function &send_code) {
  on_new_query(query_id);
  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create(send_code));
}

void PhoneNumberManager::set_phone_number(uint64 query_id, string phone_number, Settings settings) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(400, "Phone number must be non-empty"));
  }

  switch (type_) {
    case Type::ChangePhone:
      send_closure(G()->config_manager(), &ConfigManager::hide_suggested_action,
                   SuggestedAction{SuggestedAction::Type::CheckPhoneNumber});
      return send_new_send_code_query(query_id, send_code_helper_.send_change_phone_code(phone_number, settings));
    case Type::VerifyPhone:
      return send_new_send_code_query(query_id, send_code_helper_.send_verify_phone_code(phone_number, settings));
    case Type::ConfirmPhone:
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::set_phone_number_and_hash(uint64 query_id, string hash, string phone_number,
                                                   Settings settings) {
  if (phone_number.empty()) {
    return on_query_error(query_id, Status::Error(400, "Phone number must be non-empty"));
  }
  if (hash.empty()) {
    return on_query_error(query_id, Status::Error(400, "Hash must be non-empty"));
  }

  switch (type_) {
    case Type::ConfirmPhone:
      return send_new_send_code_query(query_id,
                                      send_code_helper_.send_confirm_phone_code(hash, phone_number, settings));
    case Type::ChangePhone:
    case Type::VerifyPhone:
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::resend_authentication_code(uint64 query_id) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(400, "Can't resend code"));
  }

  auto r_resend_code = send_code_helper_.resend_code();
  if (r_resend_code.is_error()) {
    return on_query_error(query_id, r_resend_code.move_as_error());
  }

  on_new_query(query_id);

  start_net_query(NetQueryType::SendCode, G()->net_query_creator().create_unauth(r_resend_code.move_as_ok()));
}

void PhoneNumberManager::send_new_check_code_query(const telegram_api::Function &check_code) {
  start_net_query(NetQueryType::CheckCode, G()->net_query_creator().create(check_code));
}

void PhoneNumberManager::check_code(uint64 query_id, string code) {
  if (state_ != State::WaitCode) {
    return on_query_error(query_id, Status::Error(400, "Can't check code"));
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
    on_current_query_error(Status::Error(400, "Another query has started"));
  }
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  query_id_ = query_id;
  // TODO: cancel older net_query
}

void PhoneNumberManager::on_current_query_error(Status status) {
  if (query_id_ == 0) {
    return;
  }
  auto id = query_id_;
  query_id_ = 0;
  net_query_id_ = 0;
  net_query_type_ = NetQueryType::None;
  on_query_error(id, std::move(status));
}

void PhoneNumberManager::on_query_error(uint64 id, Status status) {
  send_closure(G()->td(), &Td::send_error, id, std::move(status));
}

void PhoneNumberManager::on_current_query_ok() {
  if (query_id_ == 0) {
    return;
  }
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

void PhoneNumberManager::process_check_code_result(Result<tl_object_ptr<telegram_api::User>> &&result) {
  if (result.is_error()) {
    return on_current_query_error(result.move_as_error());
  }
  send_closure(G()->contacts_manager(), &ContactsManager::on_get_user, result.move_as_ok(),
               "process_check_code_result");
  state_ = State::Ok;
  on_current_query_ok();
}

void PhoneNumberManager::process_check_code_result(Result<bool> &&result) {
  if (result.is_error()) {
    return on_current_query_error(result.move_as_error());
  }
  state_ = State::Ok;
  on_current_query_ok();
}

void PhoneNumberManager::on_check_code_result(NetQueryPtr &&net_query) {
  switch (type_) {
    case Type::ChangePhone:
      return process_check_code_result(fetch_result<telegram_api::account_changePhone>(std::move(net_query)));
    case Type::VerifyPhone:
      return process_check_code_result(fetch_result<telegram_api::account_verifyPhone>(std::move(net_query)));
    case Type::ConfirmPhone:
      return process_check_code_result(fetch_result<telegram_api::account_confirmPhone>(std::move(net_query)));
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::on_send_code_result(NetQueryPtr &&net_query) {
  auto r_sent_code = [&] {
    switch (type_) {
      case Type::ChangePhone:
        return fetch_result<telegram_api::account_sendChangePhoneCode>(std::move(net_query));
      case Type::VerifyPhone:
        return fetch_result<telegram_api::account_sendVerifyPhoneCode>(std::move(net_query));
      case Type::ConfirmPhone:
        return fetch_result<telegram_api::account_sendConfirmPhoneCode>(std::move(net_query));
      default:
        UNREACHABLE();
        return fetch_result<telegram_api::account_sendChangePhoneCode>(std::move(net_query));
    }
  }();
  if (r_sent_code.is_error()) {
    return on_current_query_error(r_sent_code.move_as_error());
  }
  auto sent_code_ptr = r_sent_code.move_as_ok();
  auto sent_code_id = sent_code_ptr->get_id();
  if (sent_code_id != telegram_api::auth_sentCode::ID) {
    CHECK(sent_code_id == telegram_api::auth_sentCodeSuccess::ID);
    return on_current_query_error(Status::Error(500, "Receive invalid response"));
  }
  auto sent_code = telegram_api::move_object_as<telegram_api::auth_sentCode>(sent_code_ptr);

  LOG(INFO) << "Receive " << to_string(sent_code);

  switch (sent_code->type_->get_id()) {
    case telegram_api::auth_sentCodeTypeSetUpEmailRequired::ID:
    case telegram_api::auth_sentCodeTypeEmailCode::ID:
      return on_current_query_error(Status::Error(500, "Receive incorrect response"));
    default:
      break;
  }

  send_code_helper_.on_sent_code(std::move(sent_code));

  state_ = State::WaitCode;
  on_current_query_ok();
}

void PhoneNumberManager::on_result(NetQueryPtr net_query) {
  NetQueryType type = NetQueryType::None;
  if (net_query->id() == net_query_id_) {
    net_query_id_ = 0;
    type = net_query_type_;
    net_query_type_ = NetQueryType::None;
  }
  switch (type) {
    case NetQueryType::None:
      net_query->clear();
      break;
    case NetQueryType::SendCode:
      on_send_code_result(std::move(net_query));
      break;
    case NetQueryType::CheckCode:
      on_check_code_result(std::move(net_query));
      break;
    default:
      UNREACHABLE();
  }
}

void PhoneNumberManager::tear_down() {
  parent_.reset();
}

}  // namespace td
