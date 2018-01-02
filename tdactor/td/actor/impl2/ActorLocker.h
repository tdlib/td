//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/impl2/ActorSignals.h"
#include "td/actor/impl2/ActorState.h"

#include "td/utils/logging.h"

#include <atomic>

namespace td {
namespace actor2 {
class ActorLocker {
 public:
  struct Options {
    Options() {
    }
    bool can_execute_paused = false;
    bool is_shared = true;
    Options &with_can_execute_paused(bool new_can_execute_paused) {
      can_execute_paused = new_can_execute_paused;
      return *this;
    }
    Options &with_is_shared(bool new_is_shared) {
      is_shared = new_is_shared;
      return *this;
    }
  };
  explicit ActorLocker(ActorState *state, Options options = {})
      : state_(state), flags_(state->get_flags_unsafe()), new_flags_{}, options_{options} {
  }
  bool try_lock() {
    CHECK(!own_lock());
    while (!can_try_add_signals()) {
      new_flags_ = flags_;
      new_flags_.set_locked(true);
      new_flags_.clear_signals();
      if (state_->state_.compare_exchange_strong(flags_.raw_ref(), new_flags_.raw(), std::memory_order_acq_rel)) {
        own_lock_ = true;
        return true;
      }
    }
    return false;
  }
  bool try_unlock(ActorState::Flags flags) {
    CHECK(!flags.is_locked());
    CHECK(own_lock());
    // can't unlock with signals set
    //CHECK(!flags.has_signals());

    flags_ = flags;
    //try unlock
    if (state_->state_.compare_exchange_strong(new_flags_.raw_ref(), flags.raw(), std::memory_order_acq_rel)) {
      own_lock_ = false;
      return true;
    }

    // read all signals
    flags.set_locked(true);
    flags.clear_signals();
    do {
      flags_.add_signals(new_flags_.get_signals());
    } while (!state_->state_.compare_exchange_strong(new_flags_.raw_ref(), flags.raw(), std::memory_order_acq_rel));
    new_flags_ = flags;
    return false;
  }

  bool try_add_signals(ActorSignals signals) {
    CHECK(!own_lock());
    CHECK(can_try_add_signals());
    new_flags_ = flags_;
    new_flags_.add_signals(signals);
    return state_->state_.compare_exchange_strong(flags_.raw_ref(), new_flags_.raw(), std::memory_order_acq_rel);
  }
  bool add_signals(ActorSignals signals) {
    CHECK(!own_lock());
    while (true) {
      if (can_try_add_signals()) {
        if (try_add_signals(signals)) {
          return false;
        }
      } else {
        if (try_lock()) {
          flags_.add_signals(signals);
          return true;
        }
      }
    }
  }
  bool own_lock() const {
    return own_lock_;
  }
  ActorState::Flags flags() const {
    return flags_;
  }
  bool can_execute() const {
    return flags_.is_shared() == options_.is_shared && (options_.can_execute_paused || !flags_.is_pause());
  }

 private:
  ActorState *state_{nullptr};
  ActorState::Flags flags_;
  ActorState::Flags new_flags_;
  bool own_lock_{false};
  Options options_;

  bool can_try_add_signals() const {
    return flags_.is_locked() || (flags_.is_in_queue() && !can_execute());
  }
};
}  // namespace actor2
}  // namespace td
