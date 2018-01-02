//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

namespace td {
namespace actor2 {
class ActorSignals {
 public:
  ActorSignals() = default;
  uint32 raw() const {
    return raw_;
  }
  bool empty() const {
    return raw_ == 0;
  }
  bool has_signal(uint32 signal) const {
    return (raw_ & (1u << signal)) != 0;
  }
  void add_signal(uint32 signal) {
    raw_ |= (1u << signal);
  }
  void add_signals(ActorSignals signals) {
    raw_ |= signals.raw();
  }
  void clear_signal(uint32 signal) {
    raw_ &= ~(1u << signal);
  }
  uint32 first_signal() {
    if (!raw_) {
      return 0;
    }
#if TD_MSVC
    int res = 0;
    int bit = 1;
    while ((raw_ & bit) == 0) {
      res++;
      bit <<= 1;
    }
    return res;
#else
    return __builtin_ctz(raw_);
#endif
  }
  enum Signal : uint32 {
    // Signals in order of priority
    Wakeup = 1,
    Alarm = 2,
    Kill = 3,  // immediate kill
    Io = 4,    // move to io thread
    Cpu = 5,   // move to cpu thread
    StartUp = 6,
    TearDown = 7,
    // Two signals for mpmc queue logic
    //
    // PopSignal is set after actor is popped from queue
    // When processed it should set InQueue and Pause flags to false.
    //
    // MessagesSignal is set after new messages was added to actor
    // If owner of actor wish to delay message handling, she should set InQueue flag to true and
    // add actor into mpmc queue.
    Pop = 8,      // got popped from queue
    Message = 9,  // got new message
  };

  static ActorSignals one(uint32 signal) {
    ActorSignals res;
    res.add_signal(signal);
    return res;
  }

 private:
  uint32 raw_{0};
  friend class ActorState;
  explicit ActorSignals(uint32 raw) : raw_(raw) {
  }
};
}  // namespace actor2
}  // namespace td
