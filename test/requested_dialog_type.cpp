// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RequestedDialogType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

#include <utility>

TEST(RequestedDialogType, ManagedBot) {
  td::RequestedDialogType request(
      td::td_api::make_object<td::td_api::keyboardButtonTypeRequestManagedBot>(7, "Name", "name_bot"));
  ASSERT_TRUE(request.check_shared_user(true, false).is_ok());
  ASSERT_TRUE(request.check_shared_user(true, true).is_ok());
  ASSERT_TRUE(request.check_shared_user(false, false).is_error());
  ASSERT_TRUE(request.check_shared_user(false, true).is_error());
  ASSERT_TRUE(request.check_shared_dialog_count(0).is_error());
  ASSERT_TRUE(request.check_shared_dialog_count(1).is_ok());
  ASSERT_TRUE(request.check_shared_dialog_count(2).is_error());
}

TEST(RequestedDialogType, ReceivedManagedBot) {
  auto peer_type = td::telegram_api::make_object<td::telegram_api::requestPeerTypeCreateBot>();
  peer_type->bot_managed_ = true;
  td::RequestedDialogType request(std::move(peer_type), 7, 3);
  ASSERT_TRUE(request.check_shared_user(true, false).is_ok());
  ASSERT_TRUE(request.check_shared_user(false, false).is_error());
  ASSERT_TRUE(request.check_shared_dialog_count(0).is_error());
  ASSERT_TRUE(request.check_shared_dialog_count(1).is_ok());
  ASSERT_TRUE(request.check_shared_dialog_count(2).is_error());
}

TEST(RequestedDialogType, UserRestrictions) {
  auto users = td::td_api::make_object<td::td_api::keyboardButtonTypeRequestUsers>();
  users->restrict_user_is_bot_ = true;
  users->user_is_bot_ = false;
  users->restrict_user_is_premium_ = true;
  users->user_is_premium_ = true;
  users->max_quantity_ = 3;
  td::RequestedDialogType request(std::move(users));
  ASSERT_TRUE(request.check_shared_user(false, true).is_ok());
  ASSERT_TRUE(request.check_shared_user(true, true).is_error());
  ASSERT_TRUE(request.check_shared_user(false, false).is_error());
  ASSERT_TRUE(request.check_shared_dialog_count(3).is_ok());
  ASSERT_TRUE(request.check_shared_dialog_count(4).is_error());
}

TEST(RequestedDialogType, ChatDoesNotAcceptUser) {
  td::RequestedDialogType request(td::td_api::make_object<td::td_api::keyboardButtonTypeRequestChat>());
  ASSERT_TRUE(request.check_shared_user(false, false).is_error());
  ASSERT_TRUE(request.check_shared_user(true, false).is_error());
}
