//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/DbKey.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/StorerBase.h"

namespace td {

class BinlogInterface {
 public:
  BinlogInterface() = default;
  BinlogInterface(const BinlogInterface &) = delete;
  BinlogInterface &operator=(const BinlogInterface &) = delete;
  BinlogInterface(BinlogInterface &&) = delete;
  BinlogInterface &operator=(BinlogInterface &&) = delete;
  virtual ~BinlogInterface() = default;

  void close(Promise<> promise = {}) {
    close_impl(std::move(promise));
  }
  void close_and_destroy(Promise<> promise = {}) {
    close_and_destroy_impl(std::move(promise));
  }
  void add_raw_event(BinlogDebugInfo info, uint64 id, BufferSlice &&raw_event, Promise<> promise = Promise<>()) {
    add_raw_event_impl(id, std::move(raw_event), std::move(promise), info);
  }
  void add_raw_event(uint64 id, BufferSlice &&raw_event, Promise<> promise = Promise<>()) {
    add_raw_event_impl(id, std::move(raw_event), std::move(promise), {});
  }
  void lazy_sync(Promise<> promise = Promise<>()) {
    add_raw_event_impl(next_id(), BufferSlice(), std::move(promise), {});
  }

  uint64 add(int32 type, const Storer &storer, Promise<> promise = Promise<>()) {
    auto logevent_id = next_id();
    add_raw_event_impl(logevent_id, BinlogEvent::create_raw(logevent_id, type, 0, storer), std::move(promise), {});
    return logevent_id;
  }

  uint64 rewrite(uint64 logevent_id, int32 type, const Storer &storer, Promise<> promise = Promise<>()) {
    auto seq_no = next_id();
    add_raw_event_impl(seq_no, BinlogEvent::create_raw(logevent_id, type, BinlogEvent::Flags::Rewrite, storer),
                       std::move(promise), {});
    return seq_no;
  }

  uint64 erase(uint64 logevent_id, Promise<> promise = Promise<>()) {
    auto seq_no = next_id();
    add_raw_event_impl(seq_no,
                       BinlogEvent::create_raw(logevent_id, BinlogEvent::ServiceTypes::Empty,
                                               BinlogEvent::Flags::Rewrite, EmptyStorer()),
                       std::move(promise), {});
    return seq_no;
  }

  virtual void force_sync(Promise<> promise) = 0;
  virtual void force_flush() = 0;
  virtual void change_key(DbKey db_key, Promise<> promise = Promise<>()) = 0;

  virtual uint64 next_id() = 0;
  virtual uint64 next_id(int32 shift) = 0;

 protected:
  virtual void close_impl(Promise<> promise) = 0;
  virtual void close_and_destroy_impl(Promise<> promise) = 0;
  virtual void add_raw_event_impl(uint64 id, BufferSlice &&raw_event, Promise<> promise, BinlogDebugInfo info) = 0;
};

}  // namespace td
