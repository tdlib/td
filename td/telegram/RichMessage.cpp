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
    case td_api::richMessageSourceMarkdown::ID: {
      auto source = td_api::move_object_as<td_api::richMessageSourceMarkdown>(message->source_);
      rich_message.source_ = std::move(source->text_);
      rich_message.input_type_ = InputType::Markdown;
      break;
    }
    case td_api::richMessageSourceHtml::ID: {
      auto source = td_api::move_object_as<td_api::richMessageSourceHtml>(message->source_);
      rich_message.source_ = std::move(source->text_);
      rich_message.input_type_ = InputType::Html;
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
  return std::move(rich_message);
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
    td::append(hashtags, block->get_hashtags());
  }
  return hashtags;
}

bool RichMessage::can_send(const RestrictedRights &rights) const {
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

telegram_api::object_ptr<telegram_api::InputRichMessage> RichMessage::get_input_rich_message(const Td *td) const {
  switch (input_type_) {
    case InputType::None: {
      int32 flags = 0;
      vector<telegram_api::object_ptr<telegram_api::PageBlock>> blocks;
      vector<telegram_api::object_ptr<telegram_api::InputPhoto>> photos;
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> documents;
      for (const auto &block : blocks_) {
        blocks.push_back(block->get_input_page_block(td, photos, documents));
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
    case InputType::Markdown:
      return telegram_api::make_object<telegram_api::inputRichMessageMarkdown>(0, is_rtl_, noautolink_, source_,
                                                                               Auto());
    case InputType::Html:
      return telegram_api::make_object<telegram_api::inputRichMessageHTML>(0, is_rtl_, noautolink_, source_, Auto());
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::richMessage> RichMessage::get_rich_message_object(Td *td, bool skip_bot_commands) const {
  return td_api::make_object<td_api::richMessage>(
      get_page_blocks_object(blocks_, td, string(), string(), skip_bot_commands), is_rtl_, is_full_);
}

RichMessage RichMessage::clone() const {
  RichMessage result;
  result.blocks_ = clone_web_page_blocks(blocks_);
  result.is_rtl_ = is_rtl_;
  result.is_full_ = is_full_;
  result.noautolink_ = noautolink_;
  result.input_type_ = input_type_;
  result.source_ = source_;
  return result;
}

bool operator==(const RichMessage &lhs, const RichMessage &rhs) {
  return lhs.blocks_ == rhs.blocks_ && lhs.is_rtl_ == rhs.is_rtl_ && lhs.is_full_ == rhs.is_full_;
}

}  // namespace td
