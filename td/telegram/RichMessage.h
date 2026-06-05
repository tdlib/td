//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/WebPageBlock.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <functional>

namespace td {

class Dependencies;

class Td;

class RichMessage {
  enum class InputType : int32 { None, Markdown, Html };

  vector<unique_ptr<WebPageBlock>> blocks_;
  bool is_rtl_ = false;
  bool is_full_ = false;

  bool noautolink_ = false;
  InputType input_type_ = InputType::None;
  string source_;

  friend bool operator==(const RichMessage &lhs, const RichMessage &rhs);

 public:
  RichMessage() = default;
  RichMessage(const RichMessage &) = delete;
  RichMessage &operator=(const RichMessage &) = delete;
  RichMessage(RichMessage &&) = default;
  RichMessage &operator=(RichMessage &&) = default;
  ~RichMessage() = default;

  RichMessage(Td *td, telegram_api::object_ptr<telegram_api::richMessage> &&rich_message, DialogId owner_dialog_id);

  static Result<RichMessage> get_rich_message(Td *td, DialogId dialog_id,
                                              td_api::object_ptr<td_api::inputRichMessage> &&message, bool is_bot);

  bool is_full() const {
    return is_full_;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const;

  void add_dependencies(Dependencies &dependencies) const;

  void for_each_text(const std::function<void(Slice text)> &callback) const;

  vector<UserId> get_user_ids() const;

  bool has_bot_commands() const;

  vector<string> get_hashtags() const;

  bool can_send(const RestrictedRights &rights) const;

  int32 get_index_mask() const;

  telegram_api::object_ptr<telegram_api::InputRichMessage> get_input_rich_message(const Td *td) const;

  td_api::object_ptr<td_api::richMessage> get_rich_message_object(Td *td, bool skip_bot_commands) const;

  RichMessage clone() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const RichMessage &lhs, const RichMessage &rhs);

inline bool operator!=(const RichMessage &lhs, const RichMessage &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
