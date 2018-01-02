//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/binlog/BinlogEvent.h"

#include "td/utils/tl_parsers.h"

namespace td {
int32 VERBOSITY_NAME(binlog) = VERBOSITY_NAME(DEBUG) + 8;

Status BinlogEvent::init(BufferSlice &&raw_event, bool check_crc) {
  TlParser parser(raw_event.as_slice());
  size_ = parser.fetch_int();
  CHECK(size_ == raw_event.size());
  id_ = parser.fetch_long();
  type_ = parser.fetch_int();
  flags_ = parser.fetch_int();
  extra_ = parser.fetch_long();
  CHECK(size_ >= MIN_EVENT_SIZE);
  auto slice_data = parser.fetch_string_raw<Slice>(size_ - MIN_EVENT_SIZE);
  data_ = MutableSlice(const_cast<char *>(slice_data.begin()), slice_data.size());
  crc32_ = static_cast<uint32>(parser.fetch_int());
  if (check_crc) {
    CHECK(size_ >= EVENT_TAIL_SIZE);
    auto calculated_crc = crc32(raw_event.as_slice().truncate(size_ - EVENT_TAIL_SIZE));
    if (calculated_crc != crc32_) {
      return Status::Error(PSLICE() << "crc mismatch " << tag("actual", format::as_hex(calculated_crc))
                                    << tag("expected", format::as_hex(crc32_)));
    }
  }
  raw_event_ = std::move(raw_event);
  return Status::OK();
}

}  // namespace td
