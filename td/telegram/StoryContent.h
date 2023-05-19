//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

namespace td {

class Td;

class StoryContent {
 public:
  StoryContent() = default;
  StoryContent(const StoryContent &) = default;
  StoryContent &operator=(const StoryContent &) = default;
  StoryContent(StoryContent &&) = default;
  StoryContent &operator=(StoryContent &&) = default;

  virtual StoryContentType get_type() const = 0;
  virtual ~StoryContent() = default;
};

unique_ptr<StoryContent> get_story_content(Td *td, telegram_api::object_ptr<telegram_api::MessageMedia> &&media_ptr,
                                           DialogId owner_dialog_id);

void merge_story_contents(Td *td, const StoryContent *old_content, StoryContent *new_content, DialogId dialog_id,
                          bool need_merge_files, bool &is_content_changed, bool &need_update);

td_api::object_ptr<td_api::StoryContent> get_story_content_object(Td *td, const StoryContent *content);

}  // namespace td
