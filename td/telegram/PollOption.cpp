//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PollOption.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageCopyOptions.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

namespace td {

PollOption::PollOption(FormattedText &&text, unique_ptr<MessageContent> &&media)
    : text_(std::move(text)), media_(std::move(media)) {
  keep_only_custom_emoji(text_);
}

PollOption::PollOption(Td *td, telegram_api::object_ptr<telegram_api::PollAnswer> &&poll_answer_ptr,
                       vector<std::pair<ChannelId, MinChannel>> &min_channels) {
  if (poll_answer_ptr->get_id() != telegram_api::pollAnswer::ID) {
    LOG(ERROR) << "Receive " << to_string(poll_answer_ptr);
    return;
  }
  auto poll_answer = telegram_api::move_object_as<telegram_api::pollAnswer>(poll_answer_ptr);
  text_ = get_formatted_text(nullptr, std::move(poll_answer->text_), true, true, "PollOption");
  keep_only_custom_emoji(text_);
  if (poll_answer->media_ != nullptr) {
    media_ = get_message_content(td, FormattedText(), std::move(poll_answer->media_), DialogId(), 0, false, UserId(),
                                 nullptr, nullptr, "pollAnswer");
    if (!is_allowed_poll_option_content(media_->get_type())) {
      LOG(ERROR) << "Receive " << media_->get_type() << " in a poll option";
      media_ = nullptr;
    }
  }
  data_ = poll_answer->option_.as_slice().str();
  if (poll_answer->added_by_ != nullptr) {
    added_by_dialog_id_ = DialogId(poll_answer->added_by_);
    added_date_ = poll_answer->date_;
    if (!check_min_message_sender(td, added_by_dialog_id_, min_channels) || added_date_ <= 0) {
      LOG(ERROR) << "Receive " << to_string(poll_answer);
      added_by_dialog_id_ = DialogId();
      added_date_ = 0;
    }
  }
}

Result<PollOption> PollOption::get_poll_option(Td *td, DialogId dialog_id,
                                               td_api::object_ptr<td_api::inputPollOption> &&input_poll_option) {
  if (input_poll_option == nullptr) {
    return Status::Error(400, "Poll option must be non-empty");
  }
  TRY_RESULT(text, get_formatted_text(td, dialog_id, std::move(input_poll_option->text_), td->auth_manager_->is_bot(),
                                      false, true, false));
  constexpr size_t MAX_POLL_OPTION_LENGTH = 100;  // server-side limit
  if (utf8_length(text.text) > MAX_POLL_OPTION_LENGTH) {
    return Status::Error(400, PSLICE() << "Poll options length must not exceed " << MAX_POLL_OPTION_LENGTH);
  }
  unique_ptr<MessageContent> media;
  if (input_poll_option->media_ != nullptr) {
    TRY_RESULT(media_content, get_input_message_content(dialog_id, std::move(input_poll_option->media_), td, false));
    if (!is_allowed_poll_option_content(media_content.content->get_type())) {
      return Status::Error(400, "Invalid media content specified");
    }
    const auto *caption = get_message_content_caption(media_content.content.get());
    if (caption != nullptr && !caption->text.empty()) {
      return Status::Error(400, "Media caption must be empty");
    }
    if (!media_content.ttl.is_empty() || media_content.via_bot_user_id != UserId()) {
      return Status::Error(400, "Unallowed media parameters specified");
    }
    media = std::move(media_content.content);
  }

  return PollOption(std::move(text), std::move(media));
}

Result<vector<PollOption>> PollOption::get_poll_options(
    Td *td, DialogId dialog_id, vector<td_api::object_ptr<td_api::inputPollOption>> &&input_poll_options) {
  if (input_poll_options.empty()) {
    return Status::Error(400, "Poll must have at least one answer option");
  }
  auto max_poll_options = td->option_manager_->get_option_integer("poll_answer_count_max", 12);
  if (static_cast<int64>(input_poll_options.size()) > max_poll_options) {
    return Status::Error(400, PSLICE() << "Poll can't have more than " << max_poll_options << " options");
  }
  vector<PollOption> options;
  for (auto &input_option : input_poll_options) {
    TRY_RESULT(option, get_poll_option(td, dialog_id, std::move(input_option)));
    options.push_back(std::move(option));
  }
  return std::move(options);
}

PollOption PollOption::dup_option(Td *td, DialogId dialog_id) const {
  PollOption result;
  result.text_ = text_;
  remove_unallowed_entities(td, result.text_, dialog_id);
  if (media_ != nullptr) {
    result.media_ =
        dup_message_content(td, dialog_id, media_.get(), MessageContentDupType::Copy, MessageCopyOptions(true, false));
  }
  return result;
}

void PollOption::append_file_ids(const Td *td, vector<FileId> &file_ids) const {
  if (media_ != nullptr) {
    append(file_ids, get_message_content_file_ids(media_.get(), td));
  }
}

td_api::object_ptr<td_api::pollOption> PollOption::get_poll_option_object(Td *td) const {
  auto author = added_by_dialog_id_ == DialogId()
                    ? nullptr
                    : get_min_message_sender_object(td, added_by_dialog_id_, "pollOption");
  bool is_added = author != nullptr;
  vector<td_api::object_ptr<td_api::MessageSender>> recent_voter_ids;
  for (auto dialog_id : recent_voter_dialog_ids_) {
    auto recent_voter_id = get_min_message_sender_object(td, dialog_id, "pollOption recent voter");
    if (recent_voter_id != nullptr) {
      recent_voter_ids.push_back(std::move(recent_voter_id));
    }
  }
  td_api::object_ptr<td_api::MessageContent> media;
  if (media_ != nullptr) {
    media = get_message_content_object(media_.get(), td, DialogId(), MessageId(), DialogId(), false, false, false,
                                       DialogId(), 0, 0, false, true, -1, false, true);
  }
  return td_api::make_object<td_api::pollOption>(data_, get_formatted_text_object(nullptr, text_, true, -1),
                                                 std::move(media), voter_count_, 0, std::move(recent_voter_ids),
                                                 is_chosen_, false, std::move(author), is_added ? added_date_ : 0);
}

telegram_api::object_ptr<telegram_api::PollAnswer> PollOption::get_input_poll_answer(
    telegram_api::object_ptr<telegram_api::InputMedia> &&input_media) const {
  int32 flags = 0;
  if (input_media != nullptr) {
    flags |= telegram_api::inputPollAnswer::MEDIA_MASK;
  }
  return telegram_api::make_object<telegram_api::inputPollAnswer>(
      flags, get_input_text_with_entities(nullptr, text_, "get_input_poll_answer"), std::move(input_media));
}

vector<PollOption> PollOption::get_poll_options(
    Td *td, vector<telegram_api::object_ptr<telegram_api::PollAnswer>> &&poll_answers,
    vector<std::pair<ChannelId, MinChannel>> &min_channels) {
  return transform(std::move(poll_answers),
                   [td, &min_channels](telegram_api::object_ptr<telegram_api::PollAnswer> &&poll_answer_ptr) {
                     return PollOption(td, std::move(poll_answer_ptr), min_channels);
                   });
}

void PollOption::add_dependencies(Dependencies &dependencies, UserId my_user_id, bool is_bot) const {
  dependencies.add_message_sender_dependencies(added_by_dialog_id_);
  for (auto dialog_id : recent_voter_dialog_ids_) {
    dependencies.add_message_sender_dependencies(dialog_id);
  }
  if (media_ != nullptr) {
    add_message_content_dependencies(dependencies, media_.get(), my_user_id, is_bot);
  }
}

bool operator==(const PollOption &lhs, const PollOption &rhs) {
  // don't compare voter_count_, recent_voter_dialog_ids_, and is_chosen_
  // don't need to compare media_, because it can't change without data_ change
  return lhs.text_ == rhs.text_ && lhs.data_ == rhs.data_ && lhs.added_by_dialog_id_ == rhs.added_by_dialog_id_ &&
         lhs.added_date_ == rhs.added_date_;
}

bool operator!=(const PollOption &lhs, const PollOption &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
