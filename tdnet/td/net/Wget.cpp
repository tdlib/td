//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/Wget.h"

#include "td/net/HttpHeaderCreator.h"
#include "td/net/HttpOutboundConnection.h"
#include "td/net/SslFd.h"

#include "td/utils/buffer.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"

#include <limits>

namespace td {
Wget::Wget(Promise<HttpQueryPtr> promise, string url, std::vector<std::pair<string, string>> headers, int32 timeout_in,
           int32 ttl, SslFd::VerifyPeer verify_peer)
    : promise_(std::move(promise))
    , input_url_(std::move(url))
    , headers_(std::move(headers))
    , timeout_in_(timeout_in)
    , ttl_(ttl)
    , verify_peer_(verify_peer) {
}

Status Wget::try_init() {
  string input_url = input_url_;
  TRY_RESULT(url, parse_url(MutableSlice(input_url)));

  IPAddress addr;
  TRY_STATUS(addr.init_host_port(url.host_, url.port_));

  TRY_RESULT(fd, SocketFd::open(addr));
  if (url.protocol_ == HttpUrl::Protocol::HTTP) {
    connection_ =
        create_actor<HttpOutboundConnection>("Connect", std::move(fd), std::numeric_limits<std::size_t>::max(), 0, 0,
                                             ActorOwn<HttpOutboundConnection::Callback>(actor_id(this)));
  } else {
    TRY_RESULT(ssl_fd, SslFd::init(std::move(fd), url.host_, CSlice() /* certificate */, verify_peer_));
    connection_ =
        create_actor<HttpOutboundConnection>("Connect", std::move(ssl_fd), std::numeric_limits<std::size_t>::max(), 0,
                                             0, ActorOwn<HttpOutboundConnection::Callback>(actor_id(this)));
  }

  HttpHeaderCreator hc;
  hc.init_get(url.query_);
  for (auto &header : headers_) {
    hc.add_header(header.first, header.second);
  }
  hc.add_header("Host", url.host_);
  hc.add_header("Accept-Encoding", "gzip, deflate");

  send_closure(connection_, &HttpOutboundConnection::write_next, BufferSlice(hc.finish().ok()));
  send_closure(connection_, &HttpOutboundConnection::write_ok);
  return Status::OK();
}

void Wget::loop() {
  if (connection_.empty()) {
    auto status = try_init();
    if (status.is_error()) {
      return on_error(std::move(status));
    }
  }
}

void Wget::handle(HttpQueryPtr result) {
  on_ok(std::move(result));
}

void Wget::on_connection_error(Status error) {
  on_error(std::move(error));
}

void Wget::on_ok(HttpQueryPtr http_query_ptr) {
  CHECK(promise_);
  if (http_query_ptr->code_ == 302 && ttl_ > 0) {
    LOG(DEBUG) << *http_query_ptr;
    input_url_ = http_query_ptr->header("location").str();
    LOG(DEBUG) << input_url_;
    ttl_--;
    connection_.reset();
    yield();
  } else if (http_query_ptr->code_ >= 200 && http_query_ptr->code_ < 300) {
    promise_.set_value(std::move(http_query_ptr));
    stop();
  } else {
    on_error(Status::Error("http error"));
  }
}

void Wget::on_error(Status error) {
  CHECK(error.is_error());
  CHECK(promise_);
  promise_.set_error(std::move(error));
  stop();
}

void Wget::start_up() {
  set_timeout_in(timeout_in_);
  loop();
}

void Wget::timeout_expired() {
  on_error(Status::Error("Timeout expired"));
}

void Wget::tear_down() {
  if (promise_) {
    on_error(Status::Error("Cancelled"));
  }
}
}  // namespace td
