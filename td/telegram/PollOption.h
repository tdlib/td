//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MinChannel.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

#include <utility>

namespace td {

class Dependencies;
class MessageContent;
class Td;

struct PollOption {
  FormattedText text_;
  unique_ptr<MessageContent> media_;
  string data_;
  DialogId added_by_dialog_id_;
  vector<DialogId> recent_voter_dialog_ids_;
  int32 added_date_ = 0;
  int32 voter_count_ = 0;
  bool is_chosen_ = false;

  PollOption() = default;

  PollOption(FormattedText &&text, unique_ptr<MessageContent> &&media, int32 pos);

  PollOption(Td *td, telegram_api::object_ptr<telegram_api::PollAnswer> &&poll_answer_ptr,
             vector<std::pair<ChannelId, MinChannel>> &min_channels);

  const string &get_data() const {
    return data_;
  }

  DialogId get_added_by_dialog_id() const {
    return added_by_dialog_id_;
  }

  int32 get_added_date() const {
    return added_date_;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const;

  td_api::object_ptr<td_api::pollOption> get_poll_option_object(Td *td) const;

  telegram_api::object_ptr<telegram_api::PollAnswer> get_input_poll_answer() const;

  static vector<PollOption> get_poll_options(Td *td,
                                             vector<telegram_api::object_ptr<telegram_api::PollAnswer>> &&poll_answers,
                                             vector<std::pair<ChannelId, MinChannel>> &min_channels);

  void add_dependencies(Dependencies &dependencies, UserId my_user_id, bool is_bot) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const PollOption &lhs, const PollOption &rhs);

bool operator!=(const PollOption &lhs, const PollOption &rhs);

}  // namespace td
