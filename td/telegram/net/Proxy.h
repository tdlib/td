//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/ProxySecret.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <utility>

namespace td {

namespace tl {
template <class Type>
class unique_ptr;
}  // namespace tl

namespace td_api {
template <class Type>
using object_ptr = ::td::tl::unique_ptr<Type>;

class proxy;
class ProxyType;
}  // namespace td_api

class Proxy {
 public:
  static Result<Proxy> create_proxy(const td_api::proxy *proxy);

  static Proxy socks5(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::Socks5;
    proxy.server_ = std::move(server);
    proxy.port_ = port;
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy http_tcp(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::HttpTcp;
    proxy.server_ = std::move(server);
    proxy.port_ = port;
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy http_caching(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::HttpCaching;
    proxy.server_ = std::move(server);
    proxy.port_ = port;
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy mtproto(string server, int32 port, mtproto::ProxySecret secret) {
    Proxy proxy;
    proxy.type_ = Type::Mtproto;
    proxy.server_ = std::move(server);
    proxy.port_ = port;
    proxy.secret_ = std::move(secret);
    return proxy;
  }

  static constexpr int32 WEB_PROXY_PORT = 443;

  static Proxy web(string server, string path, mtproto::ProxySecret secret) {
    Proxy proxy;
    proxy.type_ = Type::Web;
    proxy.server_ = std::move(server);
    proxy.port_ = WEB_PROXY_PORT;
    proxy.path_ = std::move(path);
    proxy.secret_ = std::move(secret);
    return proxy;
  }

  // returns the canonical host and path of a web proxy address "host[/path]"
  static Result<std::pair<string, string>> parse_web_proxy_address(Slice address);

  static Result<mtproto::ProxySecret> get_web_proxy_secret(Slice encoded_secret);

  // a link secret of a web proxy with a path is marked, so that clients without path support reject the link
  static Result<mtproto::ProxySecret> get_web_proxy_link_secret(Slice link_secret, bool has_path);

  CSlice server() const {
    return server_;
  }

  CSlice path() const {
    return path_;
  }

  string get_web_proxy_address() const;

  string get_web_proxy_link_secret() const;

  string get_web_proxy_bridge_capability() const;

  int32 port() const {
    return port_;
  }

  CSlice user() const {
    return user_;
  }

  CSlice password() const {
    return password_;
  }

  const mtproto::ProxySecret &secret() const {
    return secret_;
  }

  enum class Type : int32 { None, Socks5, Mtproto, HttpTcp, HttpCaching, Web };
  Type type() const {
    return type_;
  }

  bool use_proxy() const {
    return type() != Proxy::Type::None;
  }
  bool use_socks5_proxy() const {
    return type() == Proxy::Type::Socks5;
  }
  bool use_mtproto_proxy() const {
    return type() == Proxy::Type::Mtproto;
  }
  bool use_http_tcp_proxy() const {
    return type() == Proxy::Type::HttpTcp;
  }
  bool use_http_caching_proxy() const {
    return type() == Proxy::Type::HttpCaching;
  }
  bool use_web_proxy() const {
    return type() == Proxy::Type::Web;
  }
  // an MTProto proxy, which is reached directly or through a web proxy relay
  bool use_mtproto_transport_proxy() const {
    return type() == Proxy::Type::Mtproto || type() == Proxy::Type::Web;
  }

  td_api::object_ptr<td_api::proxy> get_proxy_object() const;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(type_, storer);
    if (type_ == Proxy::Type::Socks5 || type_ == Proxy::Type::HttpTcp || type_ == Proxy::Type::HttpCaching) {
      store(server_, storer);
      store(port_, storer);
      store(user_, storer);
      store(password_, storer);
    } else if (type_ == Proxy::Type::Mtproto) {
      store(server_, storer);
      store(port_, storer);
      store(secret_.get_encoded_secret(), storer);
    } else if (type_ == Proxy::Type::Web) {
      store(server_, storer);
      store(port_, storer);
      store(secret_.get_encoded_secret(), storer);
      store(path_, storer);
    } else {
      CHECK(type_ == Proxy::Type::None);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(type_, parser);
    if (type_ == Proxy::Type::Socks5 || type_ == Proxy::Type::HttpTcp || type_ == Proxy::Type::HttpCaching) {
      parse(server_, parser);
      parse(port_, parser);
      parse(user_, parser);
      parse(password_, parser);
    } else if (type_ == Proxy::Type::Mtproto) {
      parse(server_, parser);
      parse(port_, parser);
      secret_ = mtproto::ProxySecret::from_link(parser.template fetch_string<Slice>(), true).move_as_ok();
    } else if (type_ == Proxy::Type::Web) {
      parse(server_, parser);
      parse(port_, parser);
      secret_ = mtproto::ProxySecret::from_link(parser.template fetch_string<Slice>(), true).move_as_ok();
      parse(path_, parser);
    } else {
      CHECK(type_ == Proxy::Type::None);
    }
  }

 private:
  Type type_{Type::None};
  string server_;
  int32 port_ = 0;
  string user_;
  string password_;
  string path_;
  mtproto::ProxySecret secret_;

  static Result<Proxy> create_proxy(string server, int port, const td_api::ProxyType *proxy_type);

  static Result<string> normalize_web_proxy_host(CSlice server);

  static bool is_valid_web_proxy_path(Slice path);
};

inline bool operator==(const Proxy &lhs, const Proxy &rhs) {
  return lhs.type() == rhs.type() && lhs.server() == rhs.server() && lhs.port() == rhs.port() &&
         lhs.user() == rhs.user() && lhs.password() == rhs.password() && lhs.path() == rhs.path() &&
         lhs.secret() == rhs.secret();
}

inline bool operator!=(const Proxy &lhs, const Proxy &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Proxy &proxy);

}  // namespace td
