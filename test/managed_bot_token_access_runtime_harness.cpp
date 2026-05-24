// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/ManagedBotTokenAccess.h"
#include "td/telegram/ManagedBotTokenDispatch.h"

#include "td/utils/tests.h"

#include <memory>

namespace {

struct PromiseProbe {
  bool has_value{false};
  bool has_error{false};
  td::string value;
  td::Status error;

  void set_value(td::string token) {
    has_value = true;
    value = std::move(token);
  }

  void set_error(td::Status status) {
    has_error = true;
    error = std::move(status);
  }
};

struct BotDataProbe {
  bool can_be_edited{false};
};

TEST(ManagedBotTokenAccessRuntimeHarness, NonBotSessionFailsClosedBeforeDelegation) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;

  auto result = td::dispatch_managed_bot_token_request(
      false, 777, true, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(revoke);
        delegated = true;
        promise_probe->set_value("unexpected");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenDispatchResult::RejectedNonBotSession), static_cast<int>(result));
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Only bots can use the method", probe->error.message().str());
}

TEST(ManagedBotTokenAccessRuntimeHarness, BotSessionDelegatesWithOriginalArguments) {
  auto probe = std::make_shared<PromiseProbe>();
  bool rejected = false;
  bool delegated = false;
  td::int64 delegated_user_id = 0;
  bool delegated_revoke = false;

  auto result = td::dispatch_managed_bot_token_request(
      true, 123456789, false, probe,
      [&](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        static_cast<void>(promise_probe);
        static_cast<void>(status);
        rejected = true;
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        delegated = true;
        delegated_user_id = user_id.get();
        delegated_revoke = revoke;
        promise_probe->set_value("managed-token");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenDispatchResult::DelegatedToManager), static_cast<int>(result));
  ASSERT_FALSE(rejected);
  ASSERT_TRUE(delegated);
  ASSERT_EQ(123456789, delegated_user_id);
  ASSERT_FALSE(delegated_revoke);
  ASSERT_TRUE(probe->has_value);
  ASSERT_EQ("managed-token", probe->value);
  ASSERT_FALSE(probe->has_error);
}

TEST(ManagedBotTokenAccessRuntimeHarness, DelegatedManagerErrorIsPropagatedViaForwardedPromise) {
  auto probe = std::make_shared<PromiseProbe>();
  bool rejected = false;

  auto result = td::dispatch_managed_bot_token_request(
      true, 4242, true, probe,
      [&](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        static_cast<void>(promise_probe);
        static_cast<void>(status);
        rejected = true;
      },
      [](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(revoke);
        promise_probe->set_error(td::Status::Error(500, "Injected manager failure"));
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenDispatchResult::DelegatedToManager), static_cast<int>(result));
  ASSERT_FALSE(rejected);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(500, probe->error.code());
  ASSERT_EQ("Injected manager failure", probe->error.message().str());
}

TEST(ManagedBotTokenAccessRuntimeHarness, ManagerPathNonBotSessionFailsClosedBeforeLookup) {
  auto probe = std::make_shared<PromiseProbe>();
  bool lookup_called = false;
  bool delegated = false;

  auto result = td::dispatch_managed_bot_token_export(
      false, 777, true, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        lookup_called = true;
        return td::Result<BotDataProbe>(td::Status::Error(400, "unexpected lookup"));
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(revoke);
        delegated = true;
        promise_probe->set_value("unexpected");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenAccessResult::RejectedNonBotSession), static_cast<int>(result));
  ASSERT_FALSE(lookup_called);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Only bots can use the method", probe->error.message().str());
}

TEST(ManagedBotTokenAccessRuntimeHarness, ManagerPathLookupErrorIsPropagatedBeforeExport) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;
  td::int64 looked_up_user_id = 0;

  auto result = td::dispatch_managed_bot_token_export(
      true, 4242, false, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(td::Status::Error(400, "Bot not found"));
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(revoke);
        delegated = true;
        promise_probe->set_value("unexpected");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenAccessResult::RejectedTargetLookupError), static_cast<int>(result));
  ASSERT_EQ(4242, looked_up_user_id);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot not found", probe->error.message().str());
}

TEST(ManagedBotTokenAccessRuntimeHarness, ManagerPathUnownedBotFailsClosedBeforeExport) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;
  td::int64 looked_up_user_id = 0;

  auto result = td::dispatch_managed_bot_token_export(
      true, 9898, true, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(BotDataProbe{false});
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(revoke);
        delegated = true;
        promise_probe->set_value("unexpected");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenAccessResult::RejectedUnownedBot), static_cast<int>(result));
  ASSERT_EQ(9898, looked_up_user_id);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot must be owned", probe->error.message().str());
}

TEST(ManagedBotTokenAccessRuntimeHarness, ManagerPathOwnedBotDelegatesWithOriginalArguments) {
  auto probe = std::make_shared<PromiseProbe>();
  td::int64 looked_up_user_id = 0;
  bool delegated = false;
  td::int64 delegated_user_id = 0;
  bool delegated_revoke = false;

  auto result = td::dispatch_managed_bot_token_export(
      true, 123456789, true, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(BotDataProbe{true});
      },
      [&](td::UserId user_id, bool revoke, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        delegated = true;
        delegated_user_id = user_id.get();
        delegated_revoke = revoke;
        promise_probe->set_value("managed-token");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenAccessResult::DelegatedToExporter), static_cast<int>(result));
  ASSERT_EQ(123456789, looked_up_user_id);
  ASSERT_TRUE(delegated);
  ASSERT_EQ(123456789, delegated_user_id);
  ASSERT_TRUE(delegated_revoke);
  ASSERT_TRUE(probe->has_value);
  ASSERT_EQ("managed-token", probe->value);
  ASSERT_FALSE(probe->has_error);
}

}  // namespace