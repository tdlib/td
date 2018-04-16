//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DeviceTokenManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/UserId.h"

#include "td/telegram/td_api.hpp"
#include "td/telegram/telegram_api.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

#include <type_traits>

namespace td {

template <class StorerT>
void DeviceTokenManager::TokenInfo::store(StorerT &storer) const {
  using td::store;
  bool has_other_user_ids = !other_user_ids.empty();
  bool is_sync = state == State::Sync;
  bool is_unregister = state == State::Unregister;
  bool is_register = state == State::Register;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_other_user_ids);
  STORE_FLAG(is_sync);
  STORE_FLAG(is_unregister);
  STORE_FLAG(is_register);
  STORE_FLAG(is_app_sandbox);
  END_STORE_FLAGS();
  store(token, storer);
  if (has_other_user_ids) {
    store(other_user_ids, storer);
  }
}

template <class ParserT>
void DeviceTokenManager::TokenInfo::parse(ParserT &parser) {
  using td::parse;
  bool has_other_user_ids;
  bool is_sync;
  bool is_unregister;
  bool is_register;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_other_user_ids);
  PARSE_FLAG(is_sync);
  PARSE_FLAG(is_unregister);
  PARSE_FLAG(is_register);
  PARSE_FLAG(is_app_sandbox);
  END_PARSE_FLAGS_GENERIC();
  CHECK(is_sync + is_unregister + is_register == 1);
  if (is_sync) {
    state = State::Sync;
  } else if (is_unregister) {
    state = State::Unregister;
  } else {
    state = State::Register;
  }
  parse(token, parser);
  if (has_other_user_ids) {
    parse(other_user_ids, parser);
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const DeviceTokenManager::TokenInfo &token_info) {
  switch (token_info.state) {
    case DeviceTokenManager::TokenInfo::State::Sync:
      string_builder << "Synchronized";
      break;
    case DeviceTokenManager::TokenInfo::State::Unregister:
      string_builder << "Unregister";
      break;
    case DeviceTokenManager::TokenInfo::State::Register:
      string_builder << "Register";
      break;
    default:
      UNREACHABLE();
  }
  string_builder << " token \"" << format::escaped(token_info.token) << "\"";
  if (!token_info.other_user_ids.empty()) {
    string_builder << ", with other users " << token_info.other_user_ids;
  }
  if (token_info.is_app_sandbox) {
    string_builder << ", sandboxed";
  }
  return string_builder;
}

void DeviceTokenManager::register_device(tl_object_ptr<td_api::DeviceToken> device_token_ptr,
                                         vector<int32> other_user_ids, Promise<tl_object_ptr<td_api::ok>> promise) {
  CHECK(device_token_ptr != nullptr);
  TokenType token_type;
  string token;
  bool is_app_sandbox = false;
  switch (device_token_ptr->get_id()) {
    case td_api::deviceTokenApplePush::ID: {
      auto device_token = static_cast<td_api::deviceTokenApplePush *>(device_token_ptr.get());
      token = std::move(device_token->device_token_);
      token_type = TokenType::APNS;
      is_app_sandbox = device_token->is_app_sandbox_;
      break;
    }
    case td_api::deviceTokenGoogleCloudMessaging::ID: {
      auto device_token = static_cast<td_api::deviceTokenGoogleCloudMessaging *>(device_token_ptr.get());
      token = std::move(device_token->token_);
      token_type = TokenType::GCM;
      break;
    }
    case td_api::deviceTokenMicrosoftPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenMicrosoftPush *>(device_token_ptr.get());
      token = std::move(device_token->channel_uri_);
      token_type = TokenType::MPNS;
      break;
    }
    case td_api::deviceTokenSimplePush::ID: {
      auto device_token = static_cast<td_api::deviceTokenSimplePush *>(device_token_ptr.get());
      token = std::move(device_token->endpoint_);
      token_type = TokenType::SIMPLE_PUSH;
      break;
    }
    case td_api::deviceTokenUbuntuPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenUbuntuPush *>(device_token_ptr.get());
      token = std::move(device_token->token_);
      token_type = TokenType::UBUNTU_PHONE;
      break;
    }
    case td_api::deviceTokenBlackBerryPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenBlackBerryPush *>(device_token_ptr.get());
      token = std::move(device_token->token_);
      token_type = TokenType::BLACKBERRY;
      break;
    }
    case td_api::deviceTokenWindowsPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenWindowsPush *>(device_token_ptr.get());
      token = std::move(device_token->access_token_);
      token_type = TokenType::WNS;
      break;
    }
    case td_api::deviceTokenApplePushVoIP::ID: {
      auto device_token = static_cast<td_api::deviceTokenApplePushVoIP *>(device_token_ptr.get());
      token = std::move(device_token->device_token_);
      token_type = TokenType::APNS_VOIP;
      is_app_sandbox = device_token->is_app_sandbox_;
      break;
    }
    case td_api::deviceTokenWebPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenWebPush *>(device_token_ptr.get());
      if (device_token->endpoint_.find(',') != string::npos) {
        return promise.set_error(Status::Error(400, "Illegal endpoint value"));
      }
      if (!is_base64url(device_token->p256dh_base64url_)) {
        return promise.set_error(Status::Error(400, "Public key must be base64url-encoded"));
      }
      if (!is_base64url(device_token->auth_base64url_)) {
        return promise.set_error(Status::Error(400, "Authentication secret must be base64url-encoded"));
      }
      if (!clean_input_string(device_token->endpoint_)) {
        return promise.set_error(Status::Error(400, "Endpoint must be encoded in UTF-8"));
      }

      if (!device_token->endpoint_.empty()) {
        token = json_encode<string>(json_object([&device_token](auto &o) {
          o("endpoint", device_token->endpoint_);
          o("keys", json_object([&device_token](auto &o) {
              o("p256dh", device_token->p256dh_base64url_);
              o("auth", device_token->auth_base64url_);
            }));
        }));
      }
      token_type = TokenType::WEB_PUSH;
      break;
    }
    case td_api::deviceTokenMicrosoftPushVoIP::ID: {
      auto device_token = static_cast<td_api::deviceTokenMicrosoftPushVoIP *>(device_token_ptr.get());
      token = std::move(device_token->channel_uri_);
      token_type = TokenType::MPNS_VOIP;
      break;
    }
    case td_api::deviceTokenTizenPush::ID: {
      auto device_token = static_cast<td_api::deviceTokenTizenPush *>(device_token_ptr.get());
      token = std::move(device_token->reg_id_);
      token_type = TokenType::TIZEN;
      break;
    }
    default:
      UNREACHABLE();
  }

  if (!clean_input_string(token)) {
    return promise.set_error(Status::Error(400, "Device token must be encoded in UTF-8"));
  }
  for (auto &other_user_id : other_user_ids) {
    UserId user_id(other_user_id);
    if (!user_id.is_valid()) {
      return promise.set_error(Status::Error(400, "Invalid user_id among other user_ids"));
    }
  }
  if (other_user_ids.size() > MAX_OTHER_USER_IDS) {
    return promise.set_error(Status::Error(400, "Too much other user_ids"));
  }

  auto &info = tokens_[token_type];
  info.net_query_id = 0;
  if (token.empty()) {
    if (info.token.empty()) {
      // already unregistered
      return promise.set_value(make_tl_object<td_api::ok>());
    }

    info.state = TokenInfo::State::Unregister;
  } else {
    info.state = TokenInfo::State::Register;
    info.token = std::move(token);
  }
  info.other_user_ids = std::move(other_user_ids);
  info.is_app_sandbox = is_app_sandbox;
  info.promise.set_value(make_tl_object<td_api::ok>());
  info.promise = std::move(promise);
  save_info(token_type);
}

string DeviceTokenManager::get_database_key(int32 token_type) {
  return PSTRING() << "device_token" << token_type;
}

void DeviceTokenManager::start_up() {
  for (int32 token_type = 1; token_type < TokenType::SIZE; token_type++) {
    auto serialized = G()->td_db()->get_binlog_pmc()->get(get_database_key(token_type));
    if (serialized.empty()) {
      continue;
    }

    auto &token = tokens_[token_type];
    char c = serialized[0];
    if (c == '*') {
      unserialize(token, serialized.substr(1)).ensure();
    } else {
      // legacy
      if (c == '+') {
        token.state = TokenInfo::State::Register;
      } else if (c == '-') {
        token.state = TokenInfo::State::Unregister;
      } else if (c == '=') {
        token.state = TokenInfo::State::Sync;
      } else {
        LOG(ERROR) << "Invalid serialized TokenInfo: " << format::escaped(serialized);
        continue;
      }
      token.token = serialized.substr(1);
    }
    LOG(INFO) << "GET device token " << token_type << "--->" << tokens_[token_type];
  }
  loop();
}

void DeviceTokenManager::save_info(int32 token_type) {
  LOG(INFO) << "SET device token " << token_type << "--->" << tokens_[token_type];
  if (tokens_[token_type].token.empty()) {
    G()->td_db()->get_binlog_pmc()->erase(get_database_key(token_type));
  } else {
    G()->td_db()->get_binlog_pmc()->set(get_database_key(token_type), "*" + serialize(tokens_[token_type]));
  }
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
  for (int32 token_type = 1; token_type < TokenType::SIZE; token_type++) {
    auto &info = tokens_[token_type];
    if (info.state == TokenInfo::State::Sync) {
      continue;
    }
    if (info.net_query_id != 0) {
      continue;
    }
    // have to send query
    NetQueryPtr net_query;
    auto other_user_ids = info.other_user_ids;
    if (info.state == TokenInfo::State::Unregister) {
      net_query = G()->net_query_creator().create(
          create_storer(telegram_api::account_unregisterDevice(token_type, info.token, std::move(other_user_ids))));
    } else {
      net_query = G()->net_query_creator().create(create_storer(telegram_api::account_registerDevice(
          token_type, info.token, info.is_app_sandbox, BufferSlice(), std::move(other_user_ids))));
    }
    info.net_query_id = net_query->id();
    G()->net_query_dispatcher().dispatch_with_callback(std::move(net_query), actor_shared(this, token_type));
  }
}

void DeviceTokenManager::on_result(NetQueryPtr net_query) {
  auto token_type = static_cast<TokenType>(get_link_token());
  CHECK(token_type >= 1 && token_type < TokenType::SIZE);
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
    if (r_flag.is_error()) {
      LOG(ERROR) << r_flag.error();
    }
  }
  save_info(token_type);
}

}  // namespace td
