//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/WebPageBlock.h"

#include "td/utils/common.h"

#include <functional>

namespace td {

class Dependencies;

class Td;

class RichMessage {
  vector<unique_ptr<WebPageBlock>> blocks_;
  bool is_rtl_ = false;
  bool is_full_ = false;

  friend bool operator==(const RichMessage &lhs, const RichMessage &rhs);

 public:
  RichMessage() = default;
  RichMessage(const RichMessage &) = delete;
  RichMessage &operator=(const RichMessage &) = delete;
  RichMessage(RichMessage &&) = default;
  RichMessage &operator=(RichMessage &&) = default;
  ~RichMessage() = default;

  RichMessage(Td *td, telegram_api::object_ptr<telegram_api::richMessage> &&rich_message, DialogId owner_dialog_id);

  bool is_full() const {
    return is_full_;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const;

  void add_dependencies(Dependencies &dependencies) const;

  void for_each_text(const std::function<void(Slice text)> &callback) const;

  vector<UserId> get_user_ids() const;

  bool has_bot_commands() const;

  td_api::object_ptr<td_api::richMessage> get_rich_message_object(Td *td) const;

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
