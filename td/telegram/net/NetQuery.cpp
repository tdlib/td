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

}  // namespace td
