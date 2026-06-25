// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/common.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/tests.h"

#include <memory>

namespace {

td::vector<std::shared_ptr<td::MpscPollableQueue<td::EventFull>>> create_queues() {
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
  return {};
#else
  auto queue = std::make_shared<td::MpscPollableQueue<td::EventFull>>();
  queue->init();
  return {queue};
#endif
}

struct NotificationState {
  bool has_notification{false};
  bool saw_remove_without_add{false};
  td::vector<td::string> events;
};

NotificationState notification_state;

class NotificationProbe final : public td::Actor {
 public:
  void add_call_notification() {
    notification_state.events.push_back("add");
    notification_state.has_notification = true;
  }

  void remove_call_notification() {
    notification_state.events.push_back("remove");
    if (!notification_state.has_notification) {
      notification_state.saw_remove_without_add = true;
      return;
    }
    notification_state.has_notification = false;
  }
};

class CallNotificationSequencer final : public td::Actor {
 public:
  explicit CallNotificationSequencer(td::ActorId<NotificationProbe> probe) : probe_(probe) {
  }

  void simulate_pending_to_non_pending_same_turn() {
    td::send_closure_later(probe_, &NotificationProbe::add_call_notification);
    td::send_closure(probe_, &NotificationProbe::remove_call_notification);
  }

 private:
  td::ActorId<NotificationProbe> probe_;
};

TEST(CallNotificationSendClosureLaterIntegration, DeferredAddMustNotBeOvertakenByImmediateRemove) {
  notification_state = NotificationState{};

  td::Scheduler scheduler;
  scheduler.init(0, create_queues(), nullptr);

  auto guard = scheduler.get_guard();
  auto probe = td::create_actor<NotificationProbe>("NotificationProbe").release();
  auto sequencer = td::create_actor<CallNotificationSequencer>("CallNotificationSequencer", probe).release();

  scheduler.run_no_guard(td::Timestamp::in(1));

  td::send_closure(sequencer, &CallNotificationSequencer::simulate_pending_to_non_pending_same_turn);

  scheduler.run_no_guard(td::Timestamp::in(1));

  ASSERT_FALSE(notification_state.saw_remove_without_add);
  ASSERT_FALSE(notification_state.has_notification);
  ASSERT_EQ(2u, notification_state.events.size());
  ASSERT_EQ("add", notification_state.events[0]);
  ASSERT_EQ("remove", notification_state.events[1]);
}

}  // namespace
