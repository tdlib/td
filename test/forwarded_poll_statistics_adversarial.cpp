// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
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

TEST(ForwardedPollStatisticsAdversarial, ForwardedPollHelperRemainsFailClosedForNonPollOrImportedOrigins) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::can_get_message_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");

  ASSERT_TRUE(region.find("content->get_type() == MessageContentType::Poll") != td::string::npos);
  ASSERT_TRUE(region.find("forward_info != nullptr") != td::string::npos);
  ASSERT_TRUE(region.find("!message->forward_info->is_imported()") != td::string::npos);
  ASSERT_TRUE(region.find("forward_info->get_origin().is_channel_post()") != td::string::npos);
  ASSERT_TRUE(region.find("if (is_forwarded_message && !can_get_forwarded_poll_statistics(m))") != td::string::npos);
}

TEST(ForwardedPollStatisticsAdversarial, PollCanViewStatsHelperUsesFailClosedPollManagerAccess) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region = extract_region(
      source, "bool get_message_content_poll_can_view_stats(const Td *td, const MessageContent *content) {",
      "bool get_message_content_poll_has_unread_votes(const Td *td, const MessageContent *content) {");

  ASSERT_TRUE(region.find("get_poll_manager_for_content_access") != td::string::npos);
  ASSERT_TRUE(region.find("get_message_content_poll_can_view_stats") != td::string::npos);
  ASSERT_TRUE(region.find("poll_manager != nullptr && poll_manager->get_poll_can_view_stats(poll_id)") !=
              td::string::npos);
}

TEST(ForwardedPollStatisticsAdversarial, PollManagerTracksCanViewStatsFromServerPollResults) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  // NOTE: The begin marker uses a stable prefix that captures the bot-guard hardening added after initial
  // test authorship: `!td_->auth_manager_->is_bot()` intentionally gates unread-vote tracking to non-bot
  // sessions, since bots do not participate in the unread-vote counting lifecycle.
  auto region = extract_region(
      source, "poll_results->has_unread_votes_ != poll->has_unread_votes_) {",
      "auto explanation = get_formatted_text(td_->user_manager_.get(), std::move(poll_results->solution_),");

  ASSERT_TRUE(region.find("if (!is_min && poll_results->can_view_stats_ != poll->can_view_stats_)") !=
              td::string::npos);
  ASSERT_TRUE(region.find("poll->can_view_stats_ = poll_results->can_view_stats_;") != td::string::npos);
}

TEST(ForwardedPollStatisticsAdversarial, TelegramSchemaExposesPollResultsCanViewStatsBit) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/telegram_api.tl");

  ASSERT_TRUE(source.find("pollResults#ba7bb15e") != td::string::npos);
  ASSERT_TRUE(source.find("can_view_stats:flags.7?true") != td::string::npos);
}

TEST(ForwardedPollStatisticsAdversarial, PollVoteStatisticsGateRejectsMissingPollContentFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "bool MessagesManager::can_get_message_poll_vote_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("m->content==nullptr||m->content->get_type()!=MessageContentType::Poll") !=
              td::string::npos);
}

TEST(ForwardedPollStatisticsAdversarial,
     PollVoteStatisticsGateTreatsRetainedForwardInfoAsForwardedAndAppliesForwardPolicy) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "bool MessagesManager::can_get_message_poll_vote_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("constboolis_forwarded_message=m->forward_info!=nullptr||m->had_forward_info;") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("can_get_forwarded_poll_statistics") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(is_forwarded_message&&!can_get_forwarded_poll_statistics(m)){") != td::string::npos);
}

}  // namespace
