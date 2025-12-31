//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class AgeVerificationParameters {
  bool need_verification_ = false;
  string bot_username_;
  string country_;
  int32 min_age_ = 0;

  friend bool operator==(const AgeVerificationParameters &lhs, const AgeVerificationParameters &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const AgeVerificationParameters &parameters);

 public:
  AgeVerificationParameters() = default;

  AgeVerificationParameters(bool need_verification, string bot_username, string country, int32 min_age);

  td_api::object_ptr<td_api::ageVerificationParameters> get_age_verification_parameters_object() const;

  bool need_verification() const {
    return need_verification_;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const AgeVerificationParameters &lhs, const AgeVerificationParameters &rhs);

inline bool operator!=(const AgeVerificationParameters &lhs, const AgeVerificationParameters &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const AgeVerificationParameters &parameters);

}  // namespace td
