//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/Proxy.h"

#include "td/mtproto/Handshake.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/TransportType.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class ProxyChecker final : public NetQueryCallback {
 public:
  explicit ProxyChecker(ActorShared<> parent);
  ProxyChecker(const ProxyChecker &) = delete;
  ProxyChecker &operator=(const ProxyChecker &) = delete;
  ProxyChecker(ProxyChecker &&) = delete;
  ProxyChecker &operator=(ProxyChecker &&) = delete;
  ~ProxyChecker() final = default;

  void test_proxy(Proxy &&proxy, int32 dc_id, double timeout, Promise<Unit> &&promise);

 private:
  ActorShared<> parent_;

  struct TestProxyRequest {
    Proxy proxy_;
    int16 dc_id_ = -1;
    ActorOwn<> child_;
    Promise<Unit> promise_;

    mtproto::TransportType get_transport() const {
      return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, dc_id_, proxy_.secret()};
    }
  };
  uint64 test_proxy_request_id_ = 0;
  FlatHashMap<uint64, unique_ptr<TestProxyRequest>> test_proxy_requests_;

  void on_test_proxy_connection_data(uint64 request_id, Result<ConnectionCreator::ConnectionData> r_data);

  void on_test_proxy_handshake_connection(uint64 request_id,
                                          Result<unique_ptr<mtproto::RawConnection>> r_raw_connection);

  void on_test_proxy_handshake(uint64 request_id, Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake);

  void on_test_proxy_timeout(uint64 request_id);
};

}  // namespace td
