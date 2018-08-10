//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQueryCreator.h"

#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"

namespace td {

NetQueryCreator::Ptr NetQueryCreator::create(uint64 id, const Storer &storer, DcId dc_id, NetQuery::Type type,
                                             NetQuery::AuthFlag auth_flag, NetQuery::GzipFlag gzip_flag,
                                             double total_timeout_limit) {
  BufferSlice slice(storer.size());
  auto real_size = storer.store(slice.as_slice().ubegin());
  CHECK(real_size == slice.size()) << real_size << " " << slice.size() << " "
                                   << format::as_hex_dump<4>(Slice(slice.as_slice()));

  // TODO: magic constant
  if (slice.size() < (1 << 8)) {
    gzip_flag = NetQuery::GzipFlag::Off;
  }
  int32 tl_constructor = NetQuery::tl_magic(slice);
  if (gzip_flag == NetQuery::GzipFlag::On) {
    // TODO: try to compress files?
    BufferSlice compressed = gzencode(slice.as_slice());
    if (compressed.empty()) {
      gzip_flag = NetQuery::GzipFlag::Off;
    } else {
      slice = std::move(compressed);
    }
  }

  auto query = object_pool_.create(NetQuery::State::Query, id, std::move(slice), BufferSlice(), dc_id, type, auth_flag,
                                   gzip_flag, tl_constructor);
  query->set_cancellation_token(query.generation());
  query->total_timeout_limit = total_timeout_limit;
  return query;
}

}  // namespace td
