//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/impl/Actor-decl.h"
#include "td/actor/impl/ActorInfo-decl.h"
#include "td/actor/impl/Scheduler-decl.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/Heap.h"
#include "td/utils/List.h"
#include "td/utils/logging.h"
#include "td/utils/ObjectPool.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <atomic>
#include <memory>
#include <utility>

namespace td {

inline StringBuilder &operator<<(StringBuilder &sb, const ActorInfo &info) {
  sb << info.get_name() << ":" << const_cast<void *>(static_cast<const void *>(&info)) << ":"
     << const_cast<void *>(static_cast<const void *>(info.get_context()));
  return sb;
}

inline void ActorInfo::init(int32 sched_id, Slice name, ObjectPool<ActorInfo>::OwnerPtr &&this_ptr, Actor *actor_ptr,
                            Deleter deleter, bool is_lite) {
  CHECK(!is_running());
  CHECK(!is_migrating());
  sched_id_.store(sched_id, std::memory_order_relaxed);
  actor_ = actor_ptr;

  if (!is_lite) {
    context_ = Scheduler::context()->this_ptr_.lock();
#ifdef TD_DEBUG
    name_ = name.str();
#endif
  }

  actor_->init(std::move(this_ptr));
  deleter_ = deleter;
  is_lite_ = is_lite;
  is_running_ = false;
  wait_generation_ = 0;
}
inline bool ActorInfo::is_lite() const {
  return is_lite_;
}
inline void ActorInfo::set_wait_generation(uint32 wait_generation) {
  wait_generation_ = wait_generation;
}
inline bool ActorInfo::must_wait(uint32 wait_generation) const {
  return wait_generation_ == wait_generation || (always_wait_for_mailbox_ && !mailbox_.empty());
}
inline void ActorInfo::always_wait_for_mailbox() {
  always_wait_for_mailbox_ = true;
}
inline void ActorInfo::on_actor_moved(Actor *actor_new_ptr) {
  actor_ = actor_new_ptr;
}

inline void ActorInfo::clear() {
  //  LOG_IF(WARNING, !mailbox_.empty()) << "Destroy actor with non-empty mailbox: " << get_name()
  //                                     << format::as_array(mailbox_);
  CHECK(mailbox_.empty());
  CHECK(!actor_);
  CHECK(!is_running());
  CHECK(!is_migrating());
  // NB: must be in non migrating state
  // store invalid scheduler id.
  sched_id_.store((1 << 30) - 1, std::memory_order_relaxed);
  context_.reset();
}

inline void ActorInfo::destroy_actor() {
  if (!actor_) {
    return;
  }
  switch (deleter_) {
    case Deleter::Destroy:
      std::default_delete<Actor>()(actor_);
      break;
    case Deleter::None:
      break;
  }
  actor_ = nullptr;
  mailbox_.clear();
}

template <class ActorT>
ActorOwn<ActorT> ActorInfo::transfer_ownership_to_scheduler(unique_ptr<ActorT> actor) {
  CHECK(!empty());
  CHECK(deleter_ == Deleter::None);
  ActorT *actor_ptr = actor.release();
  CHECK(actor_ == static_cast<Actor *>(actor_ptr));
  actor_ = static_cast<Actor *>(actor_ptr);
  deleter_ = Deleter::Destroy;
  return ActorOwn<ActorT>(actor_id(actor_ptr));
}

inline bool ActorInfo::empty() const {
  return actor_ == nullptr;
}

inline void ActorInfo::start_migrate(int32 to_sched_id) {
  sched_id_.store(to_sched_id | (1 << 30), std::memory_order_relaxed);
}
inline std::pair<int32, bool> ActorInfo::migrate_dest_flag_atomic() const {
  int32 sched_id = sched_id_.load(std::memory_order_relaxed);
  return std::make_pair(sched_id & ~(1 << 30), (sched_id & (1 << 30)) != 0);
}
inline void ActorInfo::finish_migrate() {
  sched_id_.store(migrate_dest(), std::memory_order_relaxed);
}
inline bool ActorInfo::is_migrating() const {
  return migrate_dest_flag_atomic().second;
}
inline int32 ActorInfo::migrate_dest() const {
  return migrate_dest_flag_atomic().first;
}

inline ActorId<> ActorInfo::actor_id() {
  return actor_id(actor_);
}

template <class SelfT>
ActorId<SelfT> ActorInfo::actor_id(SelfT *self) {
  return actor_->actor_id(self);
}

inline Actor *ActorInfo::get_actor_unsafe() {
  return actor_;
}
inline const Actor *ActorInfo::get_actor_unsafe() const {
  return actor_;
}

inline std::shared_ptr<ActorContext> ActorInfo::set_context(std::shared_ptr<ActorContext> context) {
  CHECK(is_running());
  context->this_ptr_ = context;
  context->tag_ = Scheduler::context()->tag_;
  std::swap(context_, context);
  Scheduler::context() = context_.get();
  Scheduler::on_context_updated();
  return context;
}
inline const ActorContext *ActorInfo::get_context() const {
  return context_.get();
}

inline ActorContext *ActorInfo::get_context() {
  return context_.get();
}

inline CSlice ActorInfo::get_name() const {
#ifdef TD_DEBUG
  return name_;
#else
  return "";
#endif
}

inline void ActorInfo::start_run() {
  VLOG(actor) << "Start run actor: " << *this;
  LOG_CHECK(!is_running_) << "Recursive call of actor " << tag("name", get_name());
  is_running_ = true;
}
inline void ActorInfo::finish_run() {
  is_running_ = false;
  if (!empty()) {
    VLOG(actor) << "Stop run actor: " << *this;
  }
}

inline bool ActorInfo::is_running() const {
  return is_running_;
}

inline HeapNode *ActorInfo::get_heap_node() {
  return this;
}
inline const HeapNode *ActorInfo::get_heap_node() const {
  return this;
}
inline ActorInfo *ActorInfo::from_heap_node(HeapNode *node) {
  return static_cast<ActorInfo *>(node);
}
inline ListNode *ActorInfo::get_list_node() {
  return this;
}
inline const ListNode *ActorInfo::get_list_node() const {
  return this;
}
inline ActorInfo *ActorInfo::from_list_node(ListNode *node) {
  return static_cast<ActorInfo *>(node);
}

}  // namespace td
