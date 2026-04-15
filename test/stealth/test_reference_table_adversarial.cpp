// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/ReferenceTable.h"

#include "td/utils/tests.h"

namespace {

TEST(ReferenceTableAdversarial, HostMatchIsCaseInsensitiveButExact) {
  ASSERT_TRUE(td::ReferenceTable::contains_host("DNS.GOOGLE"));
  ASSERT_TRUE(td::ReferenceTable::contains_host("TCDNB.AZUREEDGE.NET"));
}

TEST(ReferenceTableAdversarial, HostRejectsSuffixConfusion) {
  ASSERT_FALSE(td::ReferenceTable::contains_host("dns.google.evil.example"));
  ASSERT_FALSE(td::ReferenceTable::contains_host("firebaseremoteconfig.googleapis.com.attacker"));
}

TEST(ReferenceTableAdversarial, HostRejectsPrefixConfusion) {
  ASSERT_FALSE(td::ReferenceTable::contains_host("evil.dns.google"));
  ASSERT_FALSE(td::ReferenceTable::contains_host("cdn.tcdnb.azureedge.net"));
}

TEST(ReferenceTableAdversarial, HostRejectsWhitespaceAndTrailingDotVariants) {
  ASSERT_FALSE(td::ReferenceTable::contains_host(" dns.google"));
  ASSERT_FALSE(td::ReferenceTable::contains_host("dns.google "));
  ASSERT_FALSE(td::ReferenceTable::contains_host("dns.google."));
}

}  // namespace