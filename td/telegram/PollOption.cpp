//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PollOption.h"

#include "td/telegram/MessageSender.h"

#include "td/utils/algorithm.h"

namespace td {

PollOption::PollOption(FormattedText &&text, int32 pos) : text_(std::move(text)) {
  if (pos < 10) {
    data_ = string(1, static_cast<char>(pos + '0'));
  } else {
    data_ = string(2, static_cast<char>(pos - 10 + '0'));
    data_[0] = static_cast<char>('0' + pos / 10);
  }
}

td_api::object_ptr<td_api::pollOption> PollOption::get_poll_option_object(Td *td) const {
  return td_api::make_object<td_api::pollOption>(
      get_formatted_text_object(nullptr, text_, true, -1), voter_count_, 0, is_chosen_, false,
      added_by_dialog_id_ == DialogId() ? nullptr : get_message_sender_object(td, added_by_dialog_id_, "pollOption"),
      added_date_);
}

telegram_api::object_ptr<telegram_api::PollAnswer> PollOption::get_input_poll_option() const {
  return telegram_api::make_object<telegram_api::inputPollAnswer>(
      0, get_input_text_with_entities(nullptr, text_, "get_input_poll_option"), nullptr);
}

vector<PollOption> PollOption::get_poll_options(
    vector<telegram_api::object_ptr<telegram_api::PollAnswer>> &&poll_options) {
  return transform(std::move(poll_options), [](telegram_api::object_ptr<telegram_api::PollAnswer> &&poll_option_ptr) {
    PollOption option;
    if (poll_option_ptr->get_id() == telegram_api::pollAnswer::ID) {
      auto poll_option = telegram_api::move_object_as<telegram_api::pollAnswer>(poll_option_ptr);
      option.text_ = get_formatted_text(nullptr, std::move(poll_option->text_), true, true, "get_poll_options");
      keep_only_custom_emoji(option.text_);
      option.data_ = poll_option->option_.as_slice().str();
      if (poll_option->added_by_ != nullptr) {
        option.added_by_dialog_id_ = DialogId(poll_option->added_by_);
        option.added_date_ = poll_option->date_;
        if (!option.added_by_dialog_id_.is_valid() || option.added_date_ <= 0) {
          LOG(ERROR) << "Receive " << to_string(poll_option);
          option.added_by_dialog_id_ = DialogId();
          option.added_date_ = 0;
        }
      }
    }
    return option;
  });
}

bool operator==(const PollOption &lhs, const PollOption &rhs) {
  // don't compare voter_count_ and is_chosen_
  return lhs.text_ == rhs.text_ && lhs.data_ == rhs.data_ && lhs.added_by_dialog_id_ == rhs.added_by_dialog_id_ &&
         lhs.added_date_ == rhs.added_date_;
}

bool operator!=(const PollOption &lhs, const PollOption &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
