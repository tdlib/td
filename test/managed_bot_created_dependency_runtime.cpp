// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/RepliedMessageInfo.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace {

td::telegram_api::object_ptr<td::telegram_api::MessageAction> parse_managed_bot_created_action(td::int64 bot_user_id) {
  td::TlStorerCalcLength calc;
  calc.store_int(td::telegram_api::messageActionManagedBotCreated::ID);
  calc.store_long(bot_user_id);

  td::BufferSlice payload(calc.get_length());
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  storer.store_int(td::telegram_api::messageActionManagedBotCreated::ID);
  storer.store_long(bot_user_id);

  td::TlBufferParser parser(&payload);
  auto action = td::telegram_api::MessageAction::fetch(parser);
  CHECK(parser.get_error() == nullptr);
  parser.fetch_end();
  CHECK(parser.get_error() == nullptr);
  return action;
}

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

TEST(ManagedBotCreatedDependencyRuntime, InvalidBotUserIdIsIgnoredByDependencySeam) {
  td::Dependencies dependencies;
  td::add_managed_bot_created_dependencies(dependencies, td::UserId());

  ASSERT_TRUE(dependencies.get_user_ids().empty());
}

TEST(ManagedBotCreatedDependencyRuntime, DuplicateBotUserIdCollapsesToSingleDependency) {
  td::Dependencies dependencies;
  auto bot_user_id = td::UserId(static_cast<td::int64>(777003));

  td::add_managed_bot_created_dependencies(dependencies, bot_user_id);
  td::add_managed_bot_created_dependencies(dependencies, bot_user_id);

  ASSERT_EQ(1u, dependencies.get_user_ids().size());
  ASSERT_TRUE(dependencies.get_user_ids().count(bot_user_id) == 1);
}

TEST(ManagedBotCreatedDependencyRuntime, ActionContentKeepsBotUserIdAcrossPublicDependencyApis) {
  auto bot_user_id = td::UserId(static_cast<td::int64>(777004));
  auto content = td::get_action_message_content(nullptr, parse_managed_bot_created_action(bot_user_id.get()),
                                                td::DialogId(), 0, td::RepliedMessageInfo(), false);

  ASSERT_TRUE(content != nullptr);
  ASSERT_EQ(td::MessageContentType::ManagedBotCreated, content->get_type());

  auto min_user_ids = td::get_message_content_min_user_ids(nullptr, content.get());
  ASSERT_EQ(1u, min_user_ids.size());
  ASSERT_EQ(bot_user_id, min_user_ids[0]);

  td::Dependencies dependencies;
  td::add_message_content_dependencies(dependencies, content.get(), td::UserId(), false);
  ASSERT_EQ(1u, dependencies.get_user_ids().size());
  ASSERT_TRUE(dependencies.get_user_ids().count(bot_user_id) == 1);
}

TEST(ManagedBotCreatedDependencyRuntime, InvalidActionFallsBackWithoutLeakingDependencies) {
  auto content = td::get_action_message_content(nullptr, parse_managed_bot_created_action(0), td::DialogId(), 0,
                                                td::RepliedMessageInfo(), false);

  ASSERT_TRUE(content != nullptr);
  ASSERT_EQ(td::MessageContentType::Text, content->get_type());
  ASSERT_TRUE(td::get_message_content_min_user_ids(nullptr, content.get()).empty());

  td::Dependencies dependencies;
  td::add_message_content_dependencies(dependencies, content.get(), td::UserId(), false);
  ASSERT_TRUE(dependencies.get_user_ids().empty());
}

}  // namespace
