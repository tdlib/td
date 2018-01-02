//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpOutboundConnection.h"

#include "td/utils/logging.h"

namespace td {
// HttpOutboundConnection implementation
void HttpOutboundConnection::on_query(HttpQueryPtr query) {
  CHECK(!callback_.empty());
  send_closure(callback_, &Callback::handle, std::move(query));
}

void HttpOutboundConnection::on_error(Status error) {
  CHECK(!callback_.empty());
  send_closure(callback_, &Callback::on_connection_error, std::move(error));
}

}  // namespace td
