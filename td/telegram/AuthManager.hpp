//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AuthManager.h"

#include "td/telegram/Version.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void SendCodeHelper::AuthenticationCodeInfo::store(T &storer) const {
  using td::store;
  store(type, storer);
  store(length, storer);
  store(pattern, storer);
}

template <class T>
void SendCodeHelper::AuthenticationCodeInfo::parse(T &parser) {
  using td::parse;
  parse(type, parser);
  parse(length, parser);
  parse(pattern, parser);
}

template <class T>
void SendCodeHelper::store(T &storer) const {
  using td::store;
  store(phone_number_, storer);
  store(phone_registered_, storer);
  store(phone_code_hash_, storer);
  store(sent_code_info_, storer);
  store(next_code_info_, storer);
  store(next_code_timestamp_, storer);
}

template <class T>
void SendCodeHelper::parse(T &parser) {
  using td::parse;
  parse(phone_number_, parser);
  parse(phone_registered_, parser);
  parse(phone_code_hash_, parser);
  parse(sent_code_info_, parser);
  parse(next_code_info_, parser);
  parse(next_code_timestamp_, parser);
}

template <class T>
void AuthManager::WaitPasswordState::store(T &storer) const {
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

template <class T>
void AuthManager::WaitPasswordState::parse(T &parser) {
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

template <class T>
void AuthManager::DbState::store(T &storer) const {
  using td::store;
  bool has_terms_of_service = !terms_of_service_.get_id().empty();
  bool is_pbkdf2_supported = true;
  bool is_srp_supported = true;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_terms_of_service);
  STORE_FLAG(is_pbkdf2_supported);
  STORE_FLAG(is_srp_supported);
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
  } else if (state_ == State::WaitPassword) {
    store(wait_password_state_, storer);
  } else {
    UNREACHABLE();
  }
}

template <class T>
void AuthManager::DbState::parse(T &parser) {
  using td::parse;
  bool has_terms_of_service = false;
  bool is_pbkdf2_supported = false;
  bool is_srp_supported = false;
  if (parser.version() >= static_cast<int32>(Version::AddTermsOfService)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_terms_of_service);
    PARSE_FLAG(is_pbkdf2_supported);
    PARSE_FLAG(is_srp_supported);
    END_PARSE_FLAGS();
  }
  parse(state_, parser);
  parse(api_id_, parser);
  parse(api_hash_, parser);
  parse(state_timestamp_, parser);

  if (has_terms_of_service) {
    parse(terms_of_service_, parser);
  }

  if (state_ == State::WaitCode) {
    parse(send_code_helper_, parser);
    if (parser.version() < static_cast<int32>(Version::AddTermsOfService)) {
      return parser.set_error("Have no terms of service");
    }
  } else if (state_ == State::WaitPassword) {
    if (!is_pbkdf2_supported) {
      return parser.set_error("Need PBKDF2 support");
    }
    if (!is_srp_supported) {
      return parser.set_error("Need SRP support");
    }
    parse(wait_password_state_, parser);
  } else {
    parser.set_error(PSTRING() << "Unexpected " << tag("state", static_cast<int32>(state_)));
  }
}

}  // namespace td
