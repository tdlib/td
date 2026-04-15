// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/StaticCatalog.h"

#include "td/utils/SliceBuilder.h"
#include "td/utils/tests.h"

namespace {

TEST(StaticCatalogLightFuzz, ExactCatalogEntriesSurviveCaseMatrix) {
  for (size_t index = 0; index < td::StaticCatalog::endpoint_count(); index++) {
    auto domain = td::StaticCatalog::endpoint_host(index);
    ASSERT_TRUE(td::StaticCatalog::has_endpoint_host(domain));

    for (auto &ch : domain) {
      if ('a' <= ch && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
    }
    ASSERT_TRUE(td::StaticCatalog::has_endpoint_host(domain));
  }
}

TEST(StaticCatalogLightFuzz, MutationMatrixRejectsLookalikeDomains) {
  for (size_t index = 0; index < td::StaticCatalog::endpoint_count(); index++) {
    auto domain = td::StaticCatalog::endpoint_host(index);
    auto prefixed = static_cast<td::string>(PSTRING() << 'x' << domain);
    auto suffixed = static_cast<td::string>(PSTRING() << domain << 'x');
    auto delegated = static_cast<td::string>(PSTRING() << domain << ".example");

    ASSERT_FALSE(td::StaticCatalog::has_endpoint_host(prefixed));
    ASSERT_FALSE(td::StaticCatalog::has_endpoint_host(suffixed));
    ASSERT_FALSE(td::StaticCatalog::has_endpoint_host(delegated));
  }
}

}  // namespace