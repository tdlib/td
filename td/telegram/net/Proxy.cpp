//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/Proxy.h"

#include "td/telegram/td_api.h"

#include "td/utils/base64.h"
#include "td/utils/crypto.h"
#include "td/utils/misc.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

namespace td {

// 0xdd must never be used as the marker: older clients read a 17-byte secret starting with it as a valid one
static constexpr unsigned char WEB_PROXY_LINK_SECRET_MARKER = 0x70;

Result<string> Proxy::normalize_web_proxy_host(CSlice server) {
  if (!check_utf8(server)) {
    return Status::Error(400, "Invalid web proxy server");
  }
  auto host = trim(Slice(server));
  Slice scheme("https://");
  if (host.size() >= scheme.size() && to_lower(host.substr(0, scheme.size())) == scheme) {
    host.remove_prefix(scheme.size());
  }
  if (!host.empty() && host.back() == '.') {
    host.remove_suffix(1);
  }
  if (host.empty() || host.size() > 253 * 4) {
    return Status::Error(400, "Invalid web proxy server");
  }
  // only the Windows idn_to_ascii case-folds, but the host is a part of the capability, so it must not depend on the OS
  auto r_ascii_host = idn_to_ascii(utf8_to_lower(host));
  if (r_ascii_host.is_error()) {
    return Status::Error(400, "Invalid web proxy server");
  }
  auto result = to_lower(r_ascii_host.ok());
  if (result.size() > 253) {
    return Status::Error(400, "Invalid web proxy server");
  }
  auto labels = full_split(Slice(result), '.');
  if (labels.size() < 2) {
    return Status::Error(400, "Invalid web proxy server");
  }
  for (auto label : labels) {
    if (label.empty() || label.size() > 63 || label[0] == '-' || label.back() == '-') {
      return Status::Error(400, "Invalid web proxy server");
    }
    for (auto c : label) {
      if (!is_alnum(c) && c != '-') {
        return Status::Error(400, "Invalid web proxy server");
      }
    }
  }

  // the WHATWG URL "ends in a number" rule: such a host is an IPv4 address, possibly in a shorthand like 127.1 or 0x7f.1
  Slice last_label = labels.back();
  bool is_hex = begins_with(last_label, "0x");
  if (is_hex) {
    last_label.remove_prefix(2);
  }
  bool is_number = true;
  for (auto c : last_label) {
    if (!(is_hex ? is_hex_digit(c) : is_digit(c))) {
      is_number = false;
      break;
    }
  }
  if (is_number) {
    return Status::Error(400, "Web proxy server must be a domain name");
  }
  return std::move(result);
}

bool Proxy::is_valid_web_proxy_path(Slice path) {
  if (path.size() > 128) {
    return false;
  }
  bool is_segment_start = true;
  for (auto c : path) {
    if (c == '/') {
      if (is_segment_start) {
        return false;
      }
      is_segment_start = true;
      continue;
    }
    if (!is_alnum(c) && (is_segment_start || (c != '-' && c != '_'))) {
      return false;
    }
    is_segment_start = false;
  }
  return path.empty() || !is_segment_start;
}

Result<std::pair<string, string>> Proxy::parse_web_proxy_address(Slice address) {
  address = trim(address);
  Slice scheme("https://");
  if (address.size() >= scheme.size() && to_lower(address.substr(0, scheme.size())) == scheme) {
    address.remove_prefix(scheme.size());
  }
  Slice host = address;
  Slice path;
  auto slash_pos = address.find('/');
  if (slash_pos != Slice::npos) {
    host = address.substr(0, slash_pos);
    path = address.substr(slash_pos + 1);
  }
  TRY_RESULT(normalized_host, normalize_web_proxy_host(host.str()));
  if (!is_valid_web_proxy_path(path)) {
    return Status::Error(400, "Invalid web proxy path");
  }
  return std::make_pair(std::move(normalized_host), path.str());
}

Result<mtproto::ProxySecret> Proxy::get_web_proxy_secret(Slice encoded_secret) {
  TRY_RESULT(secret, mtproto::ProxySecret::from_link(encoded_secret));
  if (secret.emulate_tls()) {
    // the relay forwards raw obfuscated bytes, so the MTProxy would wait for a TLS handshake that never comes
    return Status::Error(400, "Web proxies don't support fake TLS secrets");
  }
  return std::move(secret);
}

Result<mtproto::ProxySecret> Proxy::get_web_proxy_link_secret(Slice link_secret, bool has_path) {
  auto r_marked = base64url_decode(link_secret);
  if (r_marked.is_ok() && r_marked.ok().size() >= 17 &&
      static_cast<unsigned char>(r_marked.ok()[0]) == WEB_PROXY_LINK_SECRET_MARKER &&
      Slice(base64url_encode(r_marked.ok())) == link_secret) {
    auto r_secret = mtproto::ProxySecret::from_binary(Slice(r_marked.ok()).substr(1));
    if (r_secret.is_ok() && !r_secret.ok().emulate_tls()) {
      return r_secret.move_as_ok();
    }
  }
  if (has_path) {
    return Status::Error(400, "Wrong proxy secret");
  }
  return get_web_proxy_secret(link_secret);
}

string Proxy::get_web_proxy_address() const {
  CHECK(type_ == Type::Web);
  if (path_.empty()) {
    return server_;
  }
  return PSTRING() << server_ << '/' << path_;
}

string Proxy::get_web_proxy_link_secret() const {
  CHECK(type_ == Type::Web);
  if (path_.empty()) {
    return secret_.get_encoded_secret();
  }
  string marked(1, static_cast<char>(WEB_PROXY_LINK_SECRET_MARKER));
  marked += secret_.get_raw_secret().str();
  return base64url_encode(marked);
}

string Proxy::get_web_proxy_bridge_capability() const {
  CHECK(type_ == Type::Web);
  // the "tdesktop-" prefix is part of the contract: the deployed relay checks these exact bytes
  string context;
  if (path_.empty()) {
    context = PSTRING() << "tdesktop-web-proxy-bridge-v1\n" << server_;
  } else {
    context = PSTRING() << "tdesktop-web-proxy-bridge-v2\n" << server_ << '\n' << path_;
  }
  string hash(32, '\0');
  hmac_sha256(secret_.get_raw_secret(), context, hash);
  return base64url_encode(hash);
}

Result<Proxy> Proxy::create_proxy(string server, int port, const td_api::ProxyType *proxy_type) {
  if (proxy_type == nullptr) {
    return Status::Error(400, "Proxy type must be non-empty");
  }
  if (proxy_type->get_id() == td_api::proxyTypeWeb::ID) {
    auto type = static_cast<const td_api::proxyTypeWeb *>(proxy_type);
    if (port != 0 && port != WEB_PROXY_PORT) {
      return Status::Error(400, "Web proxy port must be 443");
    }
    TRY_RESULT(host, normalize_web_proxy_host(server));
    if (!is_valid_web_proxy_path(type->path_)) {
      return Status::Error(400, "Invalid web proxy path");
    }
    TRY_RESULT(secret, get_web_proxy_secret(type->secret_));
    return Proxy::web(std::move(host), type->path_, std::move(secret));
  }
  if (server.empty()) {
    return Status::Error(400, "Server name must be non-empty");
  }
  if (server.size() > 255) {
    return Status::Error(400, "Server name is too long");
  }
  if (!check_utf8(server)) {
    return Status::Error(400, "Server name must be encoded in UTF-8");
  }
  if (port <= 0 || port > 65535) {
    return Status::Error(400, "Wrong port number");
  }

  switch (proxy_type->get_id()) {
    case td_api::proxyTypeSocks5::ID: {
      auto type = static_cast<const td_api::proxyTypeSocks5 *>(proxy_type);
      return Proxy::socks5(std::move(server), port, type->username_, type->password_);
    }
    case td_api::proxyTypeHttp::ID: {
      auto type = static_cast<const td_api::proxyTypeHttp *>(proxy_type);
      if (type->http_only_) {
        return Proxy::http_caching(std::move(server), port, type->username_, type->password_);
      } else {
        return Proxy::http_tcp(std::move(server), port, type->username_, type->password_);
      }
    }
    case td_api::proxyTypeMtproto::ID: {
      auto type = static_cast<const td_api::proxyTypeMtproto *>(proxy_type);
      TRY_RESULT(secret, mtproto::ProxySecret::from_link(type->secret_));
      return Proxy::mtproto(std::move(server), port, std::move(secret));
    }
    default:
      UNREACHABLE();
      return Status::Error(400, "Wrong proxy type");
  }
}

Result<Proxy> Proxy::create_proxy(const td_api::proxy *proxy) {
  if (proxy == nullptr) {
    return Status::Error(400, "Proxy must be non-empty");
  }
  return create_proxy(proxy->server_, proxy->port_, proxy->type_.get());
}

td_api::object_ptr<td_api::proxy> Proxy::get_proxy_object() const {
  auto type = [&]() -> td_api::object_ptr<td_api::ProxyType> {
    switch (type_) {
      case Type::Socks5:
        return td_api::make_object<td_api::proxyTypeSocks5>(user_, password_);
      case Type::HttpTcp:
        return td_api::make_object<td_api::proxyTypeHttp>(user_, password_, false);
      case Type::HttpCaching:
        return td_api::make_object<td_api::proxyTypeHttp>(user_, password_, true);
      case Type::Mtproto:
        return td_api::make_object<td_api::proxyTypeMtproto>(secret_.get_encoded_secret());
      case Type::Web:
        return td_api::make_object<td_api::proxyTypeWeb>(path_, secret_.get_encoded_secret());
      default:
        UNREACHABLE();
        return nullptr;
    }
  }();
  return td_api::make_object<td_api::proxy>(server_, port_, std::move(type));
}

StringBuilder &operator<<(StringBuilder &string_builder, const Proxy &proxy) {
  switch (proxy.type()) {
    case Proxy::Type::Socks5:
      return string_builder << "ProxySocks5 " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::HttpTcp:
      return string_builder << "ProxyHttpTcp " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::HttpCaching:
      return string_builder << "ProxyHttpCaching " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::Mtproto:
      return string_builder << "ProxyMtproto " << proxy.server() << ":" << proxy.port() << "/"
                            << proxy.secret().get_encoded_secret();
    case Proxy::Type::Web:
      // neither the secret nor the derived capability may be logged
      return string_builder << "ProxyWeb " << proxy.get_web_proxy_address();
    case Proxy::Type::None:
      return string_builder << "ProxyEmpty";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
