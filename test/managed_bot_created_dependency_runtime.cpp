// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"
#include "td/telegram/Dependencies.h"

#include "td/utils/tests.h"

namespace {

TEST(ManagedBotCreatedDependencyRuntime, ActionContentRegistersBotUserIdDependencyAtRuntime) {
  td::Dependencies dependencies;
  td::add_managed_bot_created_dependencies(dependencies, td::UserId(static_cast<td::int64>(777001)));

  ASSERT_TRUE(dependencies.get_user_ids().count(td::UserId(static_cast<td::int64>(777001))) == 1);
}

TEST(ManagedBotCreatedDependencyRuntime, MinUserIdsExposeManagedBotReferenceAtRuntime) {
  auto user_ids = td::get_managed_bot_created_min_user_ids(td::UserId(static_cast<td::int64>(777002)));
  ASSERT_EQ(1u, user_ids.size());
  ASSERT_EQ(td::UserId(static_cast<td::int64>(777002)), user_ids[0]);
}

}  // namespace
