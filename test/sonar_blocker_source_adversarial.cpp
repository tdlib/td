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

TEST(SonarBlockerSourceAdversarial, raw_pointer_sentinel_and_forwarding_reference_regressions_are_rejected) {
  const auto parser_source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto parser_change_regions = normalize_no_space(
      extract_region(parser_source, "struct tl_tree_change_result change_first_var(",
                     "int tl_parse_partial_type_app_decl(") +
      extract_region(parser_source, "int tl_parse_partial_type_app_decl(", "int tl_parse_partial_comb_app_decl(") +
      extract_region(parser_source, "int tl_parse_partial_comb_app_decl(", "int tl_parse_partial_app_decl("));
  const auto premium_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/PremiumGiftOption.cpp"));
  const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));
  const auto auth_header = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.h"));
  const auto watchdog_header =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.h"));
  const auto bench_source = normalize_no_space(td::mtproto::test::read_repo_text_file("benchmark/bench_crypto.cpp"));

  ASSERT_EQ(td::string::npos, parser_change_regions.find("if(t==(void*)-1l)"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("if(t!=(void*)-2l)"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("if(A==(void*)-1l)"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("assert(B!=(void*)-1l)"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("return(void*)-1l;"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("return(void*)-2l;"));
  ASSERT_EQ(td::string::npos, premium_source.find("PremiumGiftOption(std::move(premium_gift_option))"));
  ASSERT_EQ(td::string::npos, star_source.find("MessageExtendedMedia(td,std::move(media),dialog_id)"));
  ASSERT_EQ(td::string::npos, star_source.find("media.get_paid_media_object(td);"));
  ASSERT_EQ(td::string::npos, auth_header.find("ActorShared<>parent_;"));
  ASSERT_EQ(td::string::npos, watchdog_header.find("ActorShared<>parent_;"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("returnO;"));
  ASSERT_EQ(td::string::npos, parser_change_regions.find("_T=T;tree_act_var_value(*T,check_nat_val);return__tok;"));
  ASSERT_EQ(td::string::npos, bench_source.find("res^=std::rand();"));
}

TEST(SonarBlockerSourceAdversarial,
     star_manager_leaf_transaction_direction_handlers_reject_move_only_contract_regressions) {
  const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));

  ASSERT_EQ(td::string::npos,
            star_source.find("send(DialogIddialog_id,conststring&subscription_id,conststring&offset,int32limit,td_api::"
                             "object_ptr<td_api::TransactionDirection>&&direction)"));
  ASSERT_EQ(td::string::npos,
            star_source.find(
                "send(conststring&offset,int32limit,td_api::object_ptr<td_api::TransactionDirection>&&direction)"));
}

TEST(SonarBlockerSourceAdversarial, forwarding_reference_lambda_move_and_forward_regressions_rejected) {
  const auto msg_src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
  const auto sqlite_src = normalize_no_space(td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteKeyValue.h"));
  const auto netq_src = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/NetQuery.h"));
  const auto closure_src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/Closure.h"));
  const auto json_src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/JsonBuilder.h"));
  const auto promise_src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/Promise.h"));
  const auto waiter_src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/MpmcWaiter.h"));

  // MessageContent: must not use std::move on auto&& lambda params
  ASSERT_EQ(td::string::npos, msg_src.find("MessageExtendedMedia(td,std::move(extended_media),owner_dialog_id)"));
  ASSERT_EQ(td::string::npos, msg_src.find("ToDoCompletion(std::move(completion))"));
  ASSERT_EQ(td::string::npos, msg_src.find("ToDoItem(user_manager,std::move(item))"));

  // SqliteKeyValue: must not use std::move on forwarding ref callbackT
  ASSERT_EQ(td::string::npos, sqlite_src.find("std::move(callback)"));

  // NetQuery movable_atomic: must not use std::forward<T> on class template param T
  ASSERT_EQ(td::string::npos, netq_src.find("std::atomic<T>(std::forward<T>(x))"));

  // Closure: must not use std::forward<ArgsT> in non-deduced context
  ASSERT_EQ(td::string::npos, closure_src.find("std::forward<ArgsT>(args)..."));

  // JsonBuilder: must not use std::forward<F> in ctor where F is class template param
  ASSERT_EQ(td::string::npos, json_src.find("f_(std::forward<F>(f))"));

  // Promise JoinPromise: must not use std::forward<ArgsT> in non-deduced ctor context
  ASSERT_EQ(td::string::npos, promise_src.find("promises_(std::forward<ArgsT>(arg)...)"));

  // MpmcWaiter: must not have bare wait without predicate
  ASSERT_EQ(td::string::npos, waiter_src.find("condition_variable_.wait(lock);"));
}

TEST(SonarBlockerSourceAdversarial, ip_address_in6_addr_raw_memcmp_regression_rejected) {
  const auto src = normalize_no_space(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/IPAddress.cpp"));
  // Must not memcmp the full in6_addr struct (non-trivially-copyable padding UB)
  ASSERT_EQ(td::string::npos,
            src.find("std::memcmp(&a.ipv6_addr_.sin6_addr,&b.ipv6_addr_.sin6_addr,sizeof(a.ipv6_addr_.sin6_addr))"));
}

TEST(SonarBlockerSourceAdversarial, tl_parser_memory_leak_regressions_rejected) {
  const auto src = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto parse_args2_fn =
      normalize_no_space(extract_region(src, "struct tree *parse_args2(void)", "struct tree *parse_args1(void)"));
  const auto tl_union_fn = normalize_no_space(
      extract_region(src, "struct tl_combinator_tree *tl_union(", "struct tl_combinator_tree *alloc_ctree_node("));
  const auto tl_parse_args2_fn = normalize_no_space(extract_region(src, "struct tl_combinator_tree *tl_parse_args2(",
                                                                   "struct tl_combinator_tree *tl_parse_args134("));

  // parse_args2: bare else { load_parse(save); } without freeing S must not exist
  ASSERT_EQ(td::string::npos, parse_args2_fn.find("}else{load_parse(save);}structparseso=save_parse();"));

  // tl_union: bare return 0 after TL_ERROR without freeing v must not exist
  ASSERT_EQ(td::string::npos, tl_union_fn.find("TL_ERROR(\"Union:typemistmatch\\n\");return0;"));

  // tl_parse_args2: must not have TL_FAIL after duplicate field error WITHOUT freeing field_name
  ASSERT_EQ(td::string::npos, tl_parse_args2_fn.find("TL_ERROR(\"Duplicatefieldname%s\\n\",field_name);TL_FAIL;"));
}
