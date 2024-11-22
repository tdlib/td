//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class ReferralProgramParameters {
  int32 commission_ = 0;
  int32 month_count_ = 0;

  friend bool operator==(const ReferralProgramParameters &lhs, const ReferralProgramParameters &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const ReferralProgramParameters &parameters);

 public:
  ReferralProgramParameters() = default;

  ReferralProgramParameters(int32 commission_permille, int32 duration_months);

  bool is_valid() const {
    return 1 <= commission_ && commission_ <= 999 && 0 <= month_count_ && month_count_ <= 36;
  }

  td_api::object_ptr<td_api::affiliateProgramParameters> get_affiliate_program_parameters_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ReferralProgramParameters &lhs, const ReferralProgramParameters &rhs);

inline bool operator!=(const ReferralProgramParameters &lhs, const ReferralProgramParameters &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReferralProgramParameters &parameters);

}  // namespace td
