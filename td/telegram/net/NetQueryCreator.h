//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/UniqueId.h"

#include "td/utils/buffer.h"
#include "td/utils/ObjectPool.h"

#include <memory>

namespace td {

namespace telegram_api {
class Function;
}  // namespace telegram_api

class NetQueryCreator {
 public:
  explicit NetQueryCreator(std::shared_ptr<NetQueryStats> net_query_stats = {}) {
    net_query_stats_ = std::move(net_query_stats);
    object_pool_.set_check_empty(true);
  }

  void stop_check() {
    object_pool_.set_check_empty(false);
  }

  NetQueryPtr create_update(BufferSlice &&buffer) {
    return object_pool_.create(NetQuery::State::OK, 0, BufferSlice(), std::move(buffer), DcId::main(),
                               NetQuery::Type::Common, NetQuery::AuthFlag::On, NetQuery::GzipFlag::Off, 0, 0,
                               net_query_stats_.get());
  }

  NetQueryPtr create(const telegram_api::Function &function, DcId dc_id = DcId::main(),
                     NetQuery::Type type = NetQuery::Type::Common);

  NetQueryPtr create_unauth(const telegram_api::Function &function, DcId dc_id = DcId::main()) {
    return create(UniqueId::next(), function, dc_id, NetQuery::Type::Common, NetQuery::AuthFlag::Off);
  }

  NetQueryPtr create(uint64 id, const telegram_api::Function &function, DcId dc_id, NetQuery::Type type,
                     NetQuery::AuthFlag auth_flag);

 private:
  std::shared_ptr<NetQueryStats> net_query_stats_;
  ObjectPool<NetQuery> object_pool_;
};

}  // namespace td
