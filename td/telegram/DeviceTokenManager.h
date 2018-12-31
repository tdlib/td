//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/net/NetQuery.h"

#include "td/telegram/td_api.h"

#include "td/utils/common.h"

#include <array>

namespace td {
class DeviceTokenManager : public NetQueryCallback {
 public:
  explicit DeviceTokenManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void register_device(tl_object_ptr<td_api::DeviceToken> device_token, Promise<tl_object_ptr<td_api::ok>> promise);

 private:
  ActorShared<> parent_;
  enum TokenType : int32 { APNS = 1, GCM = 2, MPNS = 3, SimplePush = 4, UbuntuPhone = 5, Blackberry = 6, Size };
  struct Token {
    TokenType type;
    string token;
    Token(TokenType type, string token) : type(type), token(std::move(token)) {
    }
    explicit Token(td_api::DeviceToken &device_token);
    tl_object_ptr<td_api::DeviceToken> as_td_api();
  };
  struct TokenInfo {
    enum class State { Sync, Unregister, Register };
    State state = State::Sync;
    string token;
    uint64 net_query_id = 0;
    Promise<tl_object_ptr<td_api::ok>> promise;

    TokenInfo() = default;
    explicit TokenInfo(string from);

    string serialize();
  };

  std::array<TokenInfo, TokenType::Size> tokens_;
  int32 sync_cnt_{0};

  void start_up() override;
  void save_info(int32 token_type);
  void dec_sync_cnt();

  void loop() override;
  void on_result(NetQueryPtr net_query) override;
};
}  // namespace td
