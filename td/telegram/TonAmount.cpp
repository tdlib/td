//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonAmount.h"

#include "td/utils/logging.h"

namespace td {

TonAmount::TonAmount(telegram_api::object_ptr<telegram_api::starsTonAmount> &&amount, bool allow_negative) {
  if (amount != nullptr) {
    ton_amount_ = get_ton_count(amount->amount_, allow_negative);
  }
}

bool operator==(const TonAmount &lhs, const TonAmount &rhs) {
  return lhs.ton_amount_ == rhs.ton_amount_;
}

int64 TonAmount::get_ton_count(int64 amount, bool allow_negative) {
  auto max_amount = static_cast<int64>(1) << 51;
  if (amount < 0) {
    if (!allow_negative) {
      LOG(ERROR) << "Receive TON amount = " << amount;
      return 0;
    }
    if (amount < -max_amount) {
      LOG(ERROR) << "Receive TON amount = " << amount;
      return -max_amount;
    }
  } else if (amount > max_amount) {
    LOG(ERROR) << "Receive TON amount = " << amount;
    return max_amount;
  }
  return amount;
}

StringBuilder &operator<<(StringBuilder &string_builder, const TonAmount &ton_amount) {
  return string_builder << ton_amount.get_ton_amount() << " TON";
}

}  // namespace td
