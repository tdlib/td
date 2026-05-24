// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_no_space(td::Slice source) {
  td::string out;
  out.reserve(source.size());
  for (auto c : source) {
    unsigned char b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\n' || b == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

TEST(SecurityCoreTrueIssuesContract, ip_address_uses_void_cast_direct_copy) {
  // V512 (CWE-119) fix: the intermediate sockaddr_storage staging buffer has been removed.
  // The code now casts the sockaddr* to const void* (raw-byte source of known byte-count)
  // and copies directly to ipv6_addr_ / ipv4_addr_.  Both copies are guarded by an exact
  // length check before they execute, so there is no out-of-bounds read.
  auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/IPAddress.cpp");
  auto normalized = normalize_no_space(source);

  // The intermediate staging buffer must NOT be present.
  ASSERT_EQ(td::string::npos, normalized.find("sockaddr_storagenormalized_addr{};"));
  ASSERT_EQ(td::string::npos, normalized.find("std::memcpy(&normalized_addr,addr,len);"));

  // The fixed pattern MUST be present: direct copy via const void* cast.
  ASSERT_TRUE(normalized.find("std::memcpy(&ipv6_addr_,static_cast<constvoid*>(addr),sizeof(ipv6_addr_));") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("std::memcpy(&ipv4_addr_,static_cast<constvoid*>(addr),sizeof(ipv4_addr_));") !=
              td::string::npos);
}

TEST(SecurityCoreTrueIssuesContract, tl_parser_releases_temp_node_on_nat_var_error) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  auto normalized = normalize_no_space(source);

  ASSERT_TRUE(normalized.find("TL_ERROR(\"Natvarcannotpreceedwith%%\\n\");") != td::string::npos);
  ASSERT_TRUE(normalized.find("tfree(L,sizeof(*L));") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesContract, tl_parser_change_value_var_frees_collapsed_wrapper_nodes) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  auto normalized = normalize_no_space(source);

  ASSERT_TRUE(normalized.find("tfree(O,sizeof(*O));") != td::string::npos);
  ASSERT_TRUE(normalized.find("structtl_tree_change_resultchange_value_var(") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(t.status==tl_tree_change_found){structtl_combinator_tree*left=O->left;") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("returntl_tree_change_make_updated(left);") != td::string::npos);
  ASSERT_EQ(td::string::npos, normalized.find("if(t==(void*)-1l){structtl_combinator_tree*left=O->left;"));
}

// V501/CWE-571 hardening: comment skipping in parse_lex must stop on EOF,
// otherwise a trailing "//" without newline can lock the parser in an
// unbounded loop calling nextch() forever.
TEST(SecurityCoreTrueIssuesContract, tl_parser_line_comment_scan_stops_on_eof) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  auto normalized = normalize_no_space(source);

  ASSERT_EQ(td::string::npos, normalized.find("while(nextch()!=10);"));
  ASSERT_TRUE(normalized.find("while(curch&&nextch()!=10){}") != td::string::npos);
}

// V730/CWE-457 hardening: type tags in reply-markup button structures must
// have deterministic defaults to prevent undefined behavior on early error
// paths that inspect partially constructed objects.
TEST(SecurityCoreTrueIssuesContract, reply_markup_type_tags_are_default_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h");
  auto normalized = normalize_no_space(source);

  ASSERT_EQ(td::string::npos, normalized.find("Typetype;"));
  ASSERT_TRUE(normalized.find("Typetype=Type::Text;") != td::string::npos);
  ASSERT_TRUE(normalized.find("Typetype=Type::Url;") != td::string::npos);
  ASSERT_TRUE(normalized.find("Typetype=Type::InlineKeyboard;") != td::string::npos);
}

// V730/CWE-457 hardening: tl::simple::Type discriminator must be initialized
// to a deterministic safe default in compiler-generated constructor paths.
TEST(SecurityCoreTrueIssuesContract, tl_simple_type_discriminator_is_default_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("tdtl/td/tl/tl_simple.h");
  auto normalized = normalize_no_space(source);

  ASSERT_EQ(td::string::npos, normalized.find("}type;"));
  ASSERT_TRUE(normalized.find("}type=Int32;") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesContract, query_combiner_checks_moved_to_promise) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QueryCombiner.cpp");
  auto normalized = normalize_no_space(source);

  ASSERT_TRUE(normalized.find("CHECK(send_query);") != td::string::npos);
  ASSERT_EQ(td::string::npos, normalized.find("CHECK(!query.send_query);"));
}

TEST(SecurityCoreTrueIssuesContract, session_close_avoids_strict_sync_close_assumption) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp");
  auto normalized = normalize_no_space(source);

  ASSERT_EQ(td::string::npos, normalized.find("force_close(static_cast<mtproto::SessionConnection::Callback*>(this));"
                                              "CHECK(info->state_==ConnectionInfo::State::Empty);"));
  ASSERT_TRUE(normalized.find("if(info->state_!=ConnectionInfo::State::Empty){") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesContract, cli_uses_explicit_priority_clamp_and_optional_story_sound_id) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp");
  auto normalized = normalize_no_space(source);

  ASSERT_TRUE(normalized.find("priority=max(priority,1);") != td::string::npos);
  ASSERT_TRUE(normalized.find("stringstory_sound_id_str;") != td::string::npos);
  ASSERT_TRUE(normalized.find("story_sound_id_str.empty()?-1:to_integer<int64>(story_sound_id_str)") !=
              td::string::npos);
}

// V501 (CWE-571): cli.cpp called file_log.init() twice with the same path,
// once without a size limit and once with 1000 MiB.  The first call is dead
// work: FileLog::init() is idempotent for the same path (it just adjusts the
// rotate threshold).  The fix merges both calls into a single invocation.
TEST(SecurityCoreTrueIssuesContract, cli_file_log_init_single_call_with_rotate_threshold) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp");
  auto normalized = normalize_no_space(source);

  // The double-init pattern must be gone.
  ASSERT_EQ(td::string::npos,
            normalized.find("file_log.init(file_name.str()).is_ok()&&file_log.init(file_name.str(),1000<<20).is_ok()"));

  // The single merged call must be present.
  ASSERT_TRUE(normalized.find("file_log.init(file_name.str(),1000<<20).is_ok()") != td::string::npos);
}

// V730 (CWE-457): ForumTopicManager::CreateForumTopicQuery::random_id_ must be
// zero-initialized in the class body so that on_error() is always safe to call
// even if send() was never invoked due to an early error path.
TEST(SecurityCoreTrueIssuesContract, forum_topic_random_id_zero_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ForumTopicManager.cpp");
  auto normalized = normalize_no_space(source);

  // Bare uninitialized declaration must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("int64random_id_;"));

  // Zero-initialized declaration must be present.
  ASSERT_TRUE(normalized.find("int64random_id_=0;") != td::string::npos);
}

// V730 (CWE-457): GroupCallManager::SendGroupCallMessageQuery members
// message_id_, paid_message_star_count_, and is_live_story_ must be
// zero-initialized in the class body.  paid_message_star_count_ is used in
// on_error() to adjust star accounting; an uninitialized value there would
// silently corrupt the star balance for service errors that arrive before send()
// completes its setup.
TEST(SecurityCoreTrueIssuesContract, group_call_send_message_query_members_zero_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/GroupCallManager.cpp");
  auto normalized = normalize_no_space(source);

  // Bare uninitialized declarations must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("int32message_id_;"));
  ASSERT_EQ(td::string::npos, normalized.find("int64paid_message_star_count_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolis_live_story_;"));

  // Zero-initialized declarations must be present.
  ASSERT_TRUE(normalized.find("int32message_id_=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("int64paid_message_star_count_=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolis_live_story_=false;") != td::string::npos);
}

// V730 (CWE-457): InlineQueriesManager::GetInlineBotResultsQuery::query_hash_
// and GetPreparedInlineMessageQuery::query_hash_ must be zero-initialized in
// their respective class bodies.
TEST(SecurityCoreTrueIssuesContract, inline_queries_query_hash_zero_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp");
  auto normalized = normalize_no_space(source);

  // Bare uint64 query_hash_; must be replaced by uint64 query_hash_ = 0;
  // Because both classes have the member, we check for at least two occurrences
  // of the initialized form and zero occurrences of the bare form.
  auto count_occurrences = [](const td::string &hay, const td::string &needle) -> size_t {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != td::string::npos) {
      ++count;
      pos += needle.size();
    }
    return count;
  };

  ASSERT_EQ(0u, count_occurrences(normalized, "uint64query_hash_;"));
  ASSERT_TRUE(count_occurrences(normalized, "uint64query_hash_=0;") >= 2u);
}

// V730 (CWE-457): ChatManager query handlers must initialize state members in
// class bodies to deterministic defaults, because error/retry paths can inspect
// those members before a full send() lifecycle completes.
TEST(SecurityCoreTrueIssuesContract, chat_manager_query_state_members_are_default_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp");
  auto normalized = normalize_no_space(source);

  ASSERT_EQ(td::string::npos, normalized.find("boolis_active_;"));
  ASSERT_EQ(td::string::npos, normalized.find("int32unrestrict_boost_count_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolis_all_history_available_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolcan_have_sponsored_messages_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolhas_automatic_translation_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolhas_hidden_participants_;"));
  ASSERT_EQ(td::string::npos, normalized.find("boolhas_aggressive_anti_spam_enabled_;"));
  ASSERT_EQ(td::string::npos, normalized.find("PublicDialogTypetype_;"));

  ASSERT_TRUE(normalized.find("boolis_active_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("int32unrestrict_boost_count_=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolis_all_history_available_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolcan_have_sponsored_messages_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolhas_automatic_translation_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolhas_hidden_participants_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolhas_aggressive_anti_spam_enabled_=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("PublicDialogTypetype_=PublicDialogType::ForPersonalDialog;") != td::string::npos);
}

// V730 (CWE-457): core runtime handlers must not rely on implicit/default
// constructor state for fields read on error/retry paths.
TEST(SecurityCoreTrueIssuesContract, core_runtime_query_members_are_default_initialized_core_runtime) {
  const auto sequence_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));
  const auto reaction_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageReaction.cpp"));
  const auto business_connection_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp"));
  const auto notification_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/NotificationSettingsManager.cpp"));
  const auto translation_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/TranslationManager.cpp"));

  ASSERT_EQ(td::string::npos, sequence_source.find("Statestate_;"));
  ASSERT_TRUE(sequence_source.find("Statestate_=State::Start;") != td::string::npos);

  ASSERT_EQ(td::string::npos, reaction_source.find("int64star_count_;"));
  ASSERT_TRUE(reaction_source.find("int64star_count_=0;") != td::string::npos);

  ASSERT_EQ(td::string::npos,
            business_connection_source.find("classGetBusinessStarTransferPaymentFormQueryfinal:publicTd::ResultHandler{"
                                            "Promise<Unit>promise_;BusinessConnectionIdbusiness_connection_id_;"
                                            "int64star_count_;"));
  ASSERT_TRUE(
      business_connection_source.find("classGetBusinessStarTransferPaymentFormQueryfinal:publicTd::ResultHandler{"
                                      "Promise<Unit>promise_;BusinessConnectionIdbusiness_connection_id_;"
                                      "int64star_count_=0;") != td::string::npos);

  ASSERT_EQ(td::string::npos, notification_source.find("classGetScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                                       "Promise<Unit>promise_;NotificationSettingsScopescope_;"));
  ASSERT_EQ(td::string::npos,
            notification_source.find("classUpdateScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                     "Promise<Unit>promise_;NotificationSettingsScopescope_;"));
  ASSERT_TRUE(notification_source.find("classGetScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;"
                                       "NotificationSettingsScopescope_=NotificationSettingsScope::Private;") !=
              td::string::npos);
  ASSERT_TRUE(notification_source.find("classUpdateScopeNotifySettingsQueryfinal:publicTd::ResultHandler{"
                                       "Promise<Unit>promise_;"
                                       "NotificationSettingsScopescope_=NotificationSettingsScope::Private;") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos, translation_source.find("boolskip_bot_commands_;"));
  ASSERT_EQ(td::string::npos, translation_source.find("int32max_media_timestamp_;"));
  ASSERT_TRUE(translation_source.find("boolskip_bot_commands_=false;") != td::string::npos);
  ASSERT_TRUE(translation_source.find("int32max_media_timestamp_=-1;") != td::string::npos);
}

// V730 (CWE-457): ReplyMarkup button metadata and SequenceDispatcher request
// handles must be explicitly value-initialized to deterministic safe defaults.
TEST(SecurityCoreTrueIssuesContract, reply_markup_and_sequence_dispatcher_members_are_explicitly_initialized) {
  const auto reply_markup_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/ReplyMarkup.h"));
  const auto sequence_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/SequenceDispatcher.h"));

  ASSERT_EQ(td::string::npos, reply_markup_source.find("KeyboardButtonStylestyle;"));
  ASSERT_EQ(td::string::npos, reply_markup_source.find("UserIduser_id;"));

  ASSERT_TRUE(reply_markup_source.find("KeyboardButtonStylestyle{};") != td::string::npos);
  ASSERT_TRUE(reply_markup_source.find("UserIduser_id{};") != td::string::npos);

  ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryRefnet_query_ref_;"));
  ASSERT_EQ(td::string::npos, sequence_source.find("NetQueryPtrquery_;"));
  ASSERT_EQ(td::string::npos, sequence_source.find("ActorShared<NetQueryCallback>callback_;"));

  ASSERT_TRUE(sequence_source.find("NetQueryRefnet_query_ref_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("NetQueryPtrquery_{};") != td::string::npos);
  ASSERT_TRUE(sequence_source.find("ActorShared<NetQueryCallback>callback_{};") != td::string::npos);
}

// V730 (CWE-457): request handlers in critical managers must initialize fields
// that are consumed on error/retry paths before send() can fully run.
TEST(SecurityCoreTrueIssuesContract, v730_request_handlers_request_handler_members_are_default_initialized) {
  const auto background_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BackgroundManager.cpp"));
  const auto business_connection_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp"));
  const auto inline_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/InlineQueriesManager.cpp"));
  const auto ts_file_log_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/TsFileLog.cpp"));

  ASSERT_EQ(td::string::npos,
            background_source.find("classUploadBackgroundQueryfinal:publicTd::ResultHandler{"
                                   "Promise<td_api::object_ptr<td_api::background>>promise_;"
                                   "FileUploadIdfile_upload_id_;BackgroundTypetype_;DialogIddialog_id_;"
                                   "boolfor_dark_theme_;"));
  ASSERT_TRUE(background_source.find("classUploadBackgroundQueryfinal:publicTd::ResultHandler{"
                                     "Promise<td_api::object_ptr<td_api::background>>promise_;"
                                     "FileUploadIdfile_upload_id_;BackgroundTypetype_;DialogIddialog_id_;"
                                     "boolfor_dark_theme_=false;") != td::string::npos);

  ASSERT_EQ(td::string::npos, business_connection_source.find("int64star_count_;"));
  ASSERT_TRUE(business_connection_source.find("int64star_count_=0;") != td::string::npos);

  ASSERT_EQ(td::string::npos, inline_source.find("classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
                                                 "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
                                                 "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_;"));
  ASSERT_TRUE(inline_source.find("classGetInlineBotResultsQueryfinal:publicTd::ResultHandler{"
                                 "Promise<td_api::object_ptr<td_api::inlineQueryResults>>promise_;"
                                 "DialogIddialog_id_;UserIdbot_user_id_;uint64query_hash_=0;") != td::string::npos);

  ASSERT_EQ(td::string::npos, ts_file_log_source.find("std::atomic<bool>is_inited{false};size_tid;"));
  ASSERT_TRUE(ts_file_log_source.find("std::atomic<bool>is_inited{false};size_tid{0};") != td::string::npos);
}

// V547 (CWE-571): Session::connection_send_query contained dead debug scaffolding
// that created always-true/always-false branches: the constant immediately_fail_query
// was never true so the else-branch (which skipped the real send) and the trailing
// on_message_result_error injection were both dead code.  Leaving dead code in the
// MTProto session layer creates a maintenance hazard where refactors could
// accidentally activate the error injection path, leading to silent query failures.
TEST(SecurityCoreTrueIssuesContract, session_immediately_fail_query_dead_code_is_removed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp");
  auto normalized = normalize_no_space(source);

  // Dead scaffolding constant and both conditional branches must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("constboolimmediately_fail_query=false;"));
  ASSERT_EQ(td::string::npos, normalized.find("if(!immediately_fail_query)"));
  ASSERT_EQ(td::string::npos, normalized.find("if(immediately_fail_query)"));

  // The real send path must remain intact and reachable without nesting in a dead branch.
  ASSERT_TRUE(normalized.find("message_id=info->connection_->send_query(") != td::string::npos);
  // The dead error injection must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("on_message_result_error(message_id,401,\"TEST_ERROR\");"));
}

// V730 (CWE-457): Cache-line padding arrays in concurrent data structures must be
// value-initialized.  Uninitialized padding bytes in lock-free queue nodes can
// expose kernel stack residue or previous heap object content to any reader that
// inspects raw memory (e.g., via sanitizers, crash dumps, or speculative reads).
TEST(SecurityCoreTrueIssuesContract, mpmc_queue_cache_line_padding_is_zero_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/MpmcQueue.h");
  auto normalized = normalize_no_space(source);

  // Bare (uninitialized) forms for the two primary pad types must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<uint64>)];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad2[TD_CONCURRENCY_PAD-sizeof(std::atomic<uint64>)];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad3[TD_CONCURRENCY_PAD];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<Node*>)];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad2[TD_CONCURRENCY_PAD-sizeof(std::atomic<Node*>)];"));

  // Zero-initialized forms must be present.
  ASSERT_TRUE(normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<uint64>)]={};") != td::string::npos);
  ASSERT_TRUE(normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<Node*>)]={};") != td::string::npos);
}

// V730 (CWE-457): EpochBasedMemoryReclamation ThreadData padding arrays must be
// value-initialized:  the struct is heap-allocated per-thread and uninitialized
// padding leaks previous allocator content into the reclamation epoch tracking state.
TEST(SecurityCoreTrueIssuesContract, epoch_based_memory_reclamation_pad_is_zero_initialized) {
  auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/EpochBasedMemoryReclamation.h");
  auto normalized = normalize_no_space(source);

  // Bare (uninitialized) forms must be absent.
  ASSERT_EQ(td::string::npos, normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<int64>)];"));
  ASSERT_EQ(td::string::npos,
            normalized.find("charpad2[TD_CONCURRENCY_PAD-sizeof(std::vector<unique_ptr<T>>)*MAX_BAGS];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::vector<ThreadData>)];"));
  ASSERT_EQ(td::string::npos, normalized.find("charpad2[TD_CONCURRENCY_PAD-sizeof(std::atomic<int64>)];"));

  // Zero-initialized forms must be present.
  ASSERT_TRUE(normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::atomic<int64>)]={};") != td::string::npos);
  ASSERT_TRUE(normalized.find("charpad[TD_CONCURRENCY_PAD-sizeof(std::vector<ThreadData>)]={};") != td::string::npos);
}

// V730 (CWE-457): NetStatsManager/AuthData/BufferRaw must use deterministic
// member initialization for error/early-callback safety.
TEST(SecurityCoreTrueIssuesContract, v730_core_runtime_members_are_default_initialized) {
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.h"));
  const auto buffer_source = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/buffer.h"));

  ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsDatalast_sync_stats;"));
  ASSERT_EQ(td::string::npos,
            net_stats_source.find("std::array<TypeStats,5/*NetStatsManager::net_type_size()*/>stats_by_type;"));
  ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfocommon_net_stats_;"));
  ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfomedia_net_stats_;"));
  ASSERT_EQ(td::string::npos, net_stats_source.find("NetStatsInfocall_net_stats_;"));

  ASSERT_TRUE(net_stats_source.find("NetStatsDatalast_sync_stats{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("std::array<TypeStats,5/*NetStatsManager::net_type_size()*/>stats_by_type{};") !=
              td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocommon_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfomedia_net_stats_{};") != td::string::npos);
  ASSERT_TRUE(net_stats_source.find("NetStatsInfocall_net_stats_{};") != td::string::npos);

  ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_;"));
  ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<1000>updates_duplicate_checker_;"));
  ASSERT_EQ(td::string::npos, auth_data_source.find("MessageIdDuplicateChecker<100>updates_duplicate_rechecker_;"));

  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>duplicate_checker_{};") != td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<1000>updates_duplicate_checker_{};") !=
              td::string::npos);
  ASSERT_TRUE(auth_data_source.find("MessageIdDuplicateChecker<100>updates_duplicate_rechecker_{};") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos, buffer_source.find("size_tdata_size_;"));
  ASSERT_TRUE(buffer_source.find("size_tdata_size_{0};") != td::string::npos);
}

// V730 (CWE-457): message query handlers in core messaging paths must use
// deterministic defaults for all members that can be observed by error paths.
TEST(SecurityCoreTrueIssuesContract, v730_message_handlers_message_query_members_are_default_initialized) {
  const auto message_query_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageQueryManager.cpp"));
  const auto messages_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

  ASSERT_EQ(td::string::npos,
            message_query_source.find("classSearchMessagesGlobalQueryfinal:publicTd::ResultHandler{"
                                      "Promise<td_api::object_ptr<td_api::foundMessages>>promise_;"
                                      "stringquery_;int32offset_date_{0};DialogIdoffset_dialog_id_;"
                                      "MessageIdoffset_message_id_;int32limit_{0};MessageSearchFilterfilter_;"));
  ASSERT_TRUE(message_query_source.find("classSearchMessagesGlobalQueryfinal:publicTd::ResultHandler{"
                                        "Promise<td_api::object_ptr<td_api::foundMessages>>promise_;"
                                        "stringquery_;int32offset_date_{0};DialogIdoffset_dialog_id_;"
                                        "MessageIdoffset_message_id_;int32limit_{0};MessageSearchFilterfilter_{};") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos,
            message_query_source.find("classGetMessagePositionQueryfinal:publicTd::ResultHandler{"
                                      "Promise<int32>promise_;DialogIddialog_id_;MessageIdmessage_id_;"
                                      "MessageSearchFilterfilter_;MessageTopicmessage_topic_;"));
  ASSERT_TRUE(message_query_source.find("classGetMessagePositionQueryfinal:publicTd::ResultHandler{"
                                        "Promise<int32>promise_;DialogIddialog_id_;MessageIdmessage_id_;"
                                        "MessageSearchFilterfilter_{};MessageTopicmessage_topic_;") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos,
            messages_source.find("classGetSearchResultPositionsQueryfinal:publicTd::ResultHandler{"
                                 "Promise<td_api::object_ptr<td_api::messagePositions>>promise_;DialogIddialog_id_;"
                                 "SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_;"));
  ASSERT_TRUE(messages_source.find("classGetSearchResultPositionsQueryfinal:publicTd::ResultHandler{"
                                   "Promise<td_api::object_ptr<td_api::messagePositions>>promise_;DialogIddialog_id_;"
                                   "SavedMessagesTopicIdsaved_messages_topic_id_;MessageSearchFilterfilter_{};") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos,
            messages_source.find("classGetSearchCountersQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                 "DialogIddialog_id_;MessageTopicmessage_topic_;MessageSearchFilterfilter_;"));
  ASSERT_TRUE(messages_source.find("classGetSearchCountersQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                   "DialogIddialog_id_;MessageTopicmessage_topic_;MessageSearchFilterfilter_{};") !=
              td::string::npos);

  ASSERT_EQ(td::string::npos,
            messages_source.find("classGetAllScheduledMessagesQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                                 "DialogIddialog_id_;uint32generation_;"));
  ASSERT_TRUE(
      messages_source.find("classGetAllScheduledMessagesQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                           "DialogIddialog_id_;uint32generation_{0};") != td::string::npos);

  ASSERT_EQ(td::string::npos,
            messages_source.find("classEditMessageQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                 "DialogIddialog_id_;MessageIdmessage_id_;boolis_media_;"));
  ASSERT_TRUE(messages_source.find("classEditMessageQueryfinal:publicTd::ResultHandler{Promise<int32>promise_;"
                                   "DialogIddialog_id_;MessageIdmessage_id_;boolis_media_=false;") != td::string::npos);

  ASSERT_EQ(
      td::string::npos,
      messages_source.find("classSendScreenshotNotificationQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                           "int64random_id_;DialogIddialog_id_;"));
  ASSERT_TRUE(
      messages_source.find("classSendScreenshotNotificationQueryfinal:publicTd::ResultHandler{Promise<Unit>promise_;"
                           "int64random_id_=0;DialogIddialog_id_;") != td::string::npos);
}

TEST(SecurityCoreTrueIssuesContract, v547_v730_session_ctor_init_contract) {
  const auto session_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/Session.cpp"));
  const auto auth_data_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/mtproto/AuthData.cpp"));
  const auto net_stats_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetStatsManager.h"));

  ASSERT_EQ(td::string::npos, session_source.find("input.shutdown_requested=close_flag_||logging_out_flag_;"));
  ASSERT_TRUE(session_source.find("input.shutdown_requested=logging_out_flag_;") != td::string::npos);

  ASSERT_TRUE(auth_data_source.find("AuthData::AuthData():duplicate_checker_{},updates_duplicate_checker_{},"
                                    "updates_duplicate_rechecker_{}{") != td::string::npos);

  ASSERT_TRUE(net_stats_source.find("explicitNetStatsManager(ActorShared<>parent):parent_(std::move(parent)),"
                                    "common_net_stats_{},media_net_stats_{},call_net_stats_{}{") != td::string::npos);
}
