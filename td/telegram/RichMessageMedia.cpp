//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RichMessageMedia.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"

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

Result<vector<RichMessageMedia>> RichMessageMedia::get_rich_message_media(
    Td *td, DialogId dialog_id, vector<td_api::object_ptr<td_api::inputRichMessageMedia>> &&media) {
  vector<RichMessageMedia> result;
  for (auto &m : media) {
    TRY_RESULT(rich_message_media, get_rich_message_media(td, dialog_id, std::move(m)));
    result.push_back(std::move(rich_message_media));
  }
  return std::move(result);
}

unique_ptr<MessageContent> RichMessageMedia::get_message_content(Td *td) const {
  CHECK(td != nullptr);
  return dup_message_content(td, DialogId(), media_.get(), MessageContentDupType::ServerCopy,
                             MessageCopyOptions(true, false));
}

RichMessageMedia RichMessageMedia::clone(Td *td, DialogId dialog_id, const MessageContentDupType &type) const {
  RichMessageMedia result;
  result.id_ = id_;
  result.media_ = dup_message_content(
      td, dialog_id, media_.get(), type,
      MessageCopyOptions(type == MessageContentDupType::Copy || type == MessageContentDupType::ServerCopy, false));
  return result;
}

telegram_api::object_ptr<telegram_api::InputRichFile> RichMessageMedia::get_input_rich_file(
    const Td *td, telegram_api::object_ptr<telegram_api::InputMedia> &&input_media) const {
  if (input_media != nullptr) {
    switch (input_media->get_id()) {
      case telegram_api::inputMediaPhoto::ID: {
        auto *photo = static_cast<telegram_api::inputMediaPhoto *>(input_media.get());
        return telegram_api::make_object<telegram_api::inputRichFilePhoto>(id_, std::move(photo->id_));
      }
      case telegram_api::inputMediaDocument::ID: {
        auto *document = static_cast<telegram_api::inputMediaDocument *>(input_media.get());
        return telegram_api::make_object<telegram_api::inputRichFileDocument>(id_, std::move(document->id_));
      }
      default:
        UNREACHABLE();
    }
  }
  auto file_id = get_message_content_any_file_id(media_.get());
  CHECK(file_id.is_valid());
  auto file_view = td->file_manager_->get_file_view(file_id);
  const auto *main_remote_location = file_view.get_main_remote_location();
  if (file_view.is_encrypted() || main_remote_location == nullptr || main_remote_location->is_web()) {
    return nullptr;
  }
  switch (media_->get_type()) {
    case MessageContentType::Photo:
      return telegram_api::make_object<telegram_api::inputRichFilePhoto>(id_, main_remote_location->as_input_photo());
    default:
      return telegram_api::make_object<telegram_api::inputRichFileDocument>(id_,
                                                                            main_remote_location->as_input_document());
  }
}

}  // namespace td
