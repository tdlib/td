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

// Phase-2 chat-join / guard-bot epic backport guard (upstream tail commits b449204c5, 3e17b9042,
// 7a154de35, e27fde415, 434ef4b47). The fork already carried most of the guard-bot surface via its own
// follow-on commits; these five added the trailing pieces. These contracts pin the behaviour that a
// careless conflict resolution could have silently regressed.

// 434ef4b47 — "Add td_api::chatJoinResultRequestDeclined" (type is chatJoinResultDeclined). A server
// INVITE_REQUEST_DECLINED must map to the explicit declined result, not fall through to the generic
// nullptr (which the caller would surface as an opaque error). The pre-existing RequestSent/timeout
// branch must remain intact.
TEST(Phase2ChatJoinGuardBotContract, DeclinedErrorMapsToDeclinedResult) {
  auto src = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipantManager.cpp");
  auto region = extract_region(
      src, "DialogParticipantManager::get_chat_join_result_object(const Status &error) {", "return nullptr;");
  auto n = normalize_for_contract(region);
  // existing branch preserved
  ASSERT_TRUE(n.find("error.message()==\"INVITE_REQUEST_SENT\"") != td::string::npos);
  ASSERT_TRUE(n.find("make_object<td_api::chatJoinResultRequestSent>()") != td::string::npos);
  // new declined mapping
  ASSERT_TRUE(n.find("error.message()==\"INVITE_REQUEST_DECLINED\"") != td::string::npos);
  ASSERT_TRUE(n.find("make_object<td_api::chatJoinResultDeclined>()") != td::string::npos);
}

// 7a154de35 — "Return webAppUrl in chatJoinResultGuardBotApprovalRequired". The guard-bot Web App URL
// must be wrapped in a webAppUrl object that PROPAGATES the server's same_origin flag. Dropping
// same_origin (the old bare `url_, query_id_` form) would be a security downgrade: the client would no
// longer be told that Web App events must be accepted only from the URL's origin. Both construction
// sites (invite-link import and channel join) must use the secure form.
TEST(Phase2ChatJoinGuardBotContract, GuardBotWebAppUrlPropagatesSameOrigin) {
  for (auto path : {"td/telegram/DialogParticipantManager.cpp", "td/telegram/DialogInviteLinkManager.cpp"}) {
    auto n = normalize_for_contract(td::mtproto::test::read_repo_text_file(td::Slice(path)));
    // secure form: url_ wrapped together with same_origin_ into a webAppUrl object
    ASSERT_TRUE(n.find("make_object<td_api::webAppUrl>(ptr->webview_->url_,ptr->webview_->same_origin_)") !=
                td::string::npos);
    // the old downgrade form (url passed bare, same_origin dropped) must be gone
    ASSERT_TRUE(n.find("chatJoinResultGuardBotApprovalRequired>(ptr->webview_->url_,ptr->webview_->query_id_)") ==
                td::string::npos);
    ASSERT_TRUE(
        n.find("chatJoinResultGuardBotApprovalRequired>(user_id_object,ptr->webview_->url_,ptr->webview_->query_id_)") ==
        td::string::npos);
  }
}

// b449204c5 — "Add td_api::updateChatJoinResult". The updateJoinChatWebViewDecision handler must stop
// being an unsupported no-op and actually forward the join outcome to the client as updateChatJoinResult,
// translating the binary result through JoinChatBotResult::get_chat_join_request_result_object().
TEST(Phase2ChatJoinGuardBotContract, JoinChatWebViewDecisionForwardsUpdate) {
  auto src = td::mtproto::test::read_repo_text_file("td/telegram/UpdatesManager.cpp");
  auto region = extract_region(src, "telegram_api::updateJoinChatWebViewDecision> update,",
                               "telegram_api::updateDcOptions> update");
  auto n = normalize_for_contract(region);
  ASSERT_TRUE(n.find("send_closure(G()->td(),&Td::send_update,") != td::string::npos);
  ASSERT_TRUE(n.find("make_object<td_api::updateChatJoinResult>(") != td::string::npos);
  ASSERT_TRUE(n.find("get_chat_join_request_result_object()") != td::string::npos);
  // it must no longer be the bare no-op body
  ASSERT_TRUE(n != "telegram_api::updateJoinChatWebViewDecisionupdate,Promise<Unit>&&promise){promise.set_value(Unit());}");
}

// b449204c5 also renamed JoinChatBotResult::get_join_chat_bot_result_object ->
// get_chat_join_request_result_object. The rename must be applied consistently (declaration + definition)
// with no dangling old-name reference.
TEST(Phase2ChatJoinGuardBotContract, JoinChatBotResultGetterRenamed) {
  for (auto path : {"td/telegram/JoinChatBotResult.cpp", "td/telegram/JoinChatBotResult.h"}) {
    auto n = normalize_for_contract(td::mtproto::test::read_repo_text_file(td::Slice(path)));
    ASSERT_TRUE(n.find("get_chat_join_request_result_object()") != td::string::npos);
    ASSERT_TRUE(n.find("get_join_chat_bot_result_object") == td::string::npos);
  }
}

// TL-schema integrity for the epic: the new update type exists, the declined result exists, and the
// guard-bot approval result carries a webAppUrl (not a bare string) so same_origin can be conveyed.
TEST(Phase2ChatJoinGuardBotContract, TlSchemaHasJoinResultTypes) {
  auto n = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));
  ASSERT_TRUE(n.find("updateChatJoinResultquery_id:int64chat_id:int53result:ChatJoinRequestResult=Update;") !=
              td::string::npos);
  ASSERT_TRUE(n.find("chatJoinResultDeclined=ChatJoinResult;") != td::string::npos);
  ASSERT_TRUE(n.find("chatJoinResultGuardBotApprovalRequiredbot_user_id:int53url:webAppUrlquery_id:int53=ChatJoinResult;") !=
              td::string::npos);
}

}  // namespace
