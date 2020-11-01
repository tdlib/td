//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQueryCreator.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Storer.h"

namespace td {

NetQueryPtr NetQueryCreator::create(const telegram_api::Function &function, DcId dc_id, NetQuery::Type type) {
  return create(UniqueId::next(), function, dc_id, type, NetQuery::AuthFlag::On);
}

NetQueryPtr NetQueryCreator::create(uint64 id, const telegram_api::Function &function, DcId dc_id, NetQuery::Type type,
                                    NetQuery::AuthFlag auth_flag) {
  LOG(DEBUG) << "Create query " << to_string(function);
  auto storer = DefaultStorer<telegram_api::Function>(function);
  BufferSlice slice(storer.size());
  auto real_size = storer.store(slice.as_slice().ubegin());
  LOG_CHECK(real_size == slice.size()) << real_size << " " << slice.size() << " "
                                       << format::as_hex_dump<4>(Slice(slice.as_slice()));

  int32 tl_constructor = function.get_id();

  size_t MIN_GZIPPED_SIZE = 128;
  auto gzip_flag = slice.size() < MIN_GZIPPED_SIZE ? NetQuery::GzipFlag::Off : NetQuery::GzipFlag::On;
  if (slice.size() >= 16384) {
    // test compression ratio for the middle part
    // if it is less than 0.9, then try to compress the whole request
    size_t TESTED_SIZE = 1024;
    BufferSlice compressed_part = gzencode(slice.as_slice().substr((slice.size() - TESTED_SIZE) / 2, TESTED_SIZE), 0.9);
    if (compressed_part.empty()) {
      gzip_flag = NetQuery::GzipFlag::Off;
    }
  }
  if (gzip_flag == NetQuery::GzipFlag::On) {
    BufferSlice compressed = gzencode(slice.as_slice(), 0.9);
    if (compressed.empty()) {
      gzip_flag = NetQuery::GzipFlag::Off;
    } else {
      slice = std::move(compressed);
    }
  }

  double total_timeout_limit = 60;
  if (!G()->close_flag()) {
    auto td = G()->td();
    if (!td.empty()) {
      auto auth_manager = td.get_actor_unsafe()->auth_manager_.get();
      if (auth_manager != nullptr && auth_manager->is_bot()) {
        total_timeout_limit = 8;
      }
      if ((auth_manager == nullptr || !auth_manager->was_authorized()) && auth_flag == NetQuery::AuthFlag::On &&
          tl_constructor != telegram_api::auth_exportAuthorization::ID &&
          tl_constructor != telegram_api::auth_bindTempAuthKey::ID) {
        LOG(ERROR) << "Send query before authorization: " << to_string(function);
      }
    }
  }
  auto query = object_pool_.create(NetQuery::State::Query, id, std::move(slice), BufferSlice(), dc_id, type, auth_flag,
                                   gzip_flag, tl_constructor, total_timeout_limit, net_query_stats_.get());
  query->set_cancellation_token(query.generation());
  return query;
}

}  // namespace td
