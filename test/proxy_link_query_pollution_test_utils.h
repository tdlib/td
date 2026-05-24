// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/LinkManager.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td::proxy_link_query_pollution_test {

inline td_api::object_ptr<td_api::internalLinkTypeProxy> parse_proxy_link(Slice url) {
  auto parsed = LinkManager::parse_internal_link(url);
  if (parsed == nullptr) {
    return nullptr;
  }
  auto object = parsed->get_internal_link_type_object();
  if (object == nullptr || object->get_id() != td_api::internalLinkTypeProxy::ID) {
    return nullptr;
  }
  return td_api::move_object_as<td_api::internalLinkTypeProxy>(std::move(object));
}

inline bool is_unsupported_proxy(Slice url) {
  auto proxy_link = parse_proxy_link(url);
  return proxy_link == nullptr || proxy_link->proxy_ == nullptr;
}

inline bool is_mtproto_proxy(Slice url, Slice expected_server, int32 expected_port, Slice expected_secret) {
  auto proxy_link = parse_proxy_link(url);
  if (proxy_link == nullptr || proxy_link->proxy_ == nullptr) {
    return false;
  }
  const auto &proxy = proxy_link->proxy_;
  if (proxy->server_ != expected_server || proxy->port_ != expected_port) {
    return false;
  }
  if (proxy->type_ == nullptr || proxy->type_->get_id() != td_api::proxyTypeMtproto::ID) {
    return false;
  }
  auto *mtproto_type = static_cast<const td_api::proxyTypeMtproto *>(proxy->type_.get());
  return mtproto_type->secret_ == expected_secret;
}

inline bool is_socks5_proxy(Slice url, Slice expected_server, int32 expected_port, Slice expected_username,
                            Slice expected_password) {
  auto proxy_link = parse_proxy_link(url);
  if (proxy_link == nullptr || proxy_link->proxy_ == nullptr) {
    return false;
  }
  const auto &proxy = proxy_link->proxy_;
  if (proxy->server_ != expected_server || proxy->port_ != expected_port) {
    return false;
  }
  if (proxy->type_ == nullptr || proxy->type_->get_id() != td_api::proxyTypeSocks5::ID) {
    return false;
  }
  auto *socks5_type = static_cast<const td_api::proxyTypeSocks5 *>(proxy->type_.get());
  return socks5_type->username_ == expected_username && socks5_type->password_ == expected_password;
}

}  // namespace td::proxy_link_query_pollution_test
