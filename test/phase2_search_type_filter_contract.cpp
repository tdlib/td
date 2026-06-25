// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

// Whitespace-insensitive substring presence.
bool has(const td::string &normalized_source, td::Slice needle) {
  return normalized_source.find(needle.str()) != td::string::npos;
}

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

// Non-aborting region slice: returns "" if either marker is absent (so a pre-backport RED run fails
// cleanly via ASSERT rather than aborting the whole binary through CHECK).
td::string safe_region(const td::string &normalized_source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = normalized_source.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = normalized_source.find(end_marker.str(), begin + begin_marker.size());
  if (end == td::string::npos || end <= begin) {
    return {};
  }
  return normalized_source.substr(begin, end - begin);
}

td::string read_norm(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

// Phase-2 search-type-filter epic backport guard (upstream cluster 316e93443 DialogTypeFilter,
// bdf910f19 searchPublicChats.type_filter, e58779017 searchChatsOnServer.type_filter,
// 9ec11ab28 searchRecentlyFoundChats.type_filter, 21cbc0d17 move search_dialogs to DialogManager,
// 26d8c6a81 searchChats.type_filter). These contracts MUST hold after the epic lands.

// SECURITY / fail-closed: the bot-exclusion guard on search_dialogs must survive the
// MessagesManager -> DialogManager move. A bot must never be able to run dialog search (the guard
// is an UNREACHABLE-grade invariant, CHECK(!is_bot())). If a conflict resolution dropped it during
// the move, this is a fail-open regression.
TEST(Phase2SearchTypeFilterContract, SearchDialogsRetainsBotExclusionGuardAfterMove) {
  auto dm = read_norm("td/telegram/DialogManager.cpp");
  // search_dialogs now lives in DialogManager and still asserts the caller is not a bot
  auto region = safe_region(dm, "DialogManager::search_dialogs(", "return{narrow_cast<int32>(");
  ASSERT_TRUE(!region.empty());
  ASSERT_TRUE(has(region, "CHECK(!td_->auth_manager_->is_bot());"));
}

// The move must be complete: no stale/duplicate definition left behind in MessagesManager.
TEST(Phase2SearchTypeFilterContract, SearchDialogsMovedOutOfMessagesManager) {
  auto mm = read_norm("td/telegram/MessagesManager.cpp");
  ASSERT_TRUE(!has(mm, "MessagesManager::search_dialogs("));
}

// The filter enum and its classifier predicate exist, with the enum carrying the documented members.
TEST(Phase2SearchTypeFilterContract, DialogTypeFilterEnumAndPredicatePresent) {
  auto h = read_norm("td/telegram/DialogManager.h");
  ASSERT_TRUE(has(h, "enumclassDialogTypeFilter:int32{None,Bot,Broadcast};"));
  ASSERT_TRUE(has(h, "is_dialog_suitable_for_type_filter(DialogIddialog_id,DialogTypeFiltertype_filter)const;"));
  auto cpp = read_norm("td/telegram/DialogManager.cpp");
  ASSERT_TRUE(has(cpp, "DialogManager::is_dialog_suitable_for_type_filter(DialogIddialog_id,DialogTypeFiltertype_filter)const{"));
}

// TL schema: the four search methods carry the type_filter and the SearchChatTypeFilter class exists.
TEST(Phase2SearchTypeFilterContract, TlSchemaThreadsTypeFilter) {
  auto tl = read_norm("td/generate/scheme/td_api.tl");
  ASSERT_TRUE(has(tl, "=SearchChatTypeFilter;"));
  ASSERT_TRUE(has(tl, "searchPublicChatsquery:stringtype_filter:SearchChatTypeFilter=Chats;"));
  ASSERT_TRUE(has(tl, "searchChatsquery:stringtype_filter:SearchChatTypeFilterlimit:int32=Chats;"));
  ASSERT_TRUE(has(tl, "searchChatsOnServerquery:stringtype_filter:SearchChatTypeFilterlimit:int32=Chats;"));
  ASSERT_TRUE(has(tl, "searchRecentlyFoundChatsquery:stringtype_filter:SearchChatTypeFilterlimit:int32=Chats;"));
}

}  // namespace
