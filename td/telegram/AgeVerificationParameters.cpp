//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AgeVerificationParameters.h"

#include "td/utils/logging.h"

namespace td {

AgeVerificationParameters::AgeVerificationParameters(bool need_verification, string bot_username, string country,
                                                     int32 min_age)
    : need_verification_(need_verification)
    , bot_username_(std::move(bot_username))
    , country_(std::move(country))
    , min_age_(min_age) {
  if (need_verification_) {
    if (bot_username_.empty() || country_.empty() || min_age_ <= 0) {
      LOG(ERROR) << "Receive invalid age verification parameters: " << *this;
    }
  } else {
    if (!bot_username_.empty() || !country_.empty() || min_age_ != 0) {
      LOG(ERROR) << "Receive unneeded age verification parameters: " << min_age_ << ' ' << country_ << ' '
                 << bot_username_;
    }
  }
}

td_api::object_ptr<td_api::ageVerificationParameters>
AgeVerificationParameters::get_age_verification_parameters_object() const {
  if (!need_verification_) {
    return nullptr;
  }
  return td_api::make_object<td_api::ageVerificationParameters>(min_age_, bot_username_, country_);
}

bool operator==(const AgeVerificationParameters &lhs, const AgeVerificationParameters &rhs) {
  return lhs.need_verification_ == rhs.need_verification_ && lhs.bot_username_ == rhs.bot_username_ &&
         lhs.country_ == rhs.country_ && lhs.min_age_ == rhs.min_age_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const AgeVerificationParameters &parameters) {
  if (!parameters.need_verification_) {
    return string_builder << "[no age verification]";
  }
  return string_builder << "verify age of " << parameters.min_age_ << " years for country " << parameters.country_
                        << " via bot @" << parameters.bot_username_;
}

}  // namespace td
