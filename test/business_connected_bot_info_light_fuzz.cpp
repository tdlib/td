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

#include <cstdint>
#include <random>
#include <string>

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

td::telegram_api::object_ptr<td::telegram_api::connectedBot> make_connected_bot(std::string device, td::int32 date,
                                                                                std::string location, td::int64 bot_id) {
  auto bot = td::telegram_api::make_object<td::telegram_api::connectedBot>();
  bot->flags_ = 0;
  if (!device.empty()) {
    bot->flags_ |= td::telegram_api::connectedBot::DEVICE_MASK;
  }
  if (date != 0) {
    bot->flags_ |= td::telegram_api::connectedBot::DATE_MASK;
  }
  if (!location.empty()) {
    bot->flags_ |= td::telegram_api::connectedBot::LOCATION_MASK;
  }
  bot->bot_id_ = bot_id;
  bot->recipients_ = td::telegram_api::make_object<td::telegram_api::businessBotRecipients>();
  bot->rights_ = td::telegram_api::make_object<td::telegram_api::businessBotRights>();
  bot->device_ = std::move(device);
  bot->date_ = date;
  bot->location_ = std::move(location);
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

std::string random_bytes(std::mt19937_64 &rng, size_t max_size) {
  std::uniform_int_distribution<size_t> len_dist(0, max_size);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  auto len = len_dist(rng);
  std::string result;
  result.resize(len);
  for (size_t i = 0; i < len; ++i) {
    result[i] = static_cast<char>(byte_dist(rng));
  }
  return result;
}

TEST(BusinessConnectedBotInfoLightFuzz, RoundTripPreservesArbitraryDeviceAndLocationBytes) {
  std::mt19937_64 rng(0x4242424242424242ULL);
  for (td::int64 i = 0; i < 256; ++i) {
    auto device = random_bytes(rng, 24);
    auto location = random_bytes(rng, 32);
    td::int32 date = static_cast<td::int32>(rng() & 0x7fffffff);
    if ((rng() & 1) == 0) {
      date = 0;
    }
    auto source = td::BusinessConnectedBot(make_connected_bot(std::move(device), date, std::move(location),
                                                              777100 + i));
    auto parsed = roundtrip(source);
    ASSERT_EQ(source, parsed);
  }
}

}  // namespace
