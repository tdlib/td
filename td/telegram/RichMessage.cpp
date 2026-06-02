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
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

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

td_api::object_ptr<td_api::richMessage> RichMessage::get_rich_message_object(Td *td) const {
  return td_api::make_object<td_api::richMessage>(get_page_blocks_object(blocks_, td, string(), string()), is_rtl_,
                                                  is_full_);
}

RichMessage RichMessage::clone() const {
  RichMessage result;
  result.blocks_ = clone_web_page_blocks(blocks_);
  result.is_rtl_ = is_rtl_;
  result.is_full_ = is_full_;
  return result;
}

bool operator==(const RichMessage &lhs, const RichMessage &rhs) {
  return lhs.blocks_ == rhs.blocks_ && lhs.is_rtl_ == rhs.is_rtl_ && lhs.is_full_ == rhs.is_full_;
}

}  // namespace td
