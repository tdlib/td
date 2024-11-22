//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ReferralProgramParameters.h"
#include "td/telegram/ReferralProgramSortOrder.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Td;

class ReferralProgramManager final : public Actor {
 public:
  ReferralProgramManager(Td *td, ActorShared<> parent);

  void search_referral_programs(DialogId dialog_id, ReferralProgramSortOrder sort_order, const string &offset,
                                int32 limit, Promise<td_api::object_ptr<td_api::foundAffiliatePrograms>> &&promise);

 private:
  class GetSuggestedStarRefBotsQuery;

  class SuggestedBotStarRef {
    UserId user_id_;
    ReferralProgramParameters parameters_;

   public:
    explicit SuggestedBotStarRef(telegram_api::object_ptr<telegram_api::starRefProgram> &&ref);

    bool is_valid() const {
      return user_id_.is_valid() && parameters_.is_valid();
    }

    td_api::object_ptr<td_api::foundAffiliateProgram> get_found_affiliate_program_object(Td *td) const;
  };

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
