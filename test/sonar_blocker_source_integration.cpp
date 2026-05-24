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

TEST(SonarBlockerSourceIntegration, parser_status_results_and_forwarding_fixes_hold_together) {
  const auto parser_source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto parser_normalized = normalize_no_space(parser_source);
  const auto partial_type = normalize_no_space(
      extract_region(parser_source, "int tl_parse_partial_type_app_decl(", "int tl_parse_partial_comb_app_decl("));
  const auto partial_comb = normalize_no_space(
      extract_region(parser_source, "int tl_parse_partial_comb_app_decl(", "int tl_parse_partial_app_decl("));
  const auto premium_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/PremiumGiftOption.cpp"));
  const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));
  const auto auth_header = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.h"));
  const auto auth_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));
  const auto watchdog_header =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.h"));
  const auto watchdog_source =
      normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.cpp"));
  const auto bench_source = normalize_no_space(td::mtproto::test::read_repo_text_file("benchmark/bench_crypto.cpp"));

  ASSERT_TRUE(parser_normalized.find("structtl_tree_change_resultchange_first_var(") != td::string::npos);
  ASSERT_TRUE(parser_normalized.find("structtl_tree_change_resultchange_value_var(") != td::string::npos);
  ASSERT_TRUE(partial_type.find("tl_tree_change_resulta_change=change_value_var(A,&V);") != td::string::npos);
  ASSERT_TRUE(partial_type.find("tl_tree_change_resultb_change=change_value_var(B,&V);") != td::string::npos);
  ASSERT_TRUE(partial_comb.find("tl_tree_change_resultz_change=change_first_var(L,&K,X);") != td::string::npos);
  ASSERT_TRUE(partial_comb.find("tl_tree_change_resultzr_change=change_first_var(R,&K,X);") != td::string::npos);
  ASSERT_TRUE(
      premium_source.find("PremiumGiftOption(std::forward<decltype(premium_gift_option)>(premium_gift_option))") !=
      td::string::npos);
  ASSERT_TRUE(star_source.find("MessageExtendedMedia(td,std::forward<decltype(media)>(media),dialog_id)") !=
              td::string::npos);
  ASSERT_EQ(td::string::npos, star_source.find("media.get_paid_media_object(td);"));
  ASSERT_TRUE(star_source.find("std::forward<decltype(media)>(media).get_paid_media_object(td)") != td::string::npos);
  ASSERT_TRUE(auth_header.find("classAuthManagerfinal:publicNetActor") != td::string::npos);
  ASSERT_TRUE(auth_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_TRUE(auth_source.find("parent_actor_.reset();") != td::string::npos);
  ASSERT_TRUE(watchdog_header.find("classPublicRsaKeyWatchdogfinal:publicNetActor") != td::string::npos);
  ASSERT_TRUE(watchdog_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_TRUE(watchdog_source.find("PublicRsaKeyWatchdog(ActorShared<>parent):parent_actor_(std::move(parent))") !=
              td::string::npos);
  ASSERT_TRUE(parser_normalized.find("_T=T;tree_act_var_value(*T,check_nat_val);_T=0;return__tok;") !=
              td::string::npos);

  ASSERT_TRUE(bench_source.find("BENCH(Rand,") != td::string::npos);
  ASSERT_TRUE(bench_source.find("std::minstd_rand") != td::string::npos);
  ASSERT_EQ(td::string::npos, bench_source.find("std::rand("));
}

TEST(SonarBlockerSourceIntegration, star_manager_leaf_transaction_direction_handlers_align_with_forwarding_contracts) {
  const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));

  ASSERT_TRUE(star_source.find("send(DialogIddialog_id,conststring&subscription_id,conststring&offset,int32limit,"
                               "consttd_api::object_ptr<td_api::TransactionDirection>&direction)") != td::string::npos);
  ASSERT_TRUE(
      star_source.find(
          "send(conststring&offset,int32limit,consttd_api::object_ptr<td_api::TransactionDirection>&direction)") !=
      td::string::npos);
}

// ---------------------------------------------------------------------------
// Integration: IPAddress in6_addr equality uses s6_addr, semantics verified
// ---------------------------------------------------------------------------

#include "td/utils/port/IPAddress.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

TEST(SonarBlockerSourceIntegration, ipv6_equality_identical_addresses_are_equal) {
  td::IPAddress a, b;
  ASSERT_TRUE(a.init_ipv6_port("::1", 443).is_ok());
  ASSERT_TRUE(b.init_ipv6_port("::1", 443).is_ok());
  ASSERT_TRUE(a == b);
  ASSERT_FALSE(a != b);
  ASSERT_FALSE(a < b);
  ASSERT_FALSE(b < a);
}

TEST(SonarBlockerSourceIntegration, ipv6_equality_different_addresses_are_not_equal) {
  td::IPAddress a, b;
  ASSERT_TRUE(a.init_ipv6_port("::1", 443).is_ok());
  ASSERT_TRUE(b.init_ipv6_port("::2", 443).is_ok());
  ASSERT_FALSE(a == b);
  ASSERT_TRUE(a != b);
}

TEST(SonarBlockerSourceIntegration, ipv6_equality_different_ports_differ) {
  td::IPAddress a, b;
  ASSERT_TRUE(a.init_ipv6_port("::1", 443).is_ok());
  ASSERT_TRUE(b.init_ipv6_port("::1", 80).is_ok());
  ASSERT_FALSE(a == b);
  ASSERT_TRUE(a != b);
}

TEST(SonarBlockerSourceIntegration, ipv6_ordering_is_consistent) {
  td::IPAddress a, b;
  ASSERT_TRUE(a.init_ipv6_port("2001:db8::1", 443).is_ok());
  ASSERT_TRUE(b.init_ipv6_port("2001:db8::2", 443).is_ok());
  // Exactly one of (a<b) or (b<a) must be true; neither both nor neither
  bool ab = (a < b);
  bool ba = (b < a);
  ASSERT_TRUE(ab != ba);
}

TEST(SonarBlockerSourceIntegration, ipv6_all_zeros_equals_itself) {
  td::IPAddress a, b;
  ASSERT_TRUE(a.init_ipv6_port("::", 1).is_ok());
  ASSERT_TRUE(b.init_ipv6_port("::", 1).is_ok());
  ASSERT_TRUE(a == b);
}
