// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
// NOLINTNEXTLINE(misc-include-cleaner): needed for complete MessageContent type in create_poll argument.
#include "td/telegram/MessageContent.h"  // IWYU pragma: keep
#include "td/telegram/MessageEntity.h"
// NOLINTNEXTLINE(misc-include-cleaner): needed for MessageEntity::store template definitions.
#include "td/telegram/MessageEntity.hpp"  // IWYU pragma: keep
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

template <class StorerT>
void store_malformed_option_entities_payload_body(td::int64 local_poll_id, StorerT &storer) {
  bool is_closed = false;
  bool is_anonymous = true;
  bool allow_multiple_answers = false;
  bool is_quiz = false;
  bool has_open_period = false;
  bool has_close_date = false;
  bool has_explanation = false;
  bool has_question_entities = false;
  bool has_option_entities = true;
  bool has_multiple_correct_option_ids = false;
  bool has_open_answers = false;
  bool know_revoting_disabled = true;
  bool has_revoting_disabled = false;
  bool shuffle_answers = false;
  bool hide_results_until_close = false;
  bool has_explanation_media = false;

  td::store(local_poll_id, storer);

  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_closed);
  STORE_FLAG(is_anonymous);
  STORE_FLAG(allow_multiple_answers);
  STORE_FLAG(is_quiz);
  STORE_FLAG(has_open_period);
  STORE_FLAG(has_close_date);
  STORE_FLAG(has_explanation);
  STORE_FLAG(has_question_entities);
  STORE_FLAG(has_option_entities);
  STORE_FLAG(has_multiple_correct_option_ids);
  STORE_FLAG(has_open_answers);
  STORE_FLAG(know_revoting_disabled);
  STORE_FLAG(has_revoting_disabled);
  STORE_FLAG(shuffle_answers);
  STORE_FLAG(hide_results_until_close);
  STORE_FLAG(has_explanation_media);
  END_STORE_FLAGS();

  td::store(td::string("Wave3 malformed poll payload"), storer);
  td::store(td::vector<td::string>{"allow", "deny"}, storer);

  // Adversarial payload: option_entities has a different size than option_texts.
  td::vector<td::vector<td::MessageEntity>> option_entities;
  option_entities.emplace_back();
  option_entities.back().emplace_back(td::MessageEntity::Type::CustomEmoji, 0, 1, td::CustomEmojiId(td::int64{777777}));
  td::store(option_entities, storer);
}

td::BufferSlice serialize_malformed_option_entities_payload(td::PollId local_poll_id) {
  td::LogEventStorerCalcLength calc;
  store_malformed_option_entities_payload_body(local_poll_id.get(), calc);

  td::BufferSlice payload(calc.get_length());
  td::LogEventStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  store_malformed_option_entities_payload_body(local_poll_id.get(), storer);
  return payload;
}

td::PollId create_local_poll_for_test(td::PollManager &poll_manager) {
  td::FormattedText question;
  question.text = "Wave3 adversarial parse";

  td::vector<td::FormattedText> options;
  td::FormattedText option_a;
  option_a.text = "allow";
  td::FormattedText option_b;
  option_b.text = "deny";
  options.push_back(std::move(option_a));
  options.push_back(std::move(option_b));

  return poll_manager.create_poll(std::move(question), std::move(options), true, false, false, false, false, false,
                                  false, {}, td::FormattedText(), nullptr, 0, 0, false);
}

td::PollId create_local_quiz_poll_for_test(td::PollManager &poll_manager, td::vector<td::int32> correct_option_ids) {
  td::FormattedText question;
  question.text = "Wave3 malformed quiz poll";

  td::vector<td::FormattedText> options;
  td::FormattedText option_a;
  option_a.text = "yes";
  td::FormattedText option_b;
  option_b.text = "no";
  options.push_back(std::move(option_a));
  options.push_back(std::move(option_b));

  return poll_manager.create_poll(std::move(question), std::move(options), true, false, false, false, false, false,
                                  true, std::move(correct_option_ids), td::FormattedText(), nullptr, 0, 0, false);
}

}  // namespace

TEST(PollStateRuntimeAdversarial, MismatchedOptionEntityVectorFailsClosedWithParseError) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    auto local_poll_id = create_local_poll_for_test(*poll_manager);
    ASSERT_TRUE(local_poll_id.is_valid());
    ASSERT_TRUE(td::PollManager::is_local_poll_id(local_poll_id));

    {
      GlobalContextScope context_scope;

      auto payload = serialize_malformed_option_entities_payload(local_poll_id);
      ASSERT_TRUE(payload.size() > 0);

      auto parsed_poll_id = parse_poll(*poll_manager, payload.as_slice());
      ASSERT_TRUE(parsed_poll_id.is_error());

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, TruncatedSerializedLocalPollFailsClosedWithParseError) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    auto local_poll_id = create_local_poll_for_test(*poll_manager);
    ASSERT_TRUE(local_poll_id.is_valid());

    {
      GlobalContextScope context_scope;

      auto payload = serialize_poll(*poll_manager, local_poll_id);
      ASSERT_TRUE(payload.size() > 1);

      auto truncated_payload = payload.as_slice().str();
      truncated_payload.pop_back();

      auto parsed_poll_id = parse_poll(*poll_manager, td::Slice(truncated_payload));
      ASSERT_TRUE(parsed_poll_id.is_error());

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, HasInputMediaFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA11;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_FALSE(poll_manager->has_input_media(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetPollHasUnreadVotesFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA13;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_FALSE(poll_manager->get_poll_has_unread_votes(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, RemovePollHasUnreadVotesIgnoresUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA14;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      poll_manager->remove_poll_has_unread_votes(unknown_poll_id);
      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetPollCanViewStatsFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA15;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_FALSE(poll_manager->get_poll_can_view_stats(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetPollCanAddOptionFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA16;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_FALSE(poll_manager->get_poll_can_add_option(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetPollIsClosedFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA17;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_TRUE(poll_manager->get_poll_is_closed(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetPollIsAnonymousFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA18;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      ASSERT_TRUE(poll_manager->get_poll_is_anonymous(unknown_poll_id));

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, GetInputMediaFailsClosedForUnknownPollIdWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      constexpr td::int64 kUnknownPollIdRaw = 0x7F00AA12;
      auto unknown_poll_id = td::PollId(kUnknownPollIdRaw);

      ASSERT_FALSE(poll_manager->has_poll(unknown_poll_id));
      auto input_media = poll_manager->get_input_media(unknown_poll_id);
      ASSERT_TRUE(input_media == nullptr);

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, QuizInputMediaRejectsOutOfRangeCorrectOptionFailClosedWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      td::vector<td::int32> malformed_correct_option_ids{5};
      auto quiz_poll_id = create_local_quiz_poll_for_test(*poll_manager, std::move(malformed_correct_option_ids));
      ASSERT_TRUE(quiz_poll_id.is_valid());
      ASSERT_TRUE(td::PollManager::is_local_poll_id(quiz_poll_id));

      const auto has_input_media = poll_manager->has_input_media(quiz_poll_id);
      ASSERT_FALSE(has_input_media);

      auto input_media = poll_manager->get_input_media(quiz_poll_id);
      ASSERT_TRUE(input_media == nullptr);

      poll_manager.reset();
    }
  }

  scheduler.finish();
}

TEST(PollStateRuntimeAdversarial, QuizInputMediaRejectsEmptyCorrectOptionsFailClosedWithoutAbort) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  {
    auto guard = scheduler.get_main_guard();
    auto poll_manager = std::make_unique<td::PollManager>(nullptr, td::ActorShared<>());

    {
      GlobalContextScope context_scope;

      td::vector<td::int32> malformed_correct_option_ids;
      auto quiz_poll_id = create_local_quiz_poll_for_test(*poll_manager, std::move(malformed_correct_option_ids));
      ASSERT_TRUE(quiz_poll_id.is_valid());
      ASSERT_TRUE(td::PollManager::is_local_poll_id(quiz_poll_id));

      ASSERT_FALSE(poll_manager->has_input_media(quiz_poll_id));

      auto input_media = poll_manager->get_input_media(quiz_poll_id);
      ASSERT_TRUE(input_media == nullptr);

      poll_manager.reset();
    }
  }

  scheduler.finish();
}
