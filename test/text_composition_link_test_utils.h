// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/LinkManager.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td::text_composition_link_test {

inline unique_ptr<LinkManager::InternalLink> parse_link(Slice url) {
  return LinkManager::parse_internal_link(url);
}

inline td_api::object_ptr<td_api::InternalLinkType> parse_object(Slice url) {
  auto parsed = parse_link(url);
  if (parsed == nullptr) {
    return nullptr;
  }
  return parsed->get_internal_link_type_object();
}

inline td_api::object_ptr<td_api::internalLinkTypeTextCompositionStyle> parse_text_composition_style_link(Slice url) {
  auto object = parse_object(url);
  if (object == nullptr || object->get_id() != td_api::internalLinkTypeTextCompositionStyle::ID) {
    return nullptr;
  }
  return td_api::move_object_as<td_api::internalLinkTypeTextCompositionStyle>(std::move(object));
}

inline td_api::object_ptr<td_api::internalLinkTypeUnknownDeepLink> parse_unknown_deep_link(Slice url) {
  auto object = parse_object(url);
  if (object == nullptr || object->get_id() != td_api::internalLinkTypeUnknownDeepLink::ID) {
    return nullptr;
  }
  return td_api::move_object_as<td_api::internalLinkTypeUnknownDeepLink>(std::move(object));
}

inline Result<string> build_text_composition_style_link(Slice style_name, bool is_internal) {
  return LinkManager::get_internal_link(
      td_api::make_object<td_api::internalLinkTypeTextCompositionStyle>(style_name.str()), is_internal);
}

inline bool is_text_composition_style_link(Slice url, Slice expected_style_name) {
  auto style_link = parse_text_composition_style_link(url);
  return style_link != nullptr && style_link->style_name_ == expected_style_name;
}

inline bool is_unknown_deep_link(Slice url) {
  return parse_unknown_deep_link(url) != nullptr;
}

}  // namespace td::text_composition_link_test
