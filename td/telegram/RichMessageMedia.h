//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class FullRemoteFileLocation;
class Td;

class RichMessageMedia {
  string id_;
  unique_ptr<MessageContent> media_;

  const FullRemoteFileLocation *get_full_main_remote_location(const Td *td) const;

 public:
  RichMessageMedia() = default;

  explicit RichMessageMedia(unique_ptr<MessageContent> media) : media_(std::move(media)) {
  }

  static Result<RichMessageMedia> get_rich_message_media(Td *td, DialogId dialog_id,
                                                         td_api::object_ptr<td_api::inputRichMessageMedia> &&media);

  static Result<vector<RichMessageMedia>> get_rich_message_media(
      Td *td, DialogId dialog_id, vector<td_api::object_ptr<td_api::inputRichMessageMedia>> &&media);

  unique_ptr<MessageContent> get_message_content(Td *td) const;

  unique_ptr<MessageContent> &get_message_content_editable() {
    return media_;
  }

  MessageContent *get_message_content_ref() {
    return media_.get();
  }

  const MessageContent *get_message_content_ref() const {
    return media_.get();
  }

  RichMessageMedia clone(Td *td, DialogId dialog_id, const MessageContentDupType &type) const;

  telegram_api::object_ptr<telegram_api::inputPhoto> get_input_photo(const Td *td) const;

  telegram_api::object_ptr<telegram_api::inputDocument> get_input_document(const Td *td) const;

  telegram_api::object_ptr<telegram_api::InputRichFile> get_input_rich_file(
      const Td *td, telegram_api::object_ptr<telegram_api::InputMedia> &&input_media) const;

  static void compare(Td *td, const RichMessageMedia &lhs, const RichMessageMedia &rhs, bool &is_changed,
                      bool &need_update);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

}  // namespace td
