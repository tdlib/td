//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/utils.h"

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/UniqueId.h"

#include "td/utils/buffer.h"
#include "td/utils/ObjectPool.h"
#include "td/utils/StorerBase.h"

namespace td {

class NetQueryCreator {
 public:
  NetQueryCreator() {
    object_pool_.set_check_empty(true);
  }

  void stop_check() {
    object_pool_.set_check_empty(false);
  }

  NetQueryPtr create_update(BufferSlice &&buffer) {
    return object_pool_.create(NetQuery::State::OK, 0, BufferSlice(), std::move(buffer), DcId::main(),
                               NetQuery::Type::Common, NetQuery::AuthFlag::On, NetQuery::GzipFlag::Off, 0);
  }

  NetQueryPtr create(const Storer &storer) {
    return create(UniqueId::next(), storer, DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::On);
  }

  NetQueryPtr create(const Storer &storer, DcId dc_id, NetQuery::Type type) {
    return create(UniqueId::next(), storer, dc_id, type, NetQuery::AuthFlag::On);
  }

  NetQueryPtr create_guest_dc(const Storer &storer, DcId dc_id) {
    return create(UniqueId::next(), storer, dc_id, NetQuery::Type::Common, NetQuery::AuthFlag::On);
  }

  NetQueryPtr create_unauth(const Storer &storer, DcId dc_id = DcId::main()) {
    return create(UniqueId::next(), storer, dc_id, NetQuery::Type::Common, NetQuery::AuthFlag::Off);
  }

  NetQueryPtr create(uint64 id, const Storer &storer, DcId dc_id, NetQuery::Type type, NetQuery::AuthFlag auth_flag);

 private:
  ObjectPool<NetQuery> object_pool_;
};

}  // namespace td
