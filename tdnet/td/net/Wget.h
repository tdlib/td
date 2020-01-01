//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/net/HttpOutboundConnection.h"
#include "td/net/HttpQuery.h"
#include "td/net/SslStream.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Wget : public HttpOutboundConnection::Callback {
 public:
  explicit Wget(Promise<unique_ptr<HttpQuery>> promise, string url, std::vector<std::pair<string, string>> headers = {},
                int32 timeout_in = 10, int32 ttl = 3, bool prefer_ipv6 = false,
                SslStream::VerifyPeer verify_peer = SslStream::VerifyPeer::On, string content = {},
                string content_type = {});

 private:
  Status try_init();
  void loop() override;
  void handle(unique_ptr<HttpQuery> result) override;
  void on_connection_error(Status error) override;
  void on_ok(unique_ptr<HttpQuery> http_query_ptr);
  void on_error(Status error);

  void tear_down() override;
  void start_up() override;
  void timeout_expired() override;

  Promise<unique_ptr<HttpQuery>> promise_;
  ActorOwn<HttpOutboundConnection> connection_;
  string input_url_;
  std::vector<std::pair<string, string>> headers_;
  int32 timeout_in_;
  int32 ttl_;
  bool prefer_ipv6_ = false;
  SslStream::VerifyPeer verify_peer_;
  string content_;
  string content_type_;
};

}  // namespace td
