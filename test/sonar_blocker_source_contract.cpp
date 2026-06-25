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

td::string extract_region(const td::string &source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = source.find(end_marker.str(), begin);
  if (end == td::string::npos) {
    return source.substr(begin);
  }
  return source.substr(begin, end - begin);
}

}  // namespace

TEST(SonarBlockerSourceContract, tl_parser_change_helpers_use_status_tagged_results_instead_of_raw_pointer_sentinels) {
  const auto parser_source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto normalized = normalize_no_space(parser_source);
  const auto change_first = normalize_no_space(
      extract_region(parser_source, "struct tl_tree_change_result change_first_var(", "int uniformize("));
  const auto change_value = normalize_no_space(extract_region(
      parser_source, "struct tl_tree_change_result change_value_var(", "int tl_parse_partial_type_app_decl("));

  ASSERT_TRUE(normalized.find("enumtl_tree_change_status") != td::string::npos);
  ASSERT_TRUE(normalized.find("structtl_tree_change_result") != td::string::npos);
  ASSERT_TRUE(normalized.find("tl_tree_change_make_error(void)") != td::string::npos);
  ASSERT_TRUE(normalized.find("tl_tree_change_make_found(void)") != td::string::npos);
  ASSERT_TRUE(normalized.find("tl_tree_change_make_unchanged(void)") != td::string::npos);
  ASSERT_TRUE(normalized.find("tl_tree_change_make_updated(structtl_combinator_tree*node)") != td::string::npos);
  ASSERT_TRUE(change_first.find("returntl_tree_change_make_found();") != td::string::npos);
  ASSERT_TRUE(change_first.find("returntl_tree_change_make_updated(") != td::string::npos);
  ASSERT_TRUE(change_first.find("tl_collapse_to_replacement_and_free_wrapper(") != td::string::npos);
  ASSERT_TRUE(change_first.find("tl_collapse_to_left_and_free_wrapper(") != td::string::npos);
  ASSERT_EQ(td::string::npos, change_first.find("(void*)-1l"));
  ASSERT_EQ(td::string::npos, change_first.find("(void*)-2l"));
  ASSERT_TRUE(change_value.find("returntl_tree_change_make_found();") != td::string::npos);
  ASSERT_TRUE(change_value.find("returntl_tree_change_make_updated(left);") != td::string::npos);
  ASSERT_EQ(td::string::npos, change_value.find("(void*)-1l"));
  ASSERT_EQ(td::string::npos, change_value.find("(void*)-2l"));
}

TEST(SonarBlockerSourceContract, telegram_forwarding_reference_lambdas_forward_value_category) {
  const auto premium_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/PremiumGiftOption.cpp"));
  const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));
  const auto auth_header = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.h"));
  const auto auth_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));
  const auto watchdog_header =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.h"));
  const auto watchdog_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.cpp"));

  ASSERT_TRUE(
      premium_source.find("PremiumGiftOption(std::forward<decltype(premium_gift_option)>(premium_gift_option))") !=
      td::string::npos);
  ASSERT_EQ(td::string::npos, premium_source.find("PremiumGiftOption(std::move(premium_gift_option))"));

  ASSERT_TRUE(star_source.find("MessageExtendedMedia(td,std::forward<decltype(media)>(media),dialog_id)") !=
              td::string::npos);
  ASSERT_EQ(td::string::npos, star_source.find("MessageExtendedMedia(td,std::move(media),dialog_id)"));
  ASSERT_EQ(td::string::npos, star_source.find("media.get_paid_media_object(td);"));
  ASSERT_TRUE(star_source.find("std::forward<decltype(media)>(media).get_paid_media_object(td)") != td::string::npos);

  ASSERT_TRUE(auth_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_EQ(td::string::npos, auth_header.find("ActorShared<>parent_;"));
  ASSERT_TRUE(auth_source.find(":parent_actor_(std::move(parent))") != td::string::npos);
  ASSERT_TRUE(auth_source.find("parent_actor_.reset();") != td::string::npos);

  ASSERT_TRUE(watchdog_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_EQ(td::string::npos, watchdog_header.find("ActorShared<>parent_;"));
  ASSERT_TRUE(watchdog_source.find("PublicRsaKeyWatchdog(ActorShared<>parent):parent_actor_(std::move(parent))") !=
              td::string::npos);
}

TEST(SonarBlockerSourceContract, tl_parser_check_constructors_equal_clears_transient_var_pointer_alias) {
  const auto parser_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c"));

  ASSERT_TRUE(parser_source.find("_T=T;tree_act_var_value(*T,check_nat_val);_T=0;return__tok;") != td::string::npos);
  ASSERT_EQ(td::string::npos, parser_source.find("_T=T;tree_act_var_value(*T,check_nat_val);return__tok;"));
}

TEST(SonarBlockerSourceContract, bench_crypto_rand_benchmark_uses_cxx11_random_engine) {
  const auto bench_source = normalize_no_space(td::mtproto::test::read_repo_text_file("benchmark/bench_crypto.cpp"));

  ASSERT_EQ(td::string::npos, bench_source.find("std::rand("));
  ASSERT_TRUE(bench_source.find("std::minstd_rand") != td::string::npos);
}

TEST(SonarBlockerSourceContract, star_manager_leaf_transaction_direction_handlers_borrow_by_const_reference) {
  const auto star_source = td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp");
  const auto stars_send = normalize_no_space(extract_region(
      star_source, "void send(DialogId dialog_id, const string &subscription_id, const string &offset, int32 limit,",
      "void send(DialogId dialog_id, const string &transaction_id, bool is_refund)"));
  const auto ton_send = normalize_no_space(extract_region(star_source, "void send(const string &offset, int32 limit,",
                                                          "void on_result(BufferSlice packet) final {"));

  ASSERT_TRUE(stars_send.find("consttd_api::object_ptr<td_api::TransactionDirection>&direction") != td::string::npos);
  ASSERT_EQ(td::string::npos, stars_send.find("td_api::object_ptr<td_api::TransactionDirection>&&direction"));
  ASSERT_TRUE(ton_send.find("consttd_api::object_ptr<td_api::TransactionDirection>&direction") != td::string::npos);
  ASSERT_EQ(td::string::npos, ton_send.find("td_api::object_ptr<td_api::TransactionDirection>&&direction"));
}

// ---------------------------------------------------------------------------
// MessageContent.cpp: lambdas with auto&& params must use std::forward<decltype(x)>(x)
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, message_content_paid_media_lambda_uses_forward_not_move) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
  ASSERT_TRUE(src.find("std::forward<decltype(extended_media)>(extended_media)") != td::string::npos);
  ASSERT_EQ(td::string::npos, src.find("MessageExtendedMedia(td,std::move(extended_media),owner_dialog_id)"));
}

TEST(SonarBlockerSourceContract, message_content_todo_completion_lambda_uses_forward_not_move) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
  ASSERT_TRUE(src.find("ToDoCompletion(std::forward<decltype(completion)>(completion))") != td::string::npos);
  ASSERT_EQ(td::string::npos, src.find("ToDoCompletion(std::move(completion))"));
}

TEST(SonarBlockerSourceContract, message_content_todo_append_tasks_lambda_uses_forward_not_move) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
  // upstream d78ceefc7 added a message_date argument to the ToDoItem ctor; the fork's forward-not-move
  // invariant for the forwarding-reference lambda parameter is unchanged (still std::forward, never std::move).
  ASSERT_TRUE(src.find("ToDoItem(user_manager,std::forward<decltype(item)>(item),message_date)") != td::string::npos);
  ASSERT_EQ(td::string::npos, src.find("ToDoItem(user_manager,std::move(item),message_date)"));
}

// ---------------------------------------------------------------------------
// SqliteKeyValue.h: get_by_range_impl must use std::forward not std::move for callback
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, sqlite_key_value_get_by_range_uses_forward_not_move) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteKeyValue.h"));
  ASSERT_TRUE(src.find("std::forward<CallbackT>(callback)") != td::string::npos);
  ASSERT_EQ(td::string::npos, src.find("std::move(callback)"));
}

// ---------------------------------------------------------------------------
// NetQuery.h: movable_atomic ctor must use std::move not std::forward
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, net_query_movable_atomic_ctor_uses_move_not_forward) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetQuery.h"));
  // movable_atomic(T &&x) ctor: T is class template param, not deduced → must use std::move
  ASSERT_TRUE(src.find("movable_atomic(T&&x):std::atomic<T>(std::move(x))") != td::string::npos);
  ASSERT_EQ(td::string::npos, src.find("movable_atomic(T&&x):std::atomic<T>(std::forward<T>(x))"));
}

// ---------------------------------------------------------------------------
// Closure.h: ImmediateClosure/DelayedClosure constructors use static_cast not std::forward
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, closure_h_constructors_avoid_std_forward_on_class_template_params) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/Closure.h"));
  // std::forward<ArgsT> in a non-deduced context is flagged; replace with static_cast<ArgsT&&>
  // Both ImmediateClosure and DelayedClosure constructors are covered.
  ASSERT_EQ(td::string::npos, src.find("std::forward<ArgsT>(args)..."));
}

// ---------------------------------------------------------------------------
// JsonBuilder.h: JsonObjectImpl / JsonArrayImpl ctors use static_cast not std::forward
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, json_builder_object_array_ctors_avoid_std_forward_on_class_template_param) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/JsonBuilder.h"));
  // JsonObjectImpl(F &&f) and JsonArrayImpl(F &&f): F is class template param, use static_cast<F&&>
  ASSERT_EQ(td::string::npos, src.find("JsonObjectImpl(F&&f):f_(std::forward<F>(f))"));
  ASSERT_EQ(td::string::npos, src.find("JsonArrayImpl(F&&f):f_(std::forward<F>(f))"));
}

// ---------------------------------------------------------------------------
// Promise.h: JoinPromise ctor uses static_cast not std::forward on class template params
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, promise_join_promise_ctor_avoids_std_forward_on_class_template_params) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/Promise.h"));
  // JoinPromise(ArgsT &&...arg): ArgsT is class template param, not deduced → static_cast
  ASSERT_EQ(td::string::npos, src.find("promises_(std::forward<ArgsT>(arg)...)"));
}

// ---------------------------------------------------------------------------
// MpmcWaiter.h: condition_variable_.wait must have a spurious-wakeup predicate
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, mpmc_waiter_condition_variable_wait_has_predicate) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/MpmcWaiter.h"));
  // Bare condition_variable_.wait(lock) without predicate is a spurious-wakeup vulnerability
  ASSERT_EQ(td::string::npos, src.find("condition_variable_.wait(lock);"));
  // A predicate lambda must be present
  ASSERT_TRUE(src.find("condition_variable_.wait(lock,") != td::string::npos);
}

// ---------------------------------------------------------------------------
// IPAddress.cpp: in6_addr comparison must use s6_addr byte array, not raw struct memcmp
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, ip_address_ipv6_comparison_uses_s6_addr_not_raw_sin6_addr) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/IPAddress.cpp"));
  // memcmp on in6_addr struct is UB on types with padding; must use .s6_addr byte array
  ASSERT_EQ(td::string::npos, src.find("std::memcmp(&a.ipv6_addr_.sin6_addr,&b.ipv6_addr_.sin6_addr,"));
  ASSERT_TRUE(src.find("s6_addr") != td::string::npos);
}

// ---------------------------------------------------------------------------
// tl-parser.c: parse_args2 must free S before load_parse when S is non-null and ':' is absent
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, tl_parser_parse_args2_frees_S_before_load_parse_on_no_colon) {
  const auto src = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto fn = extract_region(src, "struct tree *parse_args2(void)", "struct tree *parse_args1(void)");
  const auto fn_norm = normalize_no_space(fn);
  // free S before load_parse when S!=NULL and ':' is absent
  ASSERT_TRUE(fn_norm.find("if(S){tree_delete(S);") != td::string::npos);
  // The S-leak path must not exist: bare load_parse(save) in else branch without freeing S
  ASSERT_EQ(td::string::npos, fn_norm.find("}else{load_parse(save);}structparseso=save_parse();"));
}

// ---------------------------------------------------------------------------
// tl-parser.c: tl_union must free v-wrapper before returning 0 on all error paths
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, tl_parser_tl_union_frees_v_on_error_paths) {
  const auto src = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto fn =
      extract_region(src, "struct tl_combinator_tree *tl_union(", "struct tl_combinator_tree *alloc_ctree_node(");
  const auto fn_norm = normalize_no_space(fn);
  // After v = alloc_ctree_node(), every return 0 must be preceded by tfree(v,…)
  ASSERT_TRUE(fn_norm.find("tfree(v,sizeof(*v));return0;") != td::string::npos);
  // Old bare return 0 after TL_ERROR must not exist inside tl_union
  ASSERT_EQ(td::string::npos, fn_norm.find("TL_ERROR(\"Union:typemistmatch\\n\");return0;"));
}

// ---------------------------------------------------------------------------
// tl-parser.c: tl_parse_args2 must free field_name before TL_FAIL on all paths
// ---------------------------------------------------------------------------

TEST(SonarBlockerSourceContract, tl_parser_parse_args2_frees_field_name_before_TL_FAIL) {
  const auto src = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto fn =
      extract_region(src, "struct tl_combinator_tree *tl_parse_args2(", "struct tl_combinator_tree *tl_parse_args134(");
  const auto fn_norm = normalize_no_space(fn);
  // field_name freed before TL_FAIL (TL_FAIL expands to 'return 0;')
  ASSERT_TRUE(fn_norm.find("tfree(field_name,0)") != td::string::npos);
}
