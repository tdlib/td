//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AuthManager.h"

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
  store(current_salt_, storer);
  store(new_salt_, storer);
  store(hint_, storer);
  store(has_recovery_, storer);
  store(email_address_pattern_, storer);
}

template <class T>
void AuthManager::WaitPasswordState::parse(T &parser) {
  using td::parse;
  parse(current_salt_, parser);
  parse(new_salt_, parser);
  parse(hint_, parser);
  parse(has_recovery_, parser);
  parse(email_address_pattern_, parser);
}

template <class T>
void AuthManager::DbState::store(T &storer) const {
  using td::store;
  store(state_, storer);
  store(api_id_, storer);
  store(api_hash_, storer);
  store(state_timestamp_, storer);

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
  parse(state_, parser);
  parse(api_id_, parser);
  parse(api_hash_, parser);
  parse(state_timestamp_, parser);

  if (state_ == State::WaitCode) {
    parse(send_code_helper_, parser);
  } else if (state_ == State::WaitPassword) {
    parse(wait_password_state_, parser);
  } else {
    parser.set_error(PSTRING() << "Unexpected " << tag("state", static_cast<int32>(state_)));
  }
}
}  // namespace td
