//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AgeVerificationParameters.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void AgeVerificationParameters::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(need_verification_);
  END_STORE_FLAGS();
  td::store(bot_username_, storer);
  td::store(country_, storer);
  td::store(min_age_, storer);
}

template <class ParserT>
void AgeVerificationParameters::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(need_verification_);
  END_PARSE_FLAGS();
  td::parse(bot_username_, parser);
  td::parse(country_, parser);
  td::parse(min_age_, parser);
}

}  // namespace td
