//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQuery.h"

#include "td/telegram/Global.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/as.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"

namespace td {

TsList<NetQueryDebug> net_query_list_;

int32 NetQuery::get_my_id() {
  return G()->get_my_id();
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
  if (status.code() == Error::Resend || status.code() == Error::Cancelled ||
      status.code() == Error::ResendInvokeAfter) {
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

void dump_pending_network_queries() {
  auto n = NetQueryCounter::get_count();
  LOG(WARNING) << tag("pending net queries", n);

  decltype(n) i = 0;
  bool was_gap = false;
  auto guard = net_query_list_.lock();
  for (auto end = net_query_list_.end(), cur = net_query_list_.begin(); cur != end; cur = cur->get_next(), i++) {
    if (i < 20 || i + 20 > n || i % (n / 20 + 1) == 0) {
      if (was_gap) {
        LOG(WARNING) << "...";
        was_gap = false;
      }
      const NetQueryDebug &debug = cur->get_data_unsafe();
      LOG(WARNING) << tag("id", debug.my_id_) << *static_cast<const NetQuery *>(cur)
                   << /*tag("total_flood", format::as_time(debug.total_timeout_)) <<*/ " "
                   << tag("since start", format::as_time(Time::now_cached() - debug.start_timestamp_))
                   << tag("state", debug.debug_str_)
                   << tag("since state", format::as_time(Time::now_cached() - debug.debug_timestamp_))
                   << tag("resend_cnt", debug.debug_resend_cnt_) << tag("fail_cnt", debug.debug_send_failed_cnt_)
                   << tag("ack", debug.debug_ack_) << tag("unknown", debug.debug_unknown_);
    } else {
      was_gap = true;
    }
  }
}

}  // namespace td
