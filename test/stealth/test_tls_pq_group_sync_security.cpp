//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::set_key_share_entry_group;
using td::mtproto::test::set_supported_group_value;

td::string build_with_seed(td::uint64 seed, const NetworkRouteHints &hints) {
  MockRng rng(seed);
  return build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints, rng);
}

TEST(TlsPqGroupSyncSecurity, CurrentLaneKeepsPqGroupSynchronizedAcrossStructures) {
  for (bool enable_ech : {false, true}) {
    NetworkRouteHints hints;
    hints.is_known = enable_ech;
    hints.is_ru = false;

    auto wire = build_with_seed(1 + static_cast<td::uint64>(enable_ech), hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                    parsed.ok().supported_groups.end());
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);

    bool has_pq_key_share = false;
    for (auto group : parsed.ok().key_share_groups) {
      if (group == td::mtproto::test::fixtures::kPqHybridGroup) {
        has_pq_key_share = true;
      }
    }
    ASSERT_TRUE(has_pq_key_share);
  }
}

TEST(TlsPqGroupSyncSecurity, RejectsPqGroupPresentOnlyInSupportedGroups) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  auto wire = build_with_seed(7, hints);
  ASSERT_TRUE(set_key_share_entry_group(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                        td::mtproto::test::fixtures::kPqHybridDraftGroup));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(TlsPqGroupSyncSecurity, RejectsPqGroupPresentOnlyInKeyShare) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  auto wire = build_with_seed(8, hints);
  ASSERT_TRUE(set_supported_group_value(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                        td::mtproto::test::fixtures::kPqHybridDraftGroup));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(TlsPqGroupSyncSecurity, RejectsPqGroupDriftOnEchDisabledRoutes) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  {
    auto wire = build_with_seed(9, hints);
    ASSERT_TRUE(set_key_share_entry_group(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                          td::mtproto::test::fixtures::kPqHybridDraftGroup));
    ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
  }

  {
    auto wire = build_with_seed(10, hints);
    ASSERT_TRUE(set_supported_group_value(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                          td::mtproto::test::fixtures::kPqHybridDraftGroup));
    ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
  }
}

}  // namespace