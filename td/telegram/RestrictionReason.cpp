//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RestrictionReason.h"

#include "td/telegram/Global.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/misc.h"

#include <tuple>

namespace td {

const RestrictionReason *get_restriction_reason(const vector<RestrictionReason> &restriction_reasons, bool sensitive) {
  if (restriction_reasons.empty()) {
    return nullptr;
  }

  auto ignored_restriction_reasons = full_split(G()->get_option_string("ignored_restriction_reasons"), ',');
  auto restriction_add_platforms = full_split(G()->get_option_string("restriction_add_platforms"), ',');
  auto platform = [] {
#if TD_ANDROID
    return Slice("android");
#elif TD_WINDOWS
    return Slice("ms");
#elif TD_DARWIN
    return Slice("ios");
#else
    return Slice();
#endif
  }();

  if (G()->get_option_boolean("ignore_platform_restrictions")) {
    platform = Slice();
    restriction_add_platforms.clear();
  }

  if (!platform.empty()) {
    // first find restriction for the current platform
    for (auto &restriction_reason : restriction_reasons) {
      if (restriction_reason.platform_ == platform &&
          !td::contains(ignored_restriction_reasons, restriction_reason.reason_) &&
          restriction_reason.is_sensitive() == sensitive) {
        return &restriction_reason;
      }
    }
  }

  if (!restriction_add_platforms.empty()) {
    // then find restriction for added platforms
    for (auto &restriction_reason : restriction_reasons) {
      if (td::contains(restriction_add_platforms, restriction_reason.platform_) &&
          !td::contains(ignored_restriction_reasons, restriction_reason.reason_) &&
          restriction_reason.is_sensitive() == sensitive) {
        return &restriction_reason;
      }
    }
  }

  // then find restriction for all platforms
  for (auto &restriction_reason : restriction_reasons) {
    if (restriction_reason.platform_ == "all" &&
        !td::contains(ignored_restriction_reasons, restriction_reason.reason_) &&
        restriction_reason.is_sensitive() == sensitive) {
      return &restriction_reason;
    }
  }

  return nullptr;
}

td_api::object_ptr<td_api::restrictionInfo> get_restriction_info_object(
    const vector<RestrictionReason> &restriction_reasons) {
  auto has_sensitive_content = get_restriction_reason(restriction_reasons, true) != nullptr;
  const auto *restriction_reason = get_restriction_reason(restriction_reasons, false);
  if (restriction_reason == nullptr) {
    if (has_sensitive_content) {
      return td_api::make_object<td_api::restrictionInfo>(string(), true);
    }
    return nullptr;
  }
  return td_api::make_object<td_api::restrictionInfo>(restriction_reason->description_, has_sensitive_content);
}

vector<RestrictionReason> get_restriction_reasons(Slice legacy_restriction_reason) {
  Slice type;
  Slice description;
  std::tie(type, description) = split(legacy_restriction_reason, ':');
  auto parts = full_split(type, '-');
  description = trim(description);

  vector<RestrictionReason> result;
  if (parts.size() <= 1) {
    return result;
  }
  for (size_t i = 1; i < parts.size(); i++) {
    result.emplace_back(parts[i].str(), parts[0].str(), description.str());
  }
  return result;
}

vector<RestrictionReason> get_restriction_reasons(
    vector<telegram_api::object_ptr<telegram_api::restrictionReason>> &&restriction_reasons) {
  return transform(std::move(restriction_reasons),
                   [](telegram_api::object_ptr<telegram_api::restrictionReason> &&restriction_reason) {
                     return RestrictionReason(std::move(restriction_reason->platform_),
                                              std::move(restriction_reason->reason_),
                                              std::move(restriction_reason->text_));
                   });
}

}  // namespace td
