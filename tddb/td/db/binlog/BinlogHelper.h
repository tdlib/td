//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"

#include "td/utils/common.h"

namespace td {

template <class BinlogT, class StorerT>
uint64 binlog_add(const BinlogT &binlog_ptr, int32 type, const StorerT &storer, Promise<> promise = Promise<>()) {
  auto logevent_id = binlog_ptr->next_id();
  binlog_ptr->add_raw_event(logevent_id, BinlogEvent::create_raw(logevent_id, type, 0, storer), std::move(promise));
  return logevent_id;
}

template <class BinlogT, class StorerT>
uint64 binlog_rewrite(const BinlogT &binlog_ptr, uint64 logevent_id, int32 type, const StorerT &storer,
                      Promise<> promise = Promise<>()) {
  auto seq_no = binlog_ptr->next_id();
  binlog_ptr->add_raw_event(seq_no, BinlogEvent::create_raw(logevent_id, type, BinlogEvent::Flags::Rewrite, storer),
                            std::move(promise));
  return seq_no;
}

#define binlog_erase(...) binlog_erase_impl({__FILE__, __LINE__}, __VA_ARGS__)

template <class BinlogT>
uint64 binlog_erase_impl(BinlogDebugInfo info, const BinlogT &binlog_ptr, uint64 logevent_id,
                         Promise<> promise = Promise<>()) {
  auto seq_no = binlog_ptr->next_id();
  binlog_ptr->add_raw_event(info, seq_no,
                            BinlogEvent::create_raw(logevent_id, BinlogEvent::ServiceTypes::Empty,
                                                    BinlogEvent::Flags::Rewrite, EmptyStorer()),
                            std::move(promise));
  return seq_no;
}

}  // namespace td
