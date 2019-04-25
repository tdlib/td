//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Document.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"

namespace td {

vector<FileId> Document::get_file_ids(const Td *td) const {
  vector<FileId> result;
  if (empty()) {
    return result;
  }
  CHECK(file_id.is_valid());

  result.push_back(file_id);
  FileId thumbnail_file_id = [&] {
    switch (type) {
      case Type::Animation:
        return td->animations_manager_->get_animation_thumbnail_file_id(file_id);
      case Type::Audio:
        return td->audios_manager_->get_audio_thumbnail_file_id(file_id);
      case Type::General:
        return td->documents_manager_->get_document_thumbnail_file_id(file_id);
      case Type::Sticker:
        return td->stickers_manager_->get_sticker_thumbnail_file_id(file_id);
      case Type::Video:
        return td->videos_manager_->get_video_thumbnail_file_id(file_id);
      case Type::VideoNote:
        return td->video_notes_manager_->get_video_note_thumbnail_file_id(file_id);
      default:
        return FileId();
    }
  }();
  if (thumbnail_file_id.is_valid()) {
    result.push_back(thumbnail_file_id);
  }
  return result;
}

StringBuilder &operator<<(StringBuilder &string_builder, const Document &document) {
  auto type = [&] {
    switch (document.type) {
      case Document::Type::Unknown:
        return "Unknown";
      case Document::Type::Animation:
        return "Animation";
      case Document::Type::Audio:
        return "Audio";
      case Document::Type::General:
        return "Document";
      case Document::Type::Sticker:
        return "Sticker";
      case Document::Type::Video:
        return "Video";
      case Document::Type::VideoNote:
        return "VideoNote";
      case Document::Type::VoiceNote:
        return "VoiceNote";
      default:
        return "Unreachable";
    }
  }();

  return string_builder << '[' << type << ' ' << document.file_id << ']';
}

}  // namespace td
