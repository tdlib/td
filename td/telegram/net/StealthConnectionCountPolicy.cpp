// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/StealthConnectionCountPolicy.h"

#include <algorithm>
#include <limits>

namespace td {
namespace {

constexpr int32 kMinMainSessionCount = 1;
constexpr int32 kMaxSessionCount = 100;

bool is_stealth_tls_mtproto_proxy(const Proxy &proxy) {
  return proxy.use_mtproto_proxy() && proxy.secret().emulate_tls();
}

int32 clamp_main_session_count(int32 session_count) {
  return std::clamp(session_count, kMinMainSessionCount, kMaxSessionCount);
}

int32 saturating_int64_to_int32(int64 value) {
  if (value <= 0) {
    return 0;
  }
  auto max_value = static_cast<int64>(std::numeric_limits<int32>::max());
  if (value >= max_value) {
    return std::numeric_limits<int32>::max();
  }
  return static_cast<int32>(value);
}

int64 safe_session_count_component(int32 count) {
  return std::max<int64>(count, 0);
}

}  // namespace

int32 StealthConnectionCountPlan::total_session_count() const noexcept {
  auto total = safe_session_count_component(main_session_count) + safe_session_count_component(upload_session_count) +
               safe_session_count_component(download_session_count) +
               safe_session_count_component(download_small_session_count);
  return saturating_int64_to_int32(total);
}

int32 StealthConnectionCountPlan::total_tcp_connection_count() const noexcept {
  return saturating_int64_to_int32(static_cast<int64>(total_session_count()) * 2);
}

bool StealthConnectionCountPlan::merges_download_small_into_download() const noexcept {
  return download_small_session_count == 0;
}

StealthConnectionCountPlan make_connection_count_plan(const Proxy &proxy, int32 main_session_count, int32 raw_dc_id,
                                                      bool is_premium) noexcept {
  StealthConnectionCountPlan plan;
  if (is_stealth_tls_mtproto_proxy(proxy)) {
    plan.capped_for_stealth_tls_proxy = true;
    plan.main_session_count = 1;
    plan.upload_session_count = 1;
    plan.download_session_count = 1;
    plan.download_small_session_count = 0;
    return plan;
  }

  plan.main_session_count = clamp_main_session_count(main_session_count);
  plan.upload_session_count = ((raw_dc_id != 2 && raw_dc_id != 4) || is_premium) ? 8 : 4;
  plan.download_session_count = is_premium ? 8 : 2;
  plan.download_small_session_count = is_premium ? 8 : 2;
  return plan;
}

NetQuery::Type resolve_connection_count_routed_query_type(NetQuery::Type type,
                                                          const StealthConnectionCountPlan &plan) noexcept {
  if (type == NetQuery::Type::DownloadSmall && plan.merges_download_small_into_download()) {
    return NetQuery::Type::Download;
  }
  return type;
}

}  // namespace td