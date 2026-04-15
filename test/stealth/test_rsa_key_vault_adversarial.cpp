// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/RsaKeyVault.h"

#include "td/utils/tests.h"

namespace {

TEST(RsaKeyVaultAdversarial, UnknownRoleFailsClosed) {
  auto status = td::mtproto::RsaKeyVault::unseal(static_cast<td::mtproto::VaultKeyRole>(0x7f));
  ASSERT_TRUE(status.is_error());
}

TEST(RsaKeyVaultAdversarial, UnknownRoleDoesNotPoisonPinnedRoles) {
  ASSERT_TRUE(td::mtproto::RsaKeyVault::unseal(static_cast<td::mtproto::VaultKeyRole>(0x7f)).is_error());

  auto rsa = td::mtproto::RsaKeyVault::unseal(td::mtproto::VaultKeyRole::MainMtproto).move_as_ok();
  ASSERT_EQ(td::mtproto::RsaKeyVault::expected_fingerprint(td::mtproto::VaultKeyRole::MainMtproto),
            rsa.get_fingerprint());
}

TEST(RsaKeyVaultAdversarial, IntegrityCheckRemainsStableAcrossRepeatedCalls) {
  for (td::uint32 iteration = 0; iteration < 32; iteration++) {
    ASSERT_TRUE(td::mtproto::RsaKeyVault::verify_integrity().is_ok());
  }
}

TEST(RsaKeyVaultAdversarial, RepeatedUnsealKeepsFingerprintsStable) {
  for (auto role : {td::mtproto::VaultKeyRole::MainMtproto, td::mtproto::VaultKeyRole::TestMtproto,
                    td::mtproto::VaultKeyRole::SimpleConfig}) {
    auto expected = td::mtproto::RsaKeyVault::expected_fingerprint(role);
    for (td::uint32 iteration = 0; iteration < 16; iteration++) {
      auto rsa = td::mtproto::RsaKeyVault::unseal(role).move_as_ok();
      ASSERT_EQ(expected, rsa.get_fingerprint());
    }
  }
}

}  // namespace