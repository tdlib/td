//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

struct PollOption {
  FormattedText text_;
  string data_;
  DialogId added_by_dialog_id_;
  int32 added_date_ = 0;
  int32 voter_count_ = 0;
  bool is_chosen_ = false;

  PollOption() = default;

  PollOption(FormattedText &&text, int32 pos);

  td_api::object_ptr<td_api::pollOption> get_poll_option_object(Td *td) const;

  telegram_api::object_ptr<telegram_api::PollAnswer> get_input_poll_option() const;

  static vector<PollOption> get_poll_options(vector<telegram_api::object_ptr<telegram_api::PollAnswer>> &&poll_options);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const PollOption &lhs, const PollOption &rhs);

bool operator!=(const PollOption &lhs, const PollOption &rhs);

}  // namespace td
