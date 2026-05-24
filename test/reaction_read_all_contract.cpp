// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(ReactionReadAllContract, ReadAllLocalPathUsesSharedUnreadReactionRemovalHelper) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = normalize_for_contract(extract_region(
      source, "bool MessagesManager::read_all_local_dialog_reactions(DialogId dialog_id, ForumTopicId forum_topic_id,",
      "void MessagesManager::read_all_dialog_reactions(DialogId dialog_id, ForumTopicId forum_topic_id,"));

  ASSERT_TRUE(region.find("remove_message_unread_reactions(d,m,\"read_all_local_dialog_reactions\")") !=
              td::string::npos);
  ASSERT_EQ(td::string::npos, region.find("m->reactions->unread_reactions_.clear();"));
  ASSERT_EQ(td::string::npos, region.find("send_update_message_unread_reactions(dialog_id,m,0);"));
}

TEST(ReactionReadAllContract, NegativeUnreadReactionLogGuardRequiresNonNullSourceString) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = normalize_for_contract(extract_region(
      source,
      "void MessagesManager::on_unread_message_reaction_removed(Dialog *d, const Message *m, const char *source) {",
      "bool MessagesManager::remove_message_unread_reactions(Dialog *d, Message *m, const char *source) {"));

  ASSERT_TRUE(region.find("if(is_dialog_inited(d)&&source!=nullptr){") != td::string::npos);
  ASSERT_EQ(td::string::npos, region.find("if(is_dialog_inited(d)){"));
}
