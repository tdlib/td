//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/FolderId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <type_traits>

namespace td {

class DialogListId {
  int64 id = 0;

 public:
  DialogListId() = default;

  explicit DialogListId(int64 dialog_list_id) : id(dialog_list_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  DialogListId(T dialog_list_id) = delete;

  explicit DialogListId(FolderId folder_id) : id(folder_id.get()) {
  }

  explicit DialogListId(const td_api::object_ptr<td_api::ChatList> &chat_list) {
    if (chat_list == nullptr) {
      CHECK(id == FolderId::main().get());
      return;
    }
    switch (chat_list->get_id()) {
      case td_api::chatListArchive::ID:
        id = FolderId::archive().get();
        break;
      case td_api::chatListMain::ID:
        CHECK(id == FolderId::main().get());
        break;
      default:
        UNREACHABLE();
        break;
    }
  }

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
    return false;
  }

  FolderId get_folder_id() const {
    CHECK(is_folder());
    return FolderId(static_cast<int32>(id));
  }

  /*
  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_long(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_long();
  }
  */
};

struct DialogListIdHash {
  std::size_t operator()(DialogListId dialog_list_id) const {
    return std::hash<int64>()(dialog_list_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, DialogListId dialog_list_id) {
  return string_builder << "chat list " << dialog_list_id.get();
}

}  // namespace td
