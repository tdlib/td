//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryContentType.h"

namespace td {

bool can_send_story_content(StoryContentType content_type) {
  switch (content_type) {
    case StoryContentType::Photo:
    case StoryContentType::Video:
      return true;
    case StoryContentType::Unsupported:
    case StoryContentType::LiveStream:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

bool can_edit_story_content(StoryContentType content_type) {
  switch (content_type) {
    case StoryContentType::Photo:
    case StoryContentType::Video:
      return true;
    case StoryContentType::Unsupported:
    case StoryContentType::LiveStream:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, StoryContentType content_type) {
  switch (content_type) {
    case StoryContentType::Photo:
      return string_builder << "Photo";
    case StoryContentType::Video:
      return string_builder << "Video";
    case StoryContentType::Unsupported:
      return string_builder << "Unsupported";
    case StoryContentType::LiveStream:
      return string_builder << "LiveStream";
    default:
      return string_builder << "Invalid type " << static_cast<int32>(content_type);
  }
}

}  // namespace td
