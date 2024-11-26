//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReferralProgramInfo.h"

#include "td/telegram/StarManager.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/misc.h"

namespace td {

ReferralProgramInfo::ReferralProgramInfo(telegram_api::object_ptr<telegram_api::starRefProgram> &&program) {
  if (program != nullptr) {
    parameters_ = ReferralProgramParameters(program->commission_permille_, program->duration_months_);
    end_date_ = program->end_date_;
    if (program->daily_revenue_per_user_ != nullptr) {
      daily_star_count_ = StarManager::get_star_count(program->daily_revenue_per_user_->amount_, true);
      daily_nanostar_count_ =
          StarManager::get_nanostar_count(daily_star_count_, program->daily_revenue_per_user_->nanos_);
    }
  }
}

td_api::object_ptr<td_api::affiliateProgramInfo> ReferralProgramInfo::get_affiliate_program_info_object() const {
  CHECK(is_valid());
  return td_api::make_object<td_api::affiliateProgramInfo>(parameters_.get_affiliate_program_parameters_object(),
                                                           end_date_, daily_star_count_, daily_nanostar_count_);
}

bool operator==(const ReferralProgramInfo &lhs, const ReferralProgramInfo &rhs) {
  return lhs.parameters_ == rhs.parameters_ && lhs.end_date_ == rhs.end_date_ &&
         lhs.daily_star_count_ == rhs.daily_star_count_ && lhs.daily_nanostar_count_ == rhs.daily_nanostar_count_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReferralProgramInfo &info) {
  string_builder << '[' << info.parameters_;
  if (info.end_date_) {
    string_builder << " ending at " << info.end_date_;
  }
  if (info.daily_star_count_ != 0 || info.daily_nanostar_count_ != 0) {
    string_builder << " with profit of " << info.daily_star_count_ << '.'
                   << lpad0(to_string(info.daily_nanostar_count_), 9);
  }
  return string_builder << ']';
}

}  // namespace td
