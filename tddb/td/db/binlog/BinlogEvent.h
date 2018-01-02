//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Storer.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_storers.h"

namespace td {
struct EmptyStorerImpl {
  EmptyStorerImpl() {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
  }
};

inline auto EmptyStorer() {
  static const EmptyStorerImpl impl;
  return create_default_storer(impl);
}

static constexpr size_t MAX_EVENT_SIZE = 1 << 24;
static constexpr size_t EVENT_HEADER_SIZE = 4 + 8 + 4 + 4 + 8;
static constexpr size_t EVENT_TAIL_SIZE = 4;
static constexpr size_t MIN_EVENT_SIZE = EVENT_HEADER_SIZE + EVENT_TAIL_SIZE;

extern int32 VERBOSITY_NAME(binlog);

// TODO: smaller BinlogEvent
struct BinlogEvent {
  int64 offset_;

  uint32 size_;
  uint64 id_;
  int32 type_;  // type can be merged with flags
  int32 flags_;
  uint64 extra_;
  MutableSlice data_;
  uint32 crc32_;

  BufferSlice raw_event_;

  enum ServiceTypes { Header = -1, Empty = -2, AesCtrEncryption = -3, NoEncryption = -4 };
  enum Flags { Rewrite = 1, Partial = 2 };

  void clear() {
    raw_event_ = BufferSlice();
  }
  bool empty() const {
    return raw_event_.empty();
  }
  BinlogEvent clone() const {
    BinlogEvent result;
    result.init(raw_event_.clone()).ensure();
    return result;
  }

  BufferSlice data_as_buffer_slice() const {
    return raw_event_.from_slice(data_);
  }

  BinlogEvent() = default;
  explicit BinlogEvent(BufferSlice &&raw_event) {
    init(std::move(raw_event), false).ensure();
  }
  Status init(BufferSlice &&raw_event, bool check_crc = true) TD_WARN_UNUSED_RESULT;

  static BufferSlice create_raw(uint64 id, int32 type, int32 flags, const Storer &storer);
};

inline StringBuilder &operator<<(StringBuilder &sb, const BinlogEvent &event) {
  return sb << "LogEvent[" << tag("id", format::as_hex(event.id_)) << tag("type", event.type_)
            << tag("flags", event.flags_) << tag("data", format::as_hex_dump<4>(event.data_)) << "]";
}

// Implementation
inline BufferSlice BinlogEvent::create_raw(uint64 id, int32 type, int32 flags, const Storer &storer) {
  auto raw_event = BufferSlice{storer.size() + MIN_EVENT_SIZE};

  TlStorerUnsafe tl_storer(raw_event.as_slice().begin());
  tl_storer.store_int(narrow_cast<int32>(raw_event.size()));
  tl_storer.store_long(id);
  tl_storer.store_int(type);
  tl_storer.store_int(flags);
  tl_storer.store_long(0);

  CHECK(tl_storer.get_buf() == raw_event.as_slice().begin() + EVENT_HEADER_SIZE);
  tl_storer.store_storer(storer);

  CHECK(tl_storer.get_buf() == raw_event.as_slice().end() - EVENT_TAIL_SIZE);
  tl_storer.store_int(::td::crc32(raw_event.as_slice().truncate(raw_event.size() - EVENT_TAIL_SIZE)));

  return raw_event;
}
}  // namespace td
