// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/ManagedBotAccessSettingsAccess.h"

#include "td/utils/tests.h"

#include <memory>

namespace {

struct PromiseProbe {
  bool has_value{false};
  bool has_error{false};
  td::Status error;

  void set_value(td::Unit) {
    has_value = true;
  }

  void set_error(td::Status status) {
    has_error = true;
    error = std::move(status);
  }
};

struct BotDataProbe {
  bool can_be_edited{false};
};

struct AccessSettingsProbe {
  bool restricted{false};
};

TEST(ManagedBotAccessSettingsRuntimeHarness, ReadPathNonBotSessionFailsClosedBeforeLookup) {
  auto probe = std::make_shared<PromiseProbe>();
  bool lookup_called = false;
  bool delegated = false;

  auto result = td::dispatch_managed_bot_access_settings_read(
      false, 777, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        lookup_called = true;
        return td::Result<BotDataProbe>(td::Status::Error(400, "unexpected lookup"));
      },
      [&](td::UserId user_id, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedNonBotSession),
            static_cast<int>(result));
  ASSERT_FALSE(lookup_called);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Only bots can use the method", probe->error.message().str());
}

TEST(ManagedBotAccessSettingsRuntimeHarness, ReadPathLookupErrorIsPropagatedBeforeDelegation) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;
  td::int64 looked_up_user_id = 0;

  auto result = td::dispatch_managed_bot_access_settings_read(
      true, 4242, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(td::Status::Error(400, "Bot not found"));
      },
      [&](td::UserId user_id, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedTargetLookupError),
            static_cast<int>(result));
  ASSERT_EQ(4242, looked_up_user_id);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot not found", probe->error.message().str());
}

TEST(ManagedBotAccessSettingsRuntimeHarness, ReadPathUnownedBotFailsClosedBeforeDelegation) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;
  td::int64 looked_up_user_id = 0;

  auto result = td::dispatch_managed_bot_access_settings_read(
      true, 9898, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(BotDataProbe{false});
      },
      [&](td::UserId user_id, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedUnownedBot), static_cast<int>(result));
  ASSERT_EQ(9898, looked_up_user_id);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot must be owned", probe->error.message().str());
}

TEST(ManagedBotAccessSettingsRuntimeHarness, ReadPathOwnedBotDelegatesWithOriginalArguments) {
  auto probe = std::make_shared<PromiseProbe>();
  td::int64 looked_up_user_id = 0;
  td::int64 delegated_user_id = 0;

  auto result = td::dispatch_managed_bot_access_settings_read(
      true, 123456789, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        looked_up_user_id = user_id.get();
        return td::Result<BotDataProbe>(BotDataProbe{true});
      },
      [&](td::UserId user_id, std::shared_ptr<PromiseProbe> promise_probe) mutable {
        delegated_user_id = user_id.get();
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::DelegatedToManager), static_cast<int>(result));
  ASSERT_EQ(123456789, looked_up_user_id);
  ASSERT_EQ(123456789, delegated_user_id);
  ASSERT_TRUE(probe->has_value);
  ASSERT_FALSE(probe->has_error);
}

TEST(ManagedBotAccessSettingsRuntimeHarness, WritePathNonBotSessionFailsClosedBeforeLookup) {
  auto probe = std::make_shared<PromiseProbe>();
  bool lookup_called = false;
  bool delegated = false;
  AccessSettingsProbe settings{true};

  auto result = td::dispatch_managed_bot_access_settings_write(
      false, 9191, settings, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        lookup_called = true;
        return td::Result<BotDataProbe>(td::Status::Error(400, "unexpected lookup"));
      },
      [&](td::UserId user_id, AccessSettingsProbe delegated_settings,
          std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(delegated_settings);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedNonBotSession),
            static_cast<int>(result));
  ASSERT_FALSE(lookup_called);
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Only bots can use the method", probe->error.message().str());
}

TEST(ManagedBotAccessSettingsRuntimeHarness, WritePathUnownedBotFailsClosedBeforeDelegation) {
  auto probe = std::make_shared<PromiseProbe>();
  bool delegated = false;
  AccessSettingsProbe settings{false};

  auto result = td::dispatch_managed_bot_access_settings_write(
      true, 8181, settings, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        return td::Result<BotDataProbe>(BotDataProbe{false});
      },
      [&](td::UserId user_id, AccessSettingsProbe delegated_settings,
          std::shared_ptr<PromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        static_cast<void>(delegated_settings);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedUnownedBot), static_cast<int>(result));
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot must be owned", probe->error.message().str());
}

TEST(ManagedBotAccessSettingsRuntimeHarness, WritePathOwnedBotDelegatesWithOriginalArguments) {
  auto probe = std::make_shared<PromiseProbe>();
  td::int64 delegated_user_id = 0;
  bool delegated_restricted = false;
  AccessSettingsProbe settings{true};

  auto result = td::dispatch_managed_bot_access_settings_write(
      true, 7171, settings, probe,
      [](std::shared_ptr<PromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        return td::Result<BotDataProbe>(BotDataProbe{true});
      },
      [&](td::UserId user_id, AccessSettingsProbe delegated_settings,
          std::shared_ptr<PromiseProbe> promise_probe) mutable {
        delegated_user_id = user_id.get();
        delegated_restricted = delegated_settings.restricted;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::DelegatedToManager), static_cast<int>(result));
  ASSERT_EQ(7171, delegated_user_id);
  ASSERT_TRUE(delegated_restricted);
  ASSERT_TRUE(probe->has_value);
  ASSERT_FALSE(probe->has_error);
}

}  // namespace
