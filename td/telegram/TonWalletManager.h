//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class TonWalletManager final : public Actor {
 public:
  TonWalletManager(Td *td, ActorShared<> parent);

  void perform_ton_center_api_request(const string &endpoint,
                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                      Promise<string> &&promise);

  void get_ton_center_streaming_api_url(Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise);

 private:
  void tear_down() final;

  void on_get_ton_center_streaming_api_url(
      Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url);

  Td *td_;
  ActorShared<> parent_;

  vector<Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>>> get_streaming_api_url_queries_;
};

}  // namespace td
