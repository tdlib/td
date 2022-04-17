//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQuery.h"

#include "td/telegram/ChainId.h"
#include "td/telegram/Global.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/as.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"

#include <algorithm>

namespace td {

int VERBOSITY_NAME(net_query) = VERBOSITY_NAME(INFO);

int64 NetQuery::get_my_id() {
  return G()->get_my_id();
}

void NetQuery::debug(string state, bool may_be_lost) {
  may_be_lost_ = may_be_lost;
  VLOG(net_query) << *this << " " << tag("state", state);
  {
    auto guard = lock();
    auto &data = get_data_unsafe();
    data.state_ = std::move(state);
    data.state_timestamp_ = Time::now();
    data.state_change_count_++;
  }
}

NetQuery::NetQuery(State state, uint64 id, BufferSlice &&query, BufferSlice &&answer, DcId dc_id, Type type,
                   AuthFlag auth_flag, GzipFlag gzip_flag, int32 tl_constructor, int32 total_timeout_limit,
                   NetQueryStats *stats, vector<ChainId> chain_ids)
    : state_(state)
    , type_(type)
    , auth_flag_(auth_flag)
    , gzip_flag_(gzip_flag)
    , dc_id_(dc_id)
    , status_()
    , id_(id)
    , query_(std::move(query))
    , answer_(std::move(answer))
    , tl_constructor_(tl_constructor)
    , total_timeout_limit_(total_timeout_limit) {
  CHECK(id_ != 0);
  chain_ids_ = transform(chain_ids, [](ChainId chain_id) { return chain_id.get() == 0 ? 1 : chain_id.get(); });
  td::unique(chain_ids_);

  auto &data = get_data_unsafe();
  data.my_id_ = get_my_id();
  data.start_timestamp_ = data.state_timestamp_ = Time::now();
  LOG(INFO) << *this;
  if (stats) {
    nq_counter_ = stats->register_query(this);
  }
}

void NetQuery::on_net_write(size_t size) {
  if (file_type_ == -1) {
    return;
  }
  G()->get_net_stats_file_callbacks().at(file_type_)->on_write(size);
}

void NetQuery::on_net_read(size_t size) {
  if (file_type_ == -1) {
    return;
  }
  G()->get_net_stats_file_callbacks().at(file_type_)->on_read(size);
}

int32 NetQuery::tl_magic(const BufferSlice &buffer_slice) {
  auto slice = buffer_slice.as_slice();
  if (slice.size() < 4) {
    return 0;
  }
  return as<int32>(slice.begin());
}

void NetQuery::set_error(Status status, string source) {
  if (status.code() == Error::Resend || status.code() == Error::Canceled || status.code() == Error::ResendInvokeAfter) {
    return set_error_impl(Status::Error(200, PSLICE() << status), std::move(source));
  }

  if (begins_with(status.message(), "INPUT_METHOD_INVALID")) {
    LOG(ERROR) << "Receive INPUT_METHOD_INVALID for query " << format::as_hex_dump<4>(Slice(query_.as_slice()));
  }
  if (status.message() == "BOT_METHOD_INVALID") {
    auto id = tl_constructor();
    if (id != telegram_api::help_getNearestDc::ID && id != telegram_api::help_getAppConfig::ID) {
      LOG(ERROR) << "Receive BOT_METHOD_INVALID for query " << format::as_hex(id);
    }
  }
  if (status.message() == "MSG_WAIT_FAILED" && status.code() != 400) {
    status = Status::Error(400, "MSG_WAIT_FAILED");
  }
  set_error_impl(std::move(status), std::move(source));
}

}  // namespace td
