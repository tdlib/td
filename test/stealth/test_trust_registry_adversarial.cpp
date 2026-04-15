// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/StaticCatalog.h"

#include "td/utils/tests.h"

namespace {

TEST(StaticCatalogAdversarial, EndpointMatchIsCaseInsensitiveButExact) {
  ASSERT_TRUE(td::StaticCatalog::has_endpoint_host("DNS.GOOGLE"));
  ASSERT_TRUE(td::StaticCatalog::has_endpoint_host("TCDNB.AZUREEDGE.NET"));
}

TEST(StaticCatalogAdversarial, EndpointRejectsSuffixConfusion) {
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("dns.google.evil.example"));
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("firebaseremoteconfig.googleapis.com.attacker"));
}

TEST(StaticCatalogAdversarial, EndpointRejectsPrefixConfusion) {
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("evil.dns.google"));
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("cdn.tcdnb.azureedge.net"));
}

TEST(StaticCatalogAdversarial, EndpointRejectsWhitespaceAndTrailingDotVariants) {
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host(" dns.google"));
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("dns.google "));
  ASSERT_FALSE(td::StaticCatalog::has_endpoint_host("dns.google."));
}

}  // namespace