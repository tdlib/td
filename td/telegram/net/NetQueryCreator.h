//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/UniqueId.h"

#include "td/utils/buffer.h"
#include "td/utils/ObjectPool.h"
#include "td/utils/Storer.h"

namespace td {

class NetQueryCreator {
 public:
  using Ptr = NetQueryPtr;
  using Ref = NetQueryRef;

  NetQueryCreator() {
    object_pool_.set_check_empty(true);
  }

  void stop_check() {
    object_pool_.set_check_empty(false);
  }

  Ptr create_result(BufferSlice &&buffer, DcId dc_id = DcId::main(),
                    NetQuery::AuthFlag auth_flag = NetQuery::AuthFlag::On,
                    NetQuery::GzipFlag gzip_flag = NetQuery::GzipFlag::Off) {
    return create_result(0, std::move(buffer), dc_id, auth_flag, gzip_flag);
  }
  Ptr create_result(uint64 id, BufferSlice &&buffer, DcId dc_id = DcId::main(),
                    NetQuery::AuthFlag auth_flag = NetQuery::AuthFlag::On,
                    NetQuery::GzipFlag gzip_flag = NetQuery::GzipFlag::Off) {
    return object_pool_.create(NetQuery::State::OK, id, BufferSlice(), std::move(buffer), dc_id, NetQuery::Type::Common,
                               auth_flag, gzip_flag, 0);
  }

  Ptr create(const Storer &storer, DcId dc_id = DcId::main(), NetQuery::Type type = NetQuery::Type::Common,
             NetQuery::AuthFlag auth_flag = NetQuery::AuthFlag::On,
             NetQuery::GzipFlag gzip_flag = NetQuery::GzipFlag::On, double total_timeout_limit = 60) {
    return create(UniqueId::next(), storer, dc_id, type, auth_flag, gzip_flag, total_timeout_limit);
  }
  Ptr create(uint64 id, const Storer &storer, DcId dc_id = DcId::main(), NetQuery::Type type = NetQuery::Type::Common,
             NetQuery::AuthFlag auth_flag = NetQuery::AuthFlag::On,
             NetQuery::GzipFlag gzip_flag = NetQuery::GzipFlag::On, double total_timeout_limit = 60);

 private:
  ObjectPool<NetQuery> object_pool_;
};

}  // namespace td
