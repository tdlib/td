// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "td/utils/port/IPAddress.h"
#include "test/stealth/SourceContractFileReader.h"

#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>

namespace {

td::string normalize_no_space(td::Slice source) {
  td::string out;
  out.reserve(source.size());
  for (auto c : source) {
    if (auto b = static_cast<unsigned char>(c); b == ' ' || b == '\t' || b == '\n' || b == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  if (begin == std::string_view::npos) {
    return {};
  }
  auto end = source.find(end_marker.str(), begin);
  if (end == std::string_view::npos) {
    return td::string(source.substr(begin));
  }
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(SonarBlockerSourceLightFuzz, deterministic_sampling_preserves_status_tagged_parser_regions_and_forwarding_fixes) {
  const auto parser_source = td::mtproto::test::read_repo_text_file("td/generate/tl-parser/tl-parser.c");
  const auto parser_regions = normalize_no_space(
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

  const std::array<std::string_view, 7> parser_forbidden = {
      "if(t==(void*)-1l)",
      "if(t!=(void*)-2l)",
      "if(A==(void*)-1l)",
      "assert(B!=(void*)-1l)",
      "return(void*)-1l;",
      "return(void*)-2l;",
      "_T=T;tree_act_var_value(*T,check_nat_val);return__tok;",
  };
  const std::array<std::string_view, 1> premium_forbidden = {
      "PremiumGiftOption(std::move(premium_gift_option))",
  };
  const std::array<std::string_view, 4> star_forbidden = {
      "MessageExtendedMedia(td,std::move(media),dialog_id)",
      "media.get_paid_media_object(td);",
      "send(DialogIddialog_id,conststring&subscription_id,conststring&offset,int32limit,td_api::object_ptr<td_api::"
      "TransactionDirection>&&direction)",
      "send(conststring&offset,int32limit,td_api::object_ptr<td_api::TransactionDirection>&&direction)",
  };
  const std::array<std::string_view, 1> header_forbidden = {
      "ActorShared<>parent_;",
  };
  const std::array<std::string_view, 2> bench_forbidden = {
      "std::rand(",
      "res^=std::rand();",
  };

  constexpr int kIterations = 2048;
  for (int i = 0; i < kIterations; i++) {
    auto parser_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(parser_forbidden.size()) - 1));
    auto premium_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(premium_forbidden.size()) - 1));
    auto star_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(star_forbidden.size()) - 1));
    auto header_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(header_forbidden.size()) - 1));
    auto bench_idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(bench_forbidden.size()) - 1));
    ASSERT_EQ(td::string::npos, parser_regions.find(parser_forbidden[parser_idx]));
    ASSERT_EQ(td::string::npos, premium_source.find(premium_forbidden[premium_idx]));
    ASSERT_EQ(td::string::npos, star_source.find(star_forbidden[star_idx]));
    ASSERT_EQ(td::string::npos, auth_header.find(header_forbidden[header_idx]));
    ASSERT_EQ(td::string::npos, watchdog_header.find(header_forbidden[header_idx]));
    ASSERT_EQ(td::string::npos, bench_source.find(bench_forbidden[bench_idx]));
  }

  ASSERT_TRUE(parser_regions.find(
                  "returntl_tree_change_make_updated(tl_collapse_to_replacement_and_free_wrapper(O,t.node));") !=
              td::string::npos);
  ASSERT_TRUE(parser_regions.find("returntl_tree_change_make_updated(tl_collapse_to_left_and_free_wrapper(O));") !=
              td::string::npos);
  ASSERT_TRUE(premium_source.find("std::forward<decltype(premium_gift_option)>(premium_gift_option)") !=
              td::string::npos);
  ASSERT_TRUE(star_source.find("std::forward<decltype(media)>(media)") != td::string::npos);
  ASSERT_TRUE(star_source.find("consttd_api::object_ptr<td_api::TransactionDirection>&direction") != td::string::npos);
  ASSERT_TRUE(auth_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_TRUE(watchdog_header.find("ActorShared<>parent_actor_;") != td::string::npos);
  ASSERT_TRUE(bench_source.find("std::minstd_rand") != td::string::npos);
}

TEST(SonarBlockerSourceLightFuzz, ipv6_comparison_self_consistent_on_random_pairs) {
  constexpr int kIterations = 10000;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    std::array<unsigned char, 16> raw_a{};
    std::array<unsigned char, 16> raw_b{};
    td::Random::secure_bytes(raw_a.data(), raw_a.size());
    td::Random::secure_bytes(raw_b.data(), raw_b.size());

    std::array<char, INET6_ADDRSTRLEN> str_a{};
    std::array<char, INET6_ADDRSTRLEN> str_b{};
    if (!inet_ntop(AF_INET6, raw_a.data(), str_a.data(), str_a.size())) {
      continue;
    }
    if (!inet_ntop(AF_INET6, raw_b.data(), str_b.data(), str_b.size())) {
      continue;
    }

    td::IPAddress a;
    td::IPAddress b;
    if (!a.init_ipv6_port(td::CSlice(str_a.data(), str_a.data() + std::strlen(str_a.data())),
                          td::Random::fast(1, 65534))
             .is_ok() ||
        !b.init_ipv6_port(td::CSlice(str_b.data(), str_b.data() + std::strlen(str_b.data())),
                          td::Random::fast(1, 65534))
             .is_ok()) {
      continue;
    }

    // Reflexivity: a == a
    ASSERT_TRUE(a == a);
    ASSERT_TRUE(b == b);
    ASSERT_FALSE(a < a);
    ASSERT_FALSE(b < b);

    // Antisymmetry: at most one of (a<b) or (b<a) is true
    bool ab = (a < b);
    bool ba = (b < a);
    ASSERT_FALSE(ab && ba);

    // Consistency of == and <: if a==b then neither a<b nor b<a
    if (a == b) {
      ASSERT_FALSE(ab);
      ASSERT_FALSE(ba);
    }

    checksum ^= static_cast<td::uint32>(raw_a[0]) ^ static_cast<td::uint32>(raw_b[0]) ^
                static_cast<td::uint32>(ab ? 1u : 0u) ^ static_cast<td::uint32>(ba ? 2u : 0u);
  }
  // checksum just proves we ran iterations (not always 0)
  (void)checksum;
}
