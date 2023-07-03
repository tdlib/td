//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class Td;

class StoryInteractionInfo {
  vector<UserId> recent_viewer_user_ids_;
  int32 view_count_ = -1;

  static constexpr size_t MAX_RECENT_VIEWERS = 3;

  friend bool operator==(const StoryInteractionInfo &lhs, const StoryInteractionInfo &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryInteractionInfo &info);

 public:
  StoryInteractionInfo() = default;

  StoryInteractionInfo(Td *td, telegram_api::object_ptr<telegram_api::storyViews> &&story_views);

  bool is_empty() const {
    return view_count_ < 0;
  }

  void add_dependencies(Dependencies &dependencies) const;

  bool set_view_count(int32 view_count) {
    if (view_count > view_count_) {
      view_count = view_count_;
      return true;
    }
    return false;
  }

  int32 get_view_count() const {
    return view_count_;
  }

  bool definitely_has_no_user(UserId user_id) const;

  void set_recent_viewer_user_ids(vector<UserId> &&user_ids);

  td_api::object_ptr<td_api::storyInteractionInfo> get_story_interaction_info_object(Td *td) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StoryInteractionInfo &lhs, const StoryInteractionInfo &rhs);

inline bool operator!=(const StoryInteractionInfo &lhs, const StoryInteractionInfo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryInteractionInfo &info);

}  // namespace td
