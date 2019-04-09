//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Document.h"

namespace td {

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
