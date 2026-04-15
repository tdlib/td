// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/RsaKeyVault.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::RsaKeyVault;
using td::mtproto::VaultKeyRole;

TEST(RsaKeyVaultContract, RegistryFingerprintsMatchPinnedValues) {
  ASSERT_EQ(static_cast<td::int64>(0xd09d1d85de64fd85ULL),
            RsaKeyVault::expected_fingerprint(VaultKeyRole::MainMtproto));
  ASSERT_EQ(static_cast<td::int64>(0xb25898df208d2603ULL),
            RsaKeyVault::expected_fingerprint(VaultKeyRole::TestMtproto));
  ASSERT_EQ(static_cast<td::int64>(0x6f3a701151477715ULL),
            RsaKeyVault::expected_fingerprint(VaultKeyRole::SimpleConfig));
}

TEST(RsaKeyVaultContract, IntegrityCheckSucceedsForBundledMaterial) {
  ASSERT_TRUE(RsaKeyVault::verify_integrity().is_ok());
}

TEST(RsaKeyVaultContract, UnsealReturnsPinnedMainKey) {
  auto rsa = RsaKeyVault::unseal(VaultKeyRole::MainMtproto).move_as_ok();
  ASSERT_EQ(RsaKeyVault::expected_fingerprint(VaultKeyRole::MainMtproto), rsa.get_fingerprint());
}

TEST(RsaKeyVaultContract, UnsealReturnsPinnedTestKey) {
  auto rsa = RsaKeyVault::unseal(VaultKeyRole::TestMtproto).move_as_ok();
  ASSERT_EQ(RsaKeyVault::expected_fingerprint(VaultKeyRole::TestMtproto), rsa.get_fingerprint());
}

TEST(RsaKeyVaultContract, UnsealReturnsPinnedRecoveryKey) {
  auto rsa = RsaKeyVault::unseal(VaultKeyRole::SimpleConfig).move_as_ok();
  ASSERT_EQ(RsaKeyVault::expected_fingerprint(VaultKeyRole::SimpleConfig), rsa.get_fingerprint());
}

}  // namespace