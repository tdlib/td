// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/PollManager.h"

#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "td/utils/tl_helpers.h"

#include <memory>

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

td::BufferSlice serialize_poll(td::PollManager &poll_manager, td::PollId poll_id) {
  td::LogEventStorerCalcLength calc;
  poll_manager.store_poll(poll_id, calc);

  td::BufferSlice payload(calc.get_length());
  td::LogEventStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  poll_manager.store_poll(poll_id, storer);
  return payload;
}

td::Result<td::PollId> parse_poll(td::PollManager &poll_manager, td::Slice payload) {
  td::LogEventParser parser(payload);
  auto poll_id = poll_manager.parse_poll(parser);
  parser.fetch_end();
  if (parser.get_status().is_error()) {
    return parser.get_status();
  }
  return poll_id;
}

td::BufferSlice serialize_server_poll_id(td::int64 raw_poll_id) {
  td::LogEventStorerCalcLength calc;
  td::store(raw_poll_id, calc);

  td::BufferSlice payload(calc.get_length());
  td::LogEventStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  td::store(raw_poll_id, storer);
  return payload;
}

}  // namespace

TEST(PollStateRuntimeIntegration, LocalPollRoundTripAcrossManagerBoundaryPreservesRuntimeState) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();

    auto writer = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());
    td::FormattedText question;
    question.text = "Wave3 persistence boundary";

    td::vector<td::FormattedText> options;
    td::FormattedText option_a;
    option_a.text = "allow";
    td::FormattedText option_b;
    option_b.text = "deny";
    options.push_back(std::move(option_a));
    options.push_back(std::move(option_b));

    auto poll_id = writer->create_poll(std::move(question), std::move(options), true, false, false, false, false, false,
                                       false, {}, td::FormattedText(), nullptr, 0, 0, false);
    ASSERT_TRUE(poll_id.is_valid());
    ASSERT_TRUE(td::PollManager::is_local_poll_id(poll_id));
    ASSERT_TRUE(writer->has_poll(poll_id));

    auto reader = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());
    ASSERT_FALSE(reader->has_poll(poll_id));

    {
      GlobalContextScope context_scope;

      auto payload = serialize_poll(*writer, poll_id);
      ASSERT_TRUE(payload.size() > 0);

      auto parsed_poll_id = parse_poll(*reader, payload.as_slice());
      ASSERT_TRUE(parsed_poll_id.is_ok());
      ASSERT_TRUE(parsed_poll_id.ok().is_valid());
      ASSERT_TRUE(td::PollManager::is_local_poll_id(parsed_poll_id.ok()));
      ASSERT_TRUE(reader->has_poll(parsed_poll_id.ok()));

      reader.reset();
      writer.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeIntegration, MissingServerPollStateDuringParseFailsClosedWithoutCrash) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();

    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      auto payload = serialize_server_poll_id(7777777);
      ASSERT_TRUE(payload.size() > 0);

      auto parsed_poll_id = parse_poll(*poll_manager, payload.as_slice());
      ASSERT_TRUE(parsed_poll_id.is_ok());

      // Fail-closed: unresolved server poll references are dropped when runtime poll state is unavailable.
      ASSERT_FALSE(parsed_poll_id.ok().is_valid());

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeIntegration, PollAccessorsFailClosedForNullContentAndContext) {
  ASSERT_FALSE(td::get_message_content_poll_id(nullptr).is_valid());
  ASSERT_FALSE(td::message_content_poll_has_media(nullptr, nullptr));
  ASSERT_FALSE(td::get_message_content_poll_is_anonymous(nullptr, nullptr));
  ASSERT_TRUE(td::get_message_content_poll_is_closed(nullptr, nullptr));
  ASSERT_FALSE(td::get_message_content_poll_can_add_option(nullptr, nullptr));
  ASSERT_FALSE(td::get_message_content_poll_has_unread_votes(nullptr, nullptr));

  // Must be a no-op in fail-closed mode.
  td::remove_message_content_poll_has_unread_votes(nullptr, nullptr);
}