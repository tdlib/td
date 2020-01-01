//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <array>
#include <utility>

namespace td {

class DeviceTokenManager : public NetQueryCallback {
 public:
  explicit DeviceTokenManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void register_device(tl_object_ptr<td_api::DeviceToken> device_token_ptr, vector<int32> other_user_ids,
                       Promise<td_api::object_ptr<td_api::pushReceiverId>> promise);

  void reregister_device();

  vector<std::pair<int64, Slice>> get_encryption_keys() const;

 private:
  ActorShared<> parent_;
  enum TokenType : int32 {
    APNS = 1,
    FCM = 2,
    MPNS = 3,
    SIMPLE_PUSH = 4,
    UBUNTU_PHONE = 5,
    BLACKBERRY = 6,
    UNUSED = 7,
    WNS = 8,
    APNS_VOIP = 9,
    WEB_PUSH = 10,
    MPNS_VOIP = 11,
    TIZEN = 12,
    SIZE
  };
  struct TokenInfo {
    enum class State : int32 { Sync, Unregister, Register, Reregister };
    State state = State::Sync;
    string token;
    uint64 net_query_id = 0;
    vector<int32> other_user_ids;
    bool is_app_sandbox = false;
    bool encrypt = false;
    string encryption_key;
    int64 encryption_key_id = 0;
    Promise<td_api::object_ptr<td_api::pushReceiverId>> promise;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  friend StringBuilder &operator<<(StringBuilder &string_builder, const TokenInfo::State &state);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const TokenInfo &token_info);

  std::array<TokenInfo, TokenType::SIZE> tokens_;
  int32 sync_cnt_{0};

  void start_up() override;

  static string get_database_key(int32 token_type);
  void save_info(int32 token_type);

  void dec_sync_cnt();

  void loop() override;
  void on_result(NetQueryPtr net_query) override;
};

}  // namespace td
