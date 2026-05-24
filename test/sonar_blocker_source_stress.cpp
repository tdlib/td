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

TEST(SonarBlockerSourceStress, repeated_reads_preserve_status_tagged_parser_and_forwarding_invariants) {
  constexpr int kIterations = 2400;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto parser_source_raw = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
    const auto parser_source = normalize_no_space(
        extract_region(parser_source_raw, "struct tl_tree_change_result change_first_var(",
                       "int tl_parse_partial_type_app_decl(") +
        extract_region(parser_source_raw, "int tl_parse_partial_type_app_decl(",
                       "int tl_parse_partial_comb_app_decl(") +
        extract_region(parser_source_raw, "int tl_parse_partial_comb_app_decl(", "int tl_parse_partial_app_decl("));
    const auto premium_source =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/PremiumGiftOption.cpp"));
    const auto star_source = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/StarManager.cpp"));
    const auto auth_header = normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.h"));
    const auto watchdog_header =
        normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/net/PublicRsaKeyWatchdog.h"));
    const auto bench_source = normalize_no_space(td::mtproto::test::read_repo_text_file("benchmark/bench_crypto.cpp"));

    ASSERT_EQ(td::string::npos, parser_source.find("if(t==(void*)-1l)"));
    ASSERT_EQ(td::string::npos, parser_source.find("if(t!=(void*)-2l)"));
    ASSERT_EQ(td::string::npos, parser_source.find("if(A==(void*)-1l)"));
    ASSERT_EQ(td::string::npos, parser_source.find("assert(B!=(void*)-1l)"));
    ASSERT_EQ(td::string::npos, parser_source.find("return(void*)-1l;"));
    ASSERT_EQ(td::string::npos, parser_source.find("return(void*)-2l;"));
    ASSERT_EQ(td::string::npos, parser_source.find("_T=T;tree_act_var_value(*T,check_nat_val);return__tok;"));
    ASSERT_EQ(td::string::npos, premium_source.find("PremiumGiftOption(std::move(premium_gift_option))"));
    ASSERT_EQ(td::string::npos, star_source.find("MessageExtendedMedia(td,std::move(media),dialog_id)"));
    ASSERT_EQ(td::string::npos, star_source.find("media.get_paid_media_object(td);"));
    ASSERT_EQ(td::string::npos,
              star_source.find("send(DialogIddialog_id,conststring&subscription_id,conststring&offset,int32limit,td_"
                               "api::object_ptr<td_api::TransactionDirection>&&direction)"));
    ASSERT_EQ(td::string::npos,
              star_source.find(
                  "send(conststring&offset,int32limit,td_api::object_ptr<td_api::TransactionDirection>&&direction)"));
    ASSERT_EQ(td::string::npos, auth_header.find("ActorShared<>parent_;"));
    ASSERT_EQ(td::string::npos, watchdog_header.find("ActorShared<>parent_;"));

    ASSERT_TRUE(parser_source.find(
                    "returntl_tree_change_make_updated(tl_collapse_to_replacement_and_free_wrapper(O,t.node));") !=
                td::string::npos);
    ASSERT_TRUE(parser_source.find("returntl_tree_change_make_updated(tl_collapse_to_left_and_free_wrapper(O));") !=
                td::string::npos);
    ASSERT_TRUE(parser_source.find("structtl_tree_change_result") != td::string::npos);
    ASSERT_TRUE(premium_source.find("std::forward<decltype(premium_gift_option)>(premium_gift_option)") !=
                td::string::npos);
    ASSERT_TRUE(star_source.find("std::forward<decltype(media)>(media)") != td::string::npos);
    ASSERT_TRUE(star_source.find("consttd_api::object_ptr<td_api::TransactionDirection>&direction") !=
                td::string::npos);
    ASSERT_TRUE(auth_header.find("ActorShared<>parent_actor_;") != td::string::npos);
    ASSERT_TRUE(watchdog_header.find("ActorShared<>parent_actor_;") != td::string::npos);

    ASSERT_EQ(td::string::npos, bench_source.find("std::rand("));
    ASSERT_TRUE(bench_source.find("std::minstd_rand") != td::string::npos);

    checksum +=
        static_cast<td::uint32>(parser_source.size() ^ premium_source.size() ^ star_source.size() ^ auth_header.size() ^
                                watchdog_header.size() ^ bench_source.size() ^ static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}
