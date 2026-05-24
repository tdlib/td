// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BotAccessSettings.h"
#include "td/telegram/ManagedBotAccessSettingsAccess.h"
#include "td/telegram/ManagedBotTokenAccess.h"
#include "td/telegram/ManagedBotTokenDispatch.h"

#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

#include <memory>

namespace {

struct TokenPromiseProbe {
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

struct UnitPromiseProbe {
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

TEST(ManagedBotOwnerPathRuntime, TokenRequestAndManagerDispatchChainDelegatesWhenOwned) {
  auto probe = std::make_shared<TokenPromiseProbe>();
  bool request_rejected = false;
  bool manager_delegate_called = false;
  bool exporter_called = false;
  td::int64 looked_up_user_id = 0;
  td::int64 exported_user_id = 0;
  bool exported_revoke = false;

  auto request_result = td::dispatch_managed_bot_token_request(
      true, 9001, true, probe,
      [&](std::shared_ptr<TokenPromiseProbe> promise_probe, td::Status status) mutable {
        request_rejected = true;
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId managed_bot_user_id, bool managed_revoke,
          std::shared_ptr<TokenPromiseProbe> manager_promise) mutable {
        manager_delegate_called = true;
        auto export_result = td::dispatch_managed_bot_token_export(
            true, managed_bot_user_id.get(), managed_revoke, std::move(manager_promise),
            [&](std::shared_ptr<TokenPromiseProbe> promise_probe, td::Status status) mutable {
              request_rejected = true;
              promise_probe->set_error(std::move(status));
            },
            [&](td::UserId user_id) -> td::Result<BotDataProbe> {
              looked_up_user_id = user_id.get();
              return td::Result<BotDataProbe>(BotDataProbe{true});
            },
            [&](td::UserId user_id, bool revoke, std::shared_ptr<TokenPromiseProbe> promise_probe) mutable {
              exporter_called = true;
              exported_user_id = user_id.get();
              exported_revoke = revoke;
              promise_probe->set_value("managed-token");
            });

        ASSERT_EQ(static_cast<int>(td::ManagedBotTokenAccessResult::DelegatedToExporter),
                  static_cast<int>(export_result));
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenDispatchResult::DelegatedToManager), static_cast<int>(request_result));
  ASSERT_FALSE(request_rejected);
  ASSERT_TRUE(manager_delegate_called);
  ASSERT_TRUE(exporter_called);
  ASSERT_EQ(9001, looked_up_user_id);
  ASSERT_EQ(9001, exported_user_id);
  ASSERT_TRUE(exported_revoke);
  ASSERT_TRUE(probe->has_value);
  ASSERT_EQ("managed-token", probe->value);
  ASSERT_FALSE(probe->has_error);
}

TEST(ManagedBotOwnerPathRuntime, TokenRequestRejectsNonBotSessionBeforeManagerChain) {
  auto probe = std::make_shared<TokenPromiseProbe>();
  bool manager_delegate_called = false;

  auto request_result = td::dispatch_managed_bot_token_request(
      false, 9002, false, probe,
      [](std::shared_ptr<TokenPromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [&](td::UserId managed_bot_user_id, bool managed_revoke,
          std::shared_ptr<TokenPromiseProbe> manager_promise) mutable {
        static_cast<void>(managed_bot_user_id);
        static_cast<void>(managed_revoke);
        manager_delegate_called = true;
        manager_promise->set_value("unexpected");
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotTokenDispatchResult::RejectedNonBotSession),
            static_cast<int>(request_result));
  ASSERT_FALSE(manager_delegate_called);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Only bots can use the method", probe->error.message().str());
}

TEST(ManagedBotOwnerPathRuntime, AccessSettingsWriteChainDelegatesWithSettingsPayload) {
  auto probe = std::make_shared<UnitPromiseProbe>();
  bool delegated = false;
  td::int64 delegated_user_id = 0;

  auto write_result = td::dispatch_managed_bot_access_settings_write(
      true, 7007, td::td_api::make_object<td::td_api::botAccessSettings>(true, td::vector<td::int64>{11, 22}), probe,
      [](std::shared_ptr<UnitPromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        return td::Result<BotDataProbe>(BotDataProbe{true});
      },
      [&](td::UserId user_id, td::td_api::object_ptr<td::td_api::botAccessSettings> &&settings,
          std::shared_ptr<UnitPromiseProbe> promise_probe) mutable {
        delegated = true;
        delegated_user_id = user_id.get();

        td::BotAccessSettings parsed_settings(std::move(settings));
        auto added_user_ids = parsed_settings.get_added_user_ids();
        ASSERT_EQ(2u, added_user_ids.size());
        ASSERT_EQ(11, added_user_ids[0].get());
        ASSERT_EQ(22, added_user_ids[1].get());

        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::DelegatedToManager),
            static_cast<int>(write_result));
  ASSERT_TRUE(delegated);
  ASSERT_EQ(7007, delegated_user_id);
  ASSERT_TRUE(probe->has_value);
  ASSERT_FALSE(probe->has_error);
}

TEST(ManagedBotOwnerPathRuntime, AccessSettingsReadChainRejectsUnownedBotBeforeDelegation) {
  auto probe = std::make_shared<UnitPromiseProbe>();
  bool delegated = false;

  auto read_result = td::dispatch_managed_bot_access_settings_read(
      true, 7008, probe,
      [](std::shared_ptr<UnitPromiseProbe> promise_probe, td::Status status) mutable {
        promise_probe->set_error(std::move(status));
      },
      [](td::UserId user_id) -> td::Result<BotDataProbe> {
        static_cast<void>(user_id);
        return td::Result<BotDataProbe>(BotDataProbe{false});
      },
      [&](td::UserId user_id, std::shared_ptr<UnitPromiseProbe> promise_probe) mutable {
        static_cast<void>(user_id);
        delegated = true;
        promise_probe->set_value(td::Unit());
      });

  ASSERT_EQ(static_cast<int>(td::ManagedBotAccessSettingsAccessResult::RejectedUnownedBot),
            static_cast<int>(read_result));
  ASSERT_FALSE(delegated);
  ASSERT_FALSE(probe->has_value);
  ASSERT_TRUE(probe->has_error);
  ASSERT_EQ(400, probe->error.code());
  ASSERT_EQ("Bot must be owned", probe->error.message().str());
}

}  // namespace
