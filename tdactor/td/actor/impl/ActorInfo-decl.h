//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/impl/ActorId-decl.h"
#include "td/actor/impl/Event.h"

#include "td/utils/common.h"
#include "td/utils/Heap.h"
#include "td/utils/List.h"
#include "td/utils/ObjectPool.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <atomic>
#include <memory>
#include <utility>

namespace td {

class Actor;

class ActorContext {
 public:
  ActorContext() = default;
  ActorContext(const ActorContext &) = delete;
  ActorContext &operator=(const ActorContext &) = delete;
  ActorContext(ActorContext &&) = delete;
  ActorContext &operator=(ActorContext &&) = delete;
  virtual ~ActorContext() = default;

  virtual int32 get_id() const {
    return 0;
  }

  const char *tag_ = nullptr;
  std::weak_ptr<ActorContext> this_ptr_;
};

class ActorInfo
    : private ListNode
    , HeapNode {
 public:
  enum class Deleter : uint8 { Destroy, None };

  ActorInfo() = default;
  ~ActorInfo() = default;

  ActorInfo(ActorInfo &&) = delete;
  ActorInfo &operator=(ActorInfo &&) = delete;

  ActorInfo(const ActorInfo &) = delete;
  ActorInfo &operator=(const ActorInfo &) = delete;

  void init(int32 sched_id, Slice name, ObjectPool<ActorInfo>::OwnerPtr &&this_ptr, Actor *actor_ptr, Deleter deleter,
            bool is_lite);
  void on_actor_moved(Actor *actor_new_ptr);

  template <class ActorT>
  ActorOwn<ActorT> transfer_ownership_to_scheduler(unique_ptr<ActorT> actor);
  void clear();
  void destroy_actor();

  bool empty() const;
  void start_migrate(int32 to_sched_id);
  bool is_migrating() const;
  int32 migrate_dest() const;
  std::pair<int32, bool> migrate_dest_flag_atomic() const;

  void finish_migrate();

  ActorId<> actor_id();
  template <class SelfT>
  ActorId<SelfT> actor_id(SelfT *self);
  Actor *get_actor_unsafe();
  const Actor *get_actor_unsafe() const;

  std::shared_ptr<ActorContext> set_context(std::shared_ptr<ActorContext> context);
  ActorContext *get_context();
  const ActorContext *get_context() const;
  CSlice get_name() const;

  HeapNode *get_heap_node();
  const HeapNode *get_heap_node() const;
  static ActorInfo *from_heap_node(HeapNode *node);

  ListNode *get_list_node();
  const ListNode *get_list_node() const;
  static ActorInfo *from_list_node(ListNode *node);

  void start_run();
  bool is_running() const;
  void finish_run();

  vector<Event> mailbox_;

  bool is_lite() const;

  void set_wait_generation(uint32 wait_generation);
  bool must_wait(uint32 wait_generation) const;
  void always_wait_for_mailbox();

 private:
  Deleter deleter_ = Deleter::None;
  bool is_lite_ = false;
  bool is_running_ = false;
  bool always_wait_for_mailbox_{false};
  uint32 wait_generation_{0};

  std::atomic<int32> sched_id_{0};
  Actor *actor_ = nullptr;

#ifdef TD_DEBUG
  string name_;
#endif
  std::shared_ptr<ActorContext> context_;
};

StringBuilder &operator<<(StringBuilder &sb, const ActorInfo &info);

}  // namespace td
