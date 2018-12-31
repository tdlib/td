//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DeviceTokenManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/telegram/td_api.hpp"
#include "td/telegram/telegram_api.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <type_traits>

namespace td {
void DeviceTokenManager::register_device(tl_object_ptr<td_api::DeviceToken> device_token,
                                         Promise<tl_object_ptr<td_api::ok>> promise) {
  Token token(*device_token);
  if (!clean_input_string(token.token)) {
    return promise.set_error(Status::Error(400, "Device token must be encoded in UTF-8"));
  }

  auto &info = tokens_[token.type];
  info.net_query_id = 0;
  if (token.token.empty()) {
    info.state = TokenInfo::State::Unregister;
    if (info.token.empty()) {
      info.state = TokenInfo::State::Sync;
    }
  } else {
    info.token = token.token;
    info.state = TokenInfo::State::Register;
  }
  if (info.promise) {
    info.promise.set_error(Status::Error(5, "Cancelled due to new registerDevice request"));
  }
  info.promise = std::move(promise);
  save_info(token.type);
  loop();
}

DeviceTokenManager::Token::Token(td_api::DeviceToken &device_token) {
  bool ok = downcast_call(device_token, [&](auto &obj) { token = obj.token_; });
  CHECK(ok);
  type = [&] {
    switch (device_token.get_id()) {
      case td_api::deviceTokenApplePush::ID:
        return TokenType::APNS;
      case td_api::deviceTokenGoogleCloudMessaging::ID:
        return TokenType::GCM;
      case td_api::deviceTokenMicrosoftPush::ID:
        return TokenType::MPNS;
      case td_api::deviceTokenUbuntuPush::ID:
        return TokenType::UbuntuPhone;
      case td_api::deviceTokenBlackberryPush::ID:
        return TokenType::Blackberry;
      default:
        UNREACHABLE();
    }
  }();
}
tl_object_ptr<td_api::DeviceToken> DeviceTokenManager::Token::as_td_api() {
  switch (type) {
    case TokenType::APNS:
      return make_tl_object<td_api::deviceTokenApplePush>(token);
    case TokenType::GCM:
      return make_tl_object<td_api::deviceTokenGoogleCloudMessaging>(token);
    case TokenType::MPNS:
      return make_tl_object<td_api::deviceTokenMicrosoftPush>(token);
    case TokenType::SimplePush:
      return make_tl_object<td_api::deviceTokenSimplePush>(token);
    case TokenType::UbuntuPhone:
      return make_tl_object<td_api::deviceTokenUbuntuPush>(token);
    case TokenType::Blackberry:
      return make_tl_object<td_api::deviceTokenBlackberryPush>(token);
    default:
      UNREACHABLE();
  }
}
DeviceTokenManager::TokenInfo::TokenInfo(string from) {
  if (from.empty()) {
    return;
  }
  char c = from[0];
  if (c == '+') {
    state = State::Register;
  } else if (c == '-') {
    state = State::Unregister;
  } else if (c == '=') {
    state = State::Sync;
  } else {
    LOG(ERROR) << "Invalid serialized TokenInfo: " << tag("token_info", from);
    return;
  }
  token = from.substr(1);
}

string DeviceTokenManager::TokenInfo::serialize() {
  char c = [&] {
    switch (state) {
      case State::Sync:
        return '=';
      case State::Unregister:
        return '-';
      case State::Register:
        return '+';
      default:
        UNREACHABLE();
    }
  }();
  return c + token;
}

void DeviceTokenManager::start_up() {
  for (int32 token_type = 1; token_type < TokenType::Size; token_type++) {
    tokens_[token_type] = TokenInfo(G()->td_db()->get_binlog_pmc()->get(PSTRING() << "device_token" << token_type));
    LOG(INFO) << token_type << "--->" << tokens_[token_type].serialize();
  }
  loop();
}
void DeviceTokenManager::save_info(int32 token_type) {
  auto key = PSTRING() << "device_token" << token_type;
  string value = tokens_[token_type].serialize();
  LOG(INFO) << "SET " << key << "--->" << value;
  G()->td_db()->get_binlog_pmc()->set(key, value);
  sync_cnt_++;
  G()->td_db()->get_binlog_pmc()->force_sync(
      PromiseCreator::event(self_closure(this, &DeviceTokenManager::dec_sync_cnt)));
}

void DeviceTokenManager::dec_sync_cnt() {
  sync_cnt_--;
  loop();
}

void DeviceTokenManager::loop() {
  if (sync_cnt_ != 0) {
    return;
  }
  for (int32 token_type = 1; token_type < TokenType::Size; token_type++) {
    auto &info = tokens_[token_type];
    if (info.state == TokenInfo::State::Sync) {
      continue;
    }
    if (info.net_query_id != 0) {
      continue;
    }
    // have to send query
    NetQueryPtr net_query;
    if (info.state == TokenInfo::State::Unregister) {
      net_query = G()->net_query_creator().create(
          create_storer(telegram_api::account_unregisterDevice(token_type, info.token)));
    } else {
      net_query =
          G()->net_query_creator().create(create_storer(telegram_api::account_registerDevice(token_type, info.token)));
    }
    info.net_query_id = net_query->id();
    G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this, token_type));
  }
}

void DeviceTokenManager::on_result(NetQueryPtr net_query) {
  auto token_type = static_cast<TokenType>(get_link_token());
  CHECK(token_type >= 1 && token_type < TokenType::Size);
  auto &info = tokens_[token_type];
  if (info.net_query_id != net_query->id()) {
    net_query->clear();
    return;
  }
  info.net_query_id = 0;
  static_assert(std::is_same<telegram_api::account_registerDevice::ReturnType,
                             telegram_api::account_unregisterDevice::ReturnType>::value,
                "");
  auto r_flag = fetch_result<telegram_api::account_registerDevice>(std::move(net_query));

  info.net_query_id = 0;
  if (r_flag.is_ok() && r_flag.ok()) {
    if (info.promise) {
      info.promise.set_value(make_tl_object<td_api::ok>());
    }
    if (info.state == TokenInfo::State::Unregister) {
      info.token = "";
    }
    info.state = TokenInfo::State::Sync;
    save_info(token_type);
  } else {
    if (info.promise) {
      if (r_flag.is_error()) {
        info.promise.set_error(r_flag.error().clone());
      } else {
        info.promise.set_error(Status::Error(5, "Got false as result"));
      }
    }
    if (info.state == TokenInfo::State::Register) {
      info.state = TokenInfo::State::Unregister;
    } else {
      info.state = TokenInfo::State::Sync;
      info.token = "";
    }
    save_info(token_type);
    if (r_flag.is_error()) {
      LOG(ERROR) << r_flag.error();
    }
  }
  loop();
}
}  // namespace td
