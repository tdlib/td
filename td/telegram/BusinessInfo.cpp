//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessInfo.h"

namespace td {

td_api::object_ptr<td_api::businessInfo> BusinessInfo::get_business_info_object() const {
  return td_api::make_object<td_api::businessInfo>(location_.get_business_location_object(),
                                                   work_hours_.get_business_work_hours_object());
}

bool BusinessInfo::is_empty_location(const DialogLocation &location) {
  return location.empty() && location.get_address().empty();
}

bool BusinessInfo::is_empty() const {
  return is_empty_location(location_) && work_hours_.is_empty();
}

bool BusinessInfo::set_location(unique_ptr<BusinessInfo> &business_info, DialogLocation &&location) {
  if (business_info == nullptr) {
    if (is_empty_location(location)) {
      return false;
    }
    business_info = make_unique<BusinessInfo>();
  }
  if (business_info->location_ != location) {
    business_info->location_ = std::move(location);
    return true;
  }
  return false;
}

bool BusinessInfo::set_work_hours(unique_ptr<BusinessInfo> &business_info, BusinessWorkHours &&work_hours) {
  if (business_info == nullptr) {
    if (work_hours.is_empty()) {
      return false;
    }
    business_info = make_unique<BusinessInfo>();
  }
  if (business_info->work_hours_ != work_hours) {
    business_info->work_hours_ = std::move(work_hours);
    return true;
  }
  return false;
}

}  // namespace td
