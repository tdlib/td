//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TermsOfService.h"

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

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

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

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
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

}  // namespace td
