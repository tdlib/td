//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReferralProgramManager.h"

#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

namespace td {

ReferralProgramManager::SuggestedBotStarRef::SuggestedBotStarRef(
    telegram_api::object_ptr<telegram_api::starRefProgram> &&ref)
    : user_id_(ref->bot_id_), parameters_(ref->commission_permille_, ref->duration_months_) {
}

td_api::object_ptr<td_api::foundAffiliateProgram>
ReferralProgramManager::SuggestedBotStarRef::get_found_affiliate_program_object(Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::foundAffiliateProgram>(
      td->user_manager_->get_user_id_object(user_id_, "foundAffiliateProgram"),
      parameters_.get_affiliate_program_parameters_object());
}

ReferralProgramManager::ReferralProgramManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void ReferralProgramManager::tear_down() {
  parent_.reset();
}

}  // namespace td
