//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/FormattedDate.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void FormattedDate::store(StorerT &storer) const {
  td::store(date_, storer);
  td::store(date_flags_, storer);
}

template <class ParserT>
void FormattedDate::parse(ParserT &parser) {
  td::parse(date_, parser);
  td::parse(date_flags_, parser);
}

}  // namespace td
