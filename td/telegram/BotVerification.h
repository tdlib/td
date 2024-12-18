//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BotVerifierSettings.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class BotVerification {
 public:
  BotVerification() = default;

  explicit BotVerification(telegram_api::object_ptr<telegram_api::botVerification> &&bot_verification);

  static unique_ptr<BotVerification> get_bot_verification(
      telegram_api::object_ptr<telegram_api::botVerification> &&bot_verification);

  td_api::object_ptr<td_api::botVerification> get_bot_verification_object(Td *td) const;

  bool is_valid() const {
    return bot_user_id_.is_valid() && settings_.is_valid();
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);

 private:
  UserId bot_user_id_;
  BotVerifierSettings settings_;

  friend bool operator==(const BotVerification &lhs, const BotVerification &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const BotVerification &bot_verification);
};

bool operator==(const BotVerification &lhs, const BotVerification &rhs);

bool operator==(const unique_ptr<BotVerification> &lhs, const unique_ptr<BotVerification> &rhs);

inline bool operator!=(const unique_ptr<BotVerification> &lhs, const unique_ptr<BotVerification> &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const BotVerification &bot_verification);

}  // namespace td
