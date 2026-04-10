// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/Proxy.h"

namespace td {

struct StealthConnectionCountPlan final {
  bool capped_for_stealth_tls_proxy{false};
  int32 main_session_count{1};
  int32 upload_session_count{1};
  int32 download_session_count{1};
  int32 download_small_session_count{1};

  int32 total_session_count() const noexcept;
  int32 total_tcp_connection_count() const noexcept;
  bool merges_download_small_into_download() const noexcept;
};

StealthConnectionCountPlan make_connection_count_plan(const Proxy &proxy, int32 main_session_count, int32 raw_dc_id,
                                                      bool is_premium) noexcept;

NetQuery::Type resolve_connection_count_routed_query_type(NetQuery::Type type,
                                                          const StealthConnectionCountPlan &plan) noexcept;

}  // namespace td