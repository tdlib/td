//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogListId.h"

#include "td/utils/algorithm.h"

namespace td {

DialogListId::DialogListId(int64 dialog_list_id) : id(dialog_list_id) {
  if (is_folder() && get_folder_id() != FolderId::archive()) {
    id = FolderId::main().get();
  } else if (is_filter()) {
    CHECK(get_filter_id().is_valid());
  }
}

DialogListId::DialogListId(const td_api::object_ptr<td_api::ChatList> &chat_list) {
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
    case td_api::chatListFolder::ID: {
      DialogFilterId filter_id(static_cast<const td_api::chatListFolder *>(chat_list.get())->chat_folder_id_);
      if (filter_id.is_valid()) {
        *this = DialogListId(filter_id);
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

td_api::object_ptr<td_api::ChatList> DialogListId::get_chat_list_object() const {
  if (is_folder()) {
    auto folder_id = get_folder_id();
    if (folder_id == FolderId::archive()) {
      return td_api::make_object<td_api::chatListArchive>();
    }
    return td_api::make_object<td_api::chatListMain>();
  }
  if (is_filter()) {
    return td_api::make_object<td_api::chatListFolder>(get_filter_id().get());
  }
  UNREACHABLE();
  return nullptr;
}

vector<td_api::object_ptr<td_api::ChatList>> DialogListId::get_chat_lists_object(
    const vector<DialogListId> &dialog_list_ids) {
  return transform(dialog_list_ids, [](DialogListId dialog_list_id) { return dialog_list_id.get_chat_list_object(); });
}

StringBuilder &operator<<(StringBuilder &string_builder, DialogListId dialog_list_id) {
  if (dialog_list_id.is_folder()) {
    auto folder_id = dialog_list_id.get_folder_id();
    if (folder_id == FolderId::archive()) {
      return string_builder << "Archive chat list";
    }
    if (folder_id == FolderId::main()) {
      return string_builder << "Main chat list";
    }
    return string_builder << "chat list " << folder_id;
  }
  if (dialog_list_id.is_filter()) {
    return string_builder << "chat list " << dialog_list_id.get_filter_id();
  }
  return string_builder << "unknown chat list " << dialog_list_id.get();
}

}  // namespace td
