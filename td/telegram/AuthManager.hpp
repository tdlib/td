//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AuthManager.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/SendCodeHelper.hpp"
#include "td/telegram/Version.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void AuthManager::WaitPasswordState::store(StorerT &storer) const {
  using td::store;
  store(current_client_salt_, storer);
  store(current_server_salt_, storer);
  store(srp_g_, storer);
  store(srp_p_, storer);
  store(srp_B_, storer);
  store(srp_id_, storer);
  store(hint_, storer);
  store(has_recovery_, storer);
  store(email_address_pattern_, storer);
}

template <class ParserT>
void AuthManager::WaitPasswordState::parse(ParserT &parser) {
  using td::parse;
  parse(current_client_salt_, parser);
  parse(current_server_salt_, parser);
  parse(srp_g_, parser);
  parse(srp_p_, parser);
  parse(srp_B_, parser);
  parse(srp_id_, parser);
  parse(hint_, parser);
  parse(has_recovery_, parser);
  parse(email_address_pattern_, parser);
}

template <class StorerT>
void AuthManager::DbState::store(StorerT &storer) const {
  using td::store;
  bool has_terms_of_service = !terms_of_service_.get_id().empty();
  bool is_pbkdf2_supported = true;
  bool is_srp_supported = true;
  bool is_wait_registration_supported = true;
  bool is_wait_registration_stores_phone_number = true;
  bool is_wait_qr_code_confirmation_supported = true;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_terms_of_service);
  STORE_FLAG(is_pbkdf2_supported);
  STORE_FLAG(is_srp_supported);
  STORE_FLAG(is_wait_registration_supported);
  STORE_FLAG(is_wait_registration_stores_phone_number);
  STORE_FLAG(is_wait_qr_code_confirmation_supported);
  END_STORE_FLAGS();
  store(state_, storer);
  store(api_id_, storer);
  store(api_hash_, storer);
  store(state_timestamp_, storer);

  if (has_terms_of_service) {
    store(terms_of_service_, storer);
  }

  if (state_ == State::WaitCode) {
    store(send_code_helper_, storer);
  } else if (state_ == State::WaitQrCodeConfirmation) {
    store(other_user_ids_, storer);
    store(login_token_, storer);
    store_time(login_token_expires_at_, storer);
  } else if (state_ == State::WaitPassword) {
    store(wait_password_state_, storer);
  } else if (state_ == State::WaitRegistration) {
    store(send_code_helper_, storer);
  } else {
    UNREACHABLE();
  }
}

template <class ParserT>
void AuthManager::DbState::parse(ParserT &parser) {
  using td::parse;
  bool has_terms_of_service = false;
  bool is_pbkdf2_supported = false;
  bool is_srp_supported = false;
  bool is_wait_registration_supported = false;
  bool is_wait_registration_stores_phone_number = false;
  bool is_wait_qr_code_confirmation_supported = false;
  if (parser.version() >= static_cast<int32>(Version::AddTermsOfService)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_terms_of_service);
    PARSE_FLAG(is_pbkdf2_supported);
    PARSE_FLAG(is_srp_supported);
    PARSE_FLAG(is_wait_registration_supported);
    PARSE_FLAG(is_wait_registration_stores_phone_number);
    PARSE_FLAG(is_wait_qr_code_confirmation_supported);
    END_PARSE_FLAGS();
  }
  if (!is_wait_qr_code_confirmation_supported) {
    return parser.set_error("Have no QR code confirmation support");
  }
  CHECK(is_pbkdf2_supported);
  CHECK(is_srp_supported);
  CHECK(is_wait_registration_supported);
  CHECK(is_wait_registration_stores_phone_number);

  parse(state_, parser);
  parse(api_id_, parser);
  parse(api_hash_, parser);
  parse(state_timestamp_, parser);

  if (has_terms_of_service) {
    parse(terms_of_service_, parser);
  }

  if (state_ == State::WaitCode) {
    parse(send_code_helper_, parser);
  } else if (state_ == State::WaitQrCodeConfirmation) {
    parse(other_user_ids_, parser);
    parse(login_token_, parser);
    parse_time(login_token_expires_at_, parser);
  } else if (state_ == State::WaitPassword) {
    parse(wait_password_state_, parser);
  } else if (state_ == State::WaitRegistration) {
    parse(send_code_helper_, parser);
  } else {
    parser.set_error(PSTRING() << "Unexpected " << tag("state", static_cast<int32>(state_)));
  }
}

}  // namespace td
