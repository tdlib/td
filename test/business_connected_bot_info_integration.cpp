// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BusinessConnectedBot.h"
#include "td/telegram/BusinessConnectedBot.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/tests.h"

namespace {

class GlobalContextScope final {
 public:
  GlobalContextScope() : old_context_(td::Scheduler::context()), context_(std::make_shared<td::Global>()) {
    context_->this_ptr_ = context_;
    td::Scheduler::context() = context_.get();
  }

  ~GlobalContextScope() {
    td::Scheduler::context() = old_context_;
  }

 private:
  td::ActorContext *old_context_ = nullptr;
  std::shared_ptr<td::Global> context_;
};

td::telegram_api::object_ptr<td::telegram_api::connectedBot> make_connected_bot() {
  auto bot = td::telegram_api::make_object<td::telegram_api::connectedBot>();
  bot->flags_ = td::telegram_api::connectedBot::DEVICE_MASK | td::telegram_api::connectedBot::DATE_MASK |
                td::telegram_api::connectedBot::LOCATION_MASK;
  bot->bot_id_ = 777002;
  bot->recipients_ = td::telegram_api::make_object<td::telegram_api::businessBotRecipients>();
  bot->rights_ = td::telegram_api::make_object<td::telegram_api::businessBotRights>();
  bot->device_ = "Chrome on Linux";
  bot->date_ = 1711111111;
  bot->location_ = "Tbilisi";
  return bot;
}

td::BusinessConnectedBot roundtrip(td::BusinessConnectedBot bot) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  td::BusinessConnectedBot parsed;
  {
    auto guard = scheduler.get_main_guard();
    GlobalContextScope context_scope;
    auto payload = td::log_event_store(bot);
    ASSERT_TRUE(td::log_event_parse(parsed, payload.as_slice()).is_ok());
  }

  scheduler.finish();
  return parsed;
}

TEST(BusinessConnectedBotInfoIntegration, RoundTripPreservesDeviceLocationAndDate) {
  auto source = td::BusinessConnectedBot(make_connected_bot());
  auto parsed = roundtrip(source);

  ASSERT_EQ(source, parsed);
}

}  // namespace
