//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Storer.h"
#include "td/utils/StorerBase.h"
#include "td/utils/StringBuilder.h"

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

extern int32 VERBOSITY_NAME(binlog);

struct BinlogDebugInfo {
  BinlogDebugInfo() = default;
  BinlogDebugInfo(const char *file, int line) : file(file), line(line) {
  }
  const char *file{""};
  int line{0};
};

inline StringBuilder &operator<<(StringBuilder &sb, const BinlogDebugInfo &info) {
  if (info.line == 0) {
    return sb;
  }
  return sb << "[" << info.file << ":" << info.line << "]";
}

struct BinlogEvent {
  static constexpr size_t MAX_SIZE = 1 << 24;
  static constexpr size_t HEADER_SIZE = 4 + 8 + 4 + 4 + 8;
  static constexpr size_t TAIL_SIZE = 4;
  static constexpr size_t MIN_SIZE = HEADER_SIZE + TAIL_SIZE;

  int64 offset_;

  uint32 size_;
  uint64 id_;
  int32 type_;  // type can be merged with flags
  int32 flags_;
  uint64 extra_;
  MutableSlice data_;
  uint32 crc32_;

  BufferSlice raw_event_;

  BinlogDebugInfo debug_info_;

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
    result.debug_info_ = BinlogDebugInfo{__FILE__, __LINE__};
    result.init(raw_event_.clone()).ensure();
    return result;
  }

  BufferSlice data_as_buffer_slice() const {
    return raw_event_.from_slice(data_);
  }

  BinlogEvent() = default;
  //explicit BinlogEvent(BufferSlice &&raw_event) {
  //init(std::move(raw_event), false).ensure();
  //}
  BinlogEvent(BufferSlice &&raw_event, BinlogDebugInfo info) {
    debug_info_ = info;
    init(std::move(raw_event), false).ensure();
  }

  Status init(BufferSlice &&raw_event, bool check_crc = true) TD_WARN_UNUSED_RESULT;

  static BufferSlice create_raw(uint64 id, int32 type, int32 flags, const Storer &storer);

  std::string public_to_string() const {
    return PSTRING() << "LogEvent[" << tag("id", format::as_hex(id_)) << tag("type", type_) << tag("flags", flags_)
                     << tag("data", data_.size()) << "]" << debug_info_;
  }

  Status validate() const;
};

inline StringBuilder &operator<<(StringBuilder &sb, const BinlogEvent &event) {
  return sb << "LogEvent[" << tag("id", format::as_hex(event.id_)) << tag("type", event.type_)
            << tag("flags", event.flags_) << tag("data", format::as_hex_dump<4>(event.data_)) << "]"
            << event.debug_info_;
}

}  // namespace td
