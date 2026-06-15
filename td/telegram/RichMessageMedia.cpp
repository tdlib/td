//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RichMessageMedia.h"

#include "td/telegram/OptionManager.h"

namespace td {

Result<RichMessageMedia> RichMessageMedia::get_rich_message_media(
    Td *td, DialogId dialog_id, td_api::object_ptr<td_api::inputRichMessageMedia> &&media) {
  if (media == nullptr) {
    return Status::Error(400, "Media must be non-empty");
  }
  TRY_RESULT(input_message_content, get_input_message_content(dialog_id, std::move(media->media_), td, false));
  switch (input_message_content.content->get_type()) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Photo:
    case MessageContentType::Video:
    case MessageContentType::VoiceNote:
      break;
    default:
      return Status::Error(400, "Unallowed media specified");
  }
  RichMessageMedia result;
  result.id_ = std::move(media->id_);
  result.media_ = std::move(input_message_content.content);
  return std::move(result);
}

unique_ptr<MessageContent> RichMessageMedia::get_message_content(Td *td) const {
  return dup_message_content(td, DialogId(), media_.get(), MessageContentDupType::ServerCopy,
                             MessageCopyOptions(true, false));
}

}  // namespace td
