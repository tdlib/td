// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/LinkManager.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <cctype>

namespace td::managed_bot_link_test {

inline unique_ptr<LinkManager::InternalLink> parse_link(Slice url) {
  return LinkManager::parse_internal_link(url);
}

inline td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object(
    unique_ptr<LinkManager::InternalLink> link) {
  CHECK(link != nullptr);
  return link->get_internal_link_type_object();
}

inline td_api::object_ptr<td_api::internalLinkTypeRequestManagedBot> parse_request_managed_bot_link(Slice url) {
  auto link = parse_link(url);
  CHECK(link != nullptr);
  auto object = link->get_internal_link_type_object();
  CHECK(object != nullptr);
  CHECK(object->get_id() == td_api::internalLinkTypeRequestManagedBot::ID);
  return td_api::move_object_as<td_api::internalLinkTypeRequestManagedBot>(std::move(object));
}

inline Result<string> build_request_managed_bot_link(Slice manager_bot_username, Slice suggested_bot_username,
                                                     Slice suggested_bot_name, bool is_internal) {
  return LinkManager::get_internal_link(
      td_api::make_object<td_api::internalLinkTypeRequestManagedBot>(
          manager_bot_username.str(), suggested_bot_username.str(), suggested_bot_name.str()),
      is_internal);
}

inline bool ends_with_bot_case_insensitive(Slice username) {
  if (username.size() < 3) {
    return false;
  }
  return std::tolower(static_cast<unsigned char>(username[username.size() - 3])) == 'b' &&
         std::tolower(static_cast<unsigned char>(username[username.size() - 2])) == 'o' &&
         std::tolower(static_cast<unsigned char>(username[username.size() - 1])) == 't';
}

}  // namespace td::managed_bot_link_test