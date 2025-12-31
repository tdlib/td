//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TonAmount.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void TonAmount::store(StorerT &storer) const {
  td::store(ton_amount_, storer);
}

template <class ParserT>
void TonAmount::parse(ParserT &parser) {
  td::parse(ton_amount_, parser);
}

}  // namespace td
