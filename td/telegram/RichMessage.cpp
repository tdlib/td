//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RichMessage.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/misc.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"

namespace td {

RichMessage::RichMessage(Td *td, telegram_api::object_ptr<telegram_api::richMessage> &&rich_message,
                         DialogId owner_dialog_id) {
  CHECK(rich_message != nullptr);

  FlatHashMap<int64, unique_ptr<Photo>> photos;
  for (auto &photo_ptr : rich_message->photos_) {
    Photo photo = get_photo(td, std::move(photo_ptr), owner_dialog_id);
    if (photo.is_empty() || photo.id.get() == 0) {
      LOG(ERROR) << "Receive empty photo in a rich message in " << owner_dialog_id;
    } else {
      auto photo_id = photo.id.get();
      photos.emplace(photo_id, make_unique<Photo>(std::move(photo)));
    }
  }

  FlatHashMap<int64, FileId> animations;
  FlatHashMap<int64, FileId> audios;
  FlatHashMap<int64, FileId> documents;
  FlatHashMap<int64, FileId> videos;
  FlatHashMap<int64, FileId> voice_notes;
  FlatHashMap<int64, FileId> others;
  auto get_map = [&](Document::Type document_type) {
    switch (document_type) {
      case Document::Type::Animation:
        return &animations;
      case Document::Type::Audio:
        return &audios;
      case Document::Type::General:
        return &documents;
      case Document::Type::Video:
        return &videos;
      case Document::Type::VoiceNote:
        return &voice_notes;
      default:
        return &others;
    }
  };

  for (auto &document_ptr : rich_message->documents_) {
    if (document_ptr->get_id() == telegram_api::document::ID) {
      auto document = telegram_api::move_object_as<telegram_api::document>(document_ptr);
      auto document_id = document->id_;
      auto parsed_document =
          td->documents_manager_->on_get_document(std::move(document), owner_dialog_id, false, false);
      if (!parsed_document.is_empty() && document_id != 0) {
        get_map(parsed_document.type)->emplace(document_id, parsed_document.file_id);
      }
    }
  }
  if (!others.empty()) {
    auto file_view = td->file_manager_->get_file_view(others.begin()->second);
    LOG(ERROR) << "Receive document of an unexpected type " << file_view.get_type();
  }

  blocks_ = get_web_page_blocks(td, std::move(rich_message->blocks_), animations, audios, documents, photos, videos,
                                voice_notes);
  is_rtl_ = rich_message->rtl_;
  is_full_ = !rich_message->part_;
  media_ = get_page_blocks_rich_message_media(blocks_);
}

Result<RichMessage> RichMessage::get_rich_message(Td *td, DialogId dialog_id,
                                                  td_api::object_ptr<td_api::inputRichMessage> &&message, bool is_bot) {
  if (message == nullptr || message->source_ == nullptr) {
    return Status::Error(400, "Rich message must be non-empty");
  }
  RichMessage rich_message;
  rich_message.is_rtl_ = message->is_rtl_;
  rich_message.noautolink_ = !message->detect_automatic_blocks_;
  rich_message.is_full_ = true;
  switch (message->source_->get_id()) {
    case td_api::richMessageSourceBlocks::ID: {
      auto source = td_api::move_object_as<td_api::richMessageSourceBlocks>(message->source_);
      TRY_RESULT(blocks, get_web_page_blocks(td, dialog_id, std::move(source->blocks_)));
      rich_message.blocks_ = std::move(blocks);
      rich_message.media_ = get_page_blocks_rich_message_media(rich_message.blocks_);
      break;
    }
    case td_api::richMessageSourceMarkdown::ID: {
      auto source = td_api::move_object_as<td_api::richMessageSourceMarkdown>(message->source_);
      if (!clean_input_string(source->text_)) {
        return Status::Error(400, "Strings must be encoded in UTF-8");
      }
      rich_message.source_ = std::move(source->text_);
      rich_message.input_type_ = InputType::Markdown;
      TRY_RESULT(media, RichMessageMedia::get_rich_message_media(td, dialog_id, std::move(source->media_)));
      rich_message.media_ = std::move(media);
      break;
    }
    case td_api::richMessageSourceHtml::ID: {
      auto source = td_api::move_object_as<td_api::richMessageSourceHtml>(message->source_);
      rich_message.source_ = std::move(source->text_);
      if (!clean_input_string(source->text_)) {
        return Status::Error(400, "Strings must be encoded in UTF-8");
      }
      rich_message.input_type_ = InputType::Html;
      TRY_RESULT(media, RichMessageMedia::get_rich_message_media(td, dialog_id, std::move(source->media_)));
      rich_message.media_ = std::move(media);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (!clean_input_string(rich_message.source_)) {
    return Status::Error(400, "Strings must be encoded in UTF-8");
  }
  return std::move(rich_message);
}

Result<RichMessage> RichMessage::get_rich_message(Td *td, DialogId dialog_id,
                                                  td_api::object_ptr<td_api::richMessage> &&message, bool is_bot) {
  if (message == nullptr) {
    return Status::Error(400, "Rich message must be non-empty");
  }
  RichMessage rich_message;
  // rich_message.blocks_ = std::move(blocks);
  rich_message.is_rtl_ = message->is_rtl_;
  rich_message.is_full_ = true;
  rich_message.media_ = get_page_blocks_rich_message_media(rich_message.blocks_);
  return std::move(rich_message);
}

vector<FileId> RichMessage::get_any_file_ids() const {
  return transform(get_individual_message_content_refs(),
                   [](const MessageContent *content) { return get_message_content_any_file_id(content); });
}

void RichMessage::append_file_ids(const Td *td, vector<FileId> &file_ids) const {
  for (const auto &block : blocks_) {
    block->append_file_ids(td, file_ids);
  }
}

void RichMessage::add_dependencies(Dependencies &dependencies) const {
  for (const auto &block : blocks_) {
    block->add_dependencies(dependencies);
  }
}

void RichMessage::for_each_text(const std::function<void(Slice text)> &callback) const {
  for (const auto &block : blocks_) {
    block->for_each_text(callback);
  }
}

vector<UserId> RichMessage::get_user_ids() const {
  vector<UserId> user_ids;
  for (const auto &block : blocks_) {
    block->append_user_ids(user_ids);
  }
  return user_ids;
}

bool RichMessage::has_bot_commands() const {
  for (const auto &block : blocks_) {
    if (block->has_bot_commands()) {
      return true;
    }
  }
  return false;
}

vector<string> RichMessage::get_hashtags() const {
  vector<string> hashtags;
  for (const auto &block : blocks_) {
    append(hashtags, block->get_hashtags());
  }
  return hashtags;
}

vector<CustomEmojiId> RichMessage::get_custom_emoji_ids() const {
  vector<CustomEmojiId> custom_emoji_ids;
  for (const auto &block : blocks_) {
    append(custom_emoji_ids, block->get_custom_emoji_ids());
  }
  return custom_emoji_ids;
}

bool RichMessage::can_send(const RestrictedRights &rights) const {
  if (!rights.can_send_messages()) {
    return false;
  }
  for (const auto &block : blocks_) {
    if (!block->can_send(rights)) {
      return false;
    }
  }
  return true;
}

int32 RichMessage::get_index_mask() const {
  return get_web_page_blocks_index_mask(blocks_);
}

vector<telegram_api::object_ptr<telegram_api::InputRichFile>> RichMessage::get_input_rich_files(
    const Td *td, bool with_input_media, vector<telegram_api::object_ptr<telegram_api::InputMedia>> input_media) const {
  vector<telegram_api::object_ptr<telegram_api::InputRichFile>> input_rich_files;
  for (size_t i = 0; i < media_.size(); i++) {
    auto input_rich_file = media_[i].get_input_rich_file(td, with_input_media ? std::move(input_media[i]) : nullptr);
    if (input_rich_file != nullptr) {
      input_rich_files.push_back(std::move(input_rich_file));
    }
  }
  return input_rich_files;
}

telegram_api::object_ptr<telegram_api::InputRichMessage> RichMessage::get_input_rich_message(
    const Td *td, bool with_input_media, vector<telegram_api::object_ptr<telegram_api::InputMedia>> input_media) const {
  if (with_input_media) {
    CHECK(media_.size() == input_media.size());
  }
  switch (input_type_) {
    case InputType::None: {
      int32 flags = 0;
      vector<telegram_api::object_ptr<telegram_api::PageBlock>> blocks;
      vector<telegram_api::object_ptr<telegram_api::InputPhoto>> photos;
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> documents;
      for (const auto &block : blocks_) {
        blocks.push_back(block->get_input_page_block(td, photos, documents));
      }
      if (with_input_media) {
        photos.clear();
        documents.clear();
        for (auto &media : input_media) {
          switch (media->get_id()) {
            case telegram_api::inputMediaPhoto::ID: {
              auto *photo = static_cast<telegram_api::inputMediaPhoto *>(media.get());
              photos.push_back(std::move(photo->id_));
              break;
            }
            case telegram_api::inputMediaDocument::ID: {
              auto *document = static_cast<telegram_api::inputMediaDocument *>(media.get());
              documents.push_back(std::move(document->id_));
              break;
            }
            default:
              UNREACHABLE();
          }
        }
      }
      if (photos.size() + documents.size() < media_.size()) {
        CHECK(!with_input_media);
        return nullptr;
      }
      if (!photos.empty()) {
        flags |= telegram_api::inputRichMessage::PHOTOS_MASK;
      }
      if (!documents.empty()) {
        flags |= telegram_api::inputRichMessage::DOCUMENTS_MASK;
      }
      auto input_users = td->user_manager_->get_input_users_force(get_user_ids());
      if (!input_users.empty()) {
        flags |= telegram_api::inputRichMessage::USERS_MASK;
      }
      return telegram_api::make_object<telegram_api::inputRichMessage>(flags, is_rtl_, noautolink_, std::move(blocks),
                                                                       std::move(photos), std::move(documents),
                                                                       std::move(input_users));
    }
    case InputType::Markdown: {
      int32 flags = 0;
      auto files = get_input_rich_files(td, with_input_media, std::move(input_media));
      if (files.size() != media_.size()) {
        CHECK(!with_input_media);
        return nullptr;
      }
      if (!files.empty()) {
        flags |= telegram_api::inputRichMessageMarkdown::FILES_MASK;
      }
      return telegram_api::make_object<telegram_api::inputRichMessageMarkdown>(flags, is_rtl_, noautolink_, source_,
                                                                               std::move(files));
    }
    case InputType::Html: {
      int32 flags = 0;
      auto files = get_input_rich_files(td, with_input_media, std::move(input_media));
      if (files.size() != media_.size()) {
        CHECK(!with_input_media);
        return nullptr;
      }
      if (!files.empty()) {
        flags |= telegram_api::inputRichMessageHTML::FILES_MASK;
      }
      return telegram_api::make_object<telegram_api::inputRichMessageHTML>(flags, is_rtl_, noautolink_, source_,
                                                                           std::move(files));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::richMessage> RichMessage::get_rich_message_object(Td *td, bool skip_bot_commands) const {
  return td_api::make_object<td_api::richMessage>(
      get_page_blocks_object(blocks_, td, string(), string(), skip_bot_commands), is_rtl_, is_full_);
}

RichMessage RichMessage::clone(Td *td, DialogId dialog_id, const MessageContentDupType &type) const {
  RichMessage result;
  result.blocks_ = clone_web_page_blocks(blocks_);
  result.is_rtl_ = is_rtl_;
  result.is_full_ = is_full_;
  result.noautolink_ = noautolink_;
  result.input_type_ = input_type_;
  if (!media_.empty()) {
    if (td != nullptr) {
      result.media_ =
          transform(media_, [&](const RichMessageMedia &media) { return media.clone(td, dialog_id, type); });
    } else {
      LOG(ERROR) << "Have no Td to clone RichMessage media";
      result.media_ = get_page_blocks_rich_message_media(result.blocks_);
    }
  }
  result.source_ = source_;
  return result;
}

vector<unique_ptr<MessageContent>> RichMessage::get_individual_message_contents(Td *td) const {
  return transform(media_, [td](const RichMessageMedia &media) { return media.get_message_content(td); });
}

vector<MessageContent *> RichMessage::get_individual_message_content_refs() {
  vector<MessageContent *> message_contents;
  for (auto &media : media_) {
    message_contents.push_back(media.get_message_content_ref());
  }
  return message_contents;
}

vector<const MessageContent *> RichMessage::get_individual_message_content_refs() const {
  return transform(media_, [](const RichMessageMedia &media) { return media.get_message_content_ref(); });
}

unique_ptr<MessageContent> &RichMessage::get_individual_message_content(int32 media_pos) {
  CHECK(static_cast<size_t>(media_pos) < media_.size());
  return media_[media_pos].get_message_content_editable();
}

const MessageContent *RichMessage::get_individual_message_content_ref(int32 media_pos) const {
  CHECK(static_cast<size_t>(media_pos) < media_.size());
  return media_[media_pos].get_message_content_ref();
}

void RichMessage::compare(Td *td, const RichMessage &lhs, const RichMessage &rhs, bool &is_changed, bool &need_update) {
  if (lhs.blocks_ != rhs.blocks_ || lhs.is_rtl_ != rhs.is_rtl_ || lhs.is_full_ != rhs.is_full_) {
    need_update = true;
  } else if (lhs.noautolink_ != rhs.noautolink_ || lhs.input_type_ != rhs.input_type_ ||
             lhs.media_.size() != rhs.media_.size() || lhs.source_ != rhs.source_) {
    is_changed = true;
  } else {
    for (size_t i = 0; i < lhs.media_.size(); i++) {
      RichMessageMedia::compare(td, lhs.media_[i], rhs.media_[i], is_changed, is_changed);
    }
  }
}

bool operator==(const RichMessage &lhs, const RichMessage &rhs) {
  // compare only publicly-visible fields
  return lhs.blocks_ == rhs.blocks_ && lhs.is_rtl_ == rhs.is_rtl_ && lhs.is_full_ == rhs.is_full_;
}

}  // namespace td
