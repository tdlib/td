// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/ReferenceTable.h"

#include "td/utils/SliceBuilder.h"
#include "td/utils/tests.h"

namespace {

TEST(ReferenceTableLightFuzz, ExactHostsSurviveCaseMatrix) {
  for (size_t index = 0; index < td::ReferenceTable::host_count(); index++) {
    auto domain = td::ReferenceTable::host_name(index);
    ASSERT_TRUE(td::ReferenceTable::contains_host(domain));

    for (auto &ch : domain) {
      if ('a' <= ch && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
    }
    ASSERT_TRUE(td::ReferenceTable::contains_host(domain));
  }
}

TEST(ReferenceTableLightFuzz, MutationMatrixRejectsLookalikes) {
  for (size_t index = 0; index < td::ReferenceTable::host_count(); index++) {
    auto domain = td::ReferenceTable::host_name(index);
    auto prefixed = static_cast<td::string>(PSTRING() << 'x' << domain);
    auto suffixed = static_cast<td::string>(PSTRING() << domain << 'x');
    auto delegated = static_cast<td::string>(PSTRING() << domain << ".example");

    ASSERT_FALSE(td::ReferenceTable::contains_host(prefixed));
    ASSERT_FALSE(td::ReferenceTable::contains_host(suffixed));
    ASSERT_FALSE(td::ReferenceTable::contains_host(delegated));
  }
}

}  // namespace