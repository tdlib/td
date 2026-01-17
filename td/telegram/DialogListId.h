//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogFilterId.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

#include <limits>
#include <type_traits>

namespace td {

class DialogListId {
  int64 id = 0;

  static constexpr int64 FILTER_ID_SHIFT = static_cast<int64>(1) << 32;

 public:
  DialogListId() = default;

  explicit DialogListId(int64 dialog_list_id);

  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  DialogListId(T dialog_list_id) = delete;

  explicit DialogListId(DialogFilterId dialog_filter_id) : id(dialog_filter_id.get() + FILTER_ID_SHIFT) {
  }

  explicit DialogListId(FolderId folder_id) : id(folder_id.get()) {
  }

  explicit DialogListId(const td_api::object_ptr<td_api::ChatList> &chat_list);

  td_api::object_ptr<td_api::ChatList> get_chat_list_object() const;

  static vector<td_api::object_ptr<td_api::ChatList>> get_chat_lists_object(
      const vector<DialogListId> &dialog_list_ids);

  int64 get() const {
    return id;
  }

  bool operator==(const DialogListId &other) const {
    return id == other.id;
  }

  bool operator!=(const DialogListId &other) const {
    return id != other.id;
  }

  bool is_folder() const {
    return std::numeric_limits<int32>::min() <= id && id <= std::numeric_limits<int32>::max();
  }

  bool is_filter() const {
    return std::numeric_limits<int32>::min() + FILTER_ID_SHIFT <= id &&
           id <= std::numeric_limits<int32>::max() + FILTER_ID_SHIFT;
  }

  FolderId get_folder_id() const {
    CHECK(is_folder());
    return FolderId(static_cast<int32>(id));
  }

  DialogFilterId get_filter_id() const {
    CHECK(is_filter());
    return DialogFilterId(static_cast<int32>(id - FILTER_ID_SHIFT));
  }
};

struct DialogListIdHash {
  uint32 operator()(DialogListId dialog_list_id) const {
    return Hash<int64>()(dialog_list_id.get());
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, DialogListId dialog_list_id);

}  // namespace td
