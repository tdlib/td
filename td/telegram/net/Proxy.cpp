//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/Proxy.h"
#include "td/telegram/td_api.h"
namespace td {
Result<Proxy> Proxy::from_td_api(string server, int port, td_api::object_ptr<td_api::ProxyType> proxy_type) {
  if (proxy_type == nullptr) {
    return Status::Error(400, "Proxy type should not be empty");
  }
  if (server.empty()) {
    return Status::Error(400, "Server name can't be empty");
  }
  if (port <= 0 || port > 65535) {
    return Status::Error(400, "Wrong port number");
  }

  Proxy new_proxy;
  switch (proxy_type->get_id()) {
    case td_api::proxyTypeSocks5::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeSocks5>(proxy_type);
      new_proxy = Proxy::socks5(server, port, type->username_, type->password_);
      break;
    }
    case td_api::proxyTypeHttp::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeHttp>(proxy_type);
      if (type->http_only_) {
        new_proxy = Proxy::http_caching(server, port, type->username_, type->password_);
      } else {
        new_proxy = Proxy::http_tcp(server, port, type->username_, type->password_);
      }
      break;
    }
    case td_api::proxyTypeMtproto::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeMtproto>(proxy_type);
      TRY_RESULT(secret, mtproto::ProxySecret::from_link(type->secret_));
      new_proxy = Proxy::mtproto(server, port, secret);
      break;
    }
    default:
      UNREACHABLE();
  }
  return new_proxy;
}
}  // namespace td
