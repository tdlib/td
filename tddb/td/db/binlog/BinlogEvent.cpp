//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/binlog/BinlogEvent.h"

#include "td/utils/crypto.h"
#include "td/utils/misc.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace td {

int32 VERBOSITY_NAME(binlog) = VERBOSITY_NAME(DEBUG) + 8;

Status BinlogEvent::init(BufferSlice &&raw_event, bool check_crc) {
  TlParser parser(raw_event.as_slice());
  size_ = parser.fetch_int();
  LOG_CHECK(size_ == raw_event.size()) << size_ << " " << raw_event.size() << debug_info_;
  id_ = parser.fetch_long();
  type_ = parser.fetch_int();
  flags_ = parser.fetch_int();
  extra_ = parser.fetch_long();
  CHECK(size_ >= MIN_SIZE);
  auto slice_data = parser.fetch_string_raw<Slice>(size_ - MIN_SIZE);
  data_ = MutableSlice(const_cast<char *>(slice_data.begin()), slice_data.size());
  crc32_ = static_cast<uint32>(parser.fetch_int());
  if (check_crc) {
    CHECK(size_ >= TAIL_SIZE);
    auto calculated_crc = crc32(raw_event.as_slice().truncate(size_ - TAIL_SIZE));
    if (calculated_crc != crc32_) {
      return Status::Error(PSLICE() << "crc mismatch " << tag("actual", format::as_hex(calculated_crc))
                                    << tag("expected", format::as_hex(crc32_)) << public_to_string());
    }
  }
  raw_event_ = std::move(raw_event);
  return Status::OK();
}

Status BinlogEvent::validate() const {
  BinlogEvent event;
  if (raw_event_.size() < 4) {
    return Status::Error("Too small event");
  }
  uint32 size = TlParser(raw_event_.as_slice().truncate(4)).fetch_int();
  if (size_ != size) {
    return Status::Error(PSLICE() << "Size of event changed: " << tag("was", size_) << tag("now", size));
  }
  return event.init(raw_event_.clone(), true);
}

BufferSlice BinlogEvent::create_raw(uint64 id, int32 type, int32 flags, const Storer &storer) {
  auto raw_event = BufferSlice{storer.size() + MIN_SIZE};

  TlStorerUnsafe tl_storer(raw_event.as_slice().ubegin());
  tl_storer.store_int(narrow_cast<int32>(raw_event.size()));
  tl_storer.store_long(id);
  tl_storer.store_int(type);
  tl_storer.store_int(flags);
  tl_storer.store_long(0);

  CHECK(tl_storer.get_buf() == raw_event.as_slice().ubegin() + HEADER_SIZE);
  tl_storer.store_storer(storer);

  CHECK(tl_storer.get_buf() == raw_event.as_slice().uend() - TAIL_SIZE);
  tl_storer.store_int(::td::crc32(raw_event.as_slice().truncate(raw_event.size() - TAIL_SIZE)));

  return raw_event;
}

}  // namespace td
