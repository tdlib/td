//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SendCodeHelper.h"

namespace td {

void SendCodeHelper::on_sent_code(telegram_api::object_ptr<telegram_api::auth_sentCode> sent_code) {
  phone_code_hash_ = sent_code->phone_code_hash_;
  sent_code_info_ = get_authentication_code_info(std::move(sent_code->type_));
  next_code_info_ = get_authentication_code_info(std::move(sent_code->next_type_));
  next_code_timestamp_ = Timestamp::in((sent_code->flags_ & SENT_CODE_FLAG_HAS_TIMEOUT) != 0 ? sent_code->timeout_ : 0);
}

td_api::object_ptr<td_api::authorizationStateWaitCode> SendCodeHelper::get_authorization_state_wait_code() const {
  return make_tl_object<td_api::authorizationStateWaitCode>(get_authentication_code_info_object());
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

telegram_api::object_ptr<telegram_api::codeSettings> SendCodeHelper::get_input_code_settings(const Settings &settings) {
  int32 flags = 0;
  if (settings != nullptr) {
    if (settings->allow_flash_call_) {
      flags |= telegram_api::codeSettings::ALLOW_FLASHCALL_MASK;
    }
    if (settings->is_current_phone_number_) {
      flags |= telegram_api::codeSettings::CURRENT_NUMBER_MASK;
    }
    if (settings->allow_sms_retriever_api_) {
      flags |= telegram_api::codeSettings::ALLOW_APP_HASH_MASK;
    }
  }
  return telegram_api::make_object<telegram_api::codeSettings>(flags, false /*ignored*/, false /*ignored*/,
                                                               false /*ignored*/);
}

telegram_api::auth_sendCode SendCodeHelper::send_code(Slice phone_number, const Settings &settings, int32 api_id,
                                                      const string &api_hash) {
  phone_number_ = phone_number.str();
  return telegram_api::auth_sendCode(phone_number_, api_id, api_hash, get_input_code_settings(settings));
}

telegram_api::account_sendChangePhoneCode SendCodeHelper::send_change_phone_code(Slice phone_number,
                                                                                 const Settings &settings) {
  phone_number_ = phone_number.str();
  return telegram_api::account_sendChangePhoneCode(phone_number_, get_input_code_settings(settings));
}

telegram_api::account_sendVerifyPhoneCode SendCodeHelper::send_verify_phone_code(Slice phone_number,
                                                                                 const Settings &settings) {
  phone_number_ = phone_number.str();
  return telegram_api::account_sendVerifyPhoneCode(phone_number_, get_input_code_settings(settings));
}

telegram_api::account_sendConfirmPhoneCode SendCodeHelper::send_confirm_phone_code(const string &hash,
                                                                                   Slice phone_number,
                                                                                   const Settings &settings) {
  phone_number_ = phone_number.str();
  return telegram_api::account_sendConfirmPhoneCode(hash, get_input_code_settings(settings));
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

td_api::object_ptr<td_api::AuthenticationCodeType> SendCodeHelper::get_authentication_code_type_object(
    const AuthenticationCodeInfo &authentication_code_info) {
  switch (authentication_code_info.type) {
    case AuthenticationCodeInfo::Type::None:
      return nullptr;
    case AuthenticationCodeInfo::Type::Message:
      return td_api::make_object<td_api::authenticationCodeTypeTelegramMessage>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::Sms:
      return td_api::make_object<td_api::authenticationCodeTypeSms>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::Call:
      return td_api::make_object<td_api::authenticationCodeTypeCall>(authentication_code_info.length);
    case AuthenticationCodeInfo::Type::FlashCall:
      return td_api::make_object<td_api::authenticationCodeTypeFlashCall>(authentication_code_info.pattern);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
