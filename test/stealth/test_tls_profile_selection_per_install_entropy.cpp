// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Regression for PR #21 review finding 4 (F4): stable_selection_hash mixed only
// destination, time bucket, and platform hints, so the entire installed
// population on the same proxy/destination/platform/time bucket selected an
// identical profile -- a synchronized, DPI-correlatable pattern. A per-install
// salt is now mixed in. These tests prove the salt de-correlates selection
// (different installs can land on different profiles) while keeping selection
// deterministic for a fixed salt and reproducing the legacy vector at salt 0.

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/ech_route_failure_store_test_utils.h"

#include "td/utils/common.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::get_per_install_selection_salt;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::set_runtime_ech_failure_store;
using td::mtproto::stealth::reset_per_install_selection_salt_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_per_install_selection_salt;
using td::mtproto::test::EchRouteFailureMemoryKeyValue;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::Slice kPerInstallSelectionSaltStoreKey = "stealth_profile_selection_salt";

RuntimePlatformHints linux_desktop() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Linux};
}

TEST(PerInstallSelectionEntropy, DifferentSaltsDecorrelateProfileChoice) {
  reset_runtime_stealth_params_for_tests();
  reset_per_install_selection_salt_for_tests();
  SCOPE_EXIT {
    reset_per_install_selection_salt_for_tests();
  };

  // Same destination, platform, and time bucket for every "install"; only the
  // per-install salt differs. A multi-profile lane must not collapse to one
  // choice across the population.
  std::set<BrowserProfile> chosen;
  for (td::uint64 salt = 1; salt <= 96; salt++) {
    set_per_install_selection_salt(salt);
    chosen.insert(pick_runtime_profile("stable-dest.example.com", kUnixTime, linux_desktop()));
  }
  ASSERT_TRUE(chosen.size() > 1);
}

TEST(PerInstallSelectionEntropy, FixedSaltIsDeterministicAndZeroSaltIsBaseline) {
  reset_runtime_stealth_params_for_tests();
  reset_per_install_selection_salt_for_tests();
  SCOPE_EXIT {
    reset_per_install_selection_salt_for_tests();
  };

  // No store configured and salt cleared: selection is deterministic and the
  // salt is the unset sentinel (0), preserving legacy behaviour.
  ASSERT_EQ(static_cast<td::uint64>(0), get_per_install_selection_salt());
  auto baseline_a = pick_runtime_profile("dest.example.com", kUnixTime, linux_desktop());
  auto baseline_b = pick_runtime_profile("dest.example.com", kUnixTime, linux_desktop());
  ASSERT_TRUE(baseline_a == baseline_b);

  // A fixed non-zero salt is also stable across calls (per-install stickiness).
  set_per_install_selection_salt(0x9E3779B97F4A7C15ULL);
  auto salted_a = pick_runtime_profile("dest.example.com", kUnixTime, linux_desktop());
  auto salted_b = pick_runtime_profile("dest.example.com", kUnixTime, linux_desktop());
  ASSERT_TRUE(salted_a == salted_b);
}

TEST(PerInstallSelectionEntropy, StoreMintsAndPersistsStableSaltWhenUnset) {
  reset_runtime_stealth_params_for_tests();
  reset_per_install_selection_salt_for_tests();
  auto store = std::make_shared<EchRouteFailureMemoryKeyValue>();
  SCOPE_EXIT {
    set_runtime_ech_failure_store(nullptr);
    reset_per_install_selection_salt_for_tests();
  };

  ASSERT_EQ(static_cast<td::uint64>(0), get_per_install_selection_salt());
  ASSERT_TRUE(store->get(kPerInstallSelectionSaltStoreKey.str()).empty());

  set_runtime_ech_failure_store(store);

  auto minted = get_per_install_selection_salt();
  ASSERT_TRUE(minted != 0);
  ASSERT_EQ(td::to_string(minted), store->get(kPerInstallSelectionSaltStoreKey.str()));
}

TEST(PerInstallSelectionEntropy, StoreRestoresPersistedSaltIntoSelectionCache) {
  reset_runtime_stealth_params_for_tests();
  reset_per_install_selection_salt_for_tests();
  auto store = std::make_shared<EchRouteFailureMemoryKeyValue>();
  SCOPE_EXIT {
    set_runtime_ech_failure_store(nullptr);
    reset_per_install_selection_salt_for_tests();
  };

  constexpr td::uint64 kPersistedSalt = 0x123456789ABCDEF0ULL;
  store->set(kPerInstallSelectionSaltStoreKey.str(), td::to_string(kPersistedSalt));

  set_runtime_ech_failure_store(store);

  ASSERT_EQ(kPersistedSalt, get_per_install_selection_salt());
}

}  // namespace
