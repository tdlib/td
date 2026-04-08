// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace stealth {

namespace detail {

constexpr uint16 kCurrentSingleLanePqGroupId = 0x11EC;
constexpr uint16 kCurrentSingleLanePqKeyShareLength = 0x04C0;
constexpr uint16 kCorrectEchEncKeyLen = 32;
constexpr uint16 kCurrentSingleLaneAlpsType = 0x44CD;

struct TlsHelloBuildOptions final {
  size_t padding_extension_payload_length{0};
  int ech_payload_length{144};
  uint16 pq_group_id{kCurrentSingleLanePqGroupId};
  uint16 ech_enc_key_length{kCorrectEchEncKeyLen};
  uint16 alps_extension_type{kCurrentSingleLaneAlpsType};
};

string build_default_tls_client_hello_with_options(string domain, Slice secret, int32 unix_time,
                                                   const NetworkRouteHints &route_hints, IRng &rng,
                                                   const TlsHelloBuildOptions &options);

}  // namespace detail

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time);
string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints);
string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints, IRng &rng);
string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints);
string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints, IRng &rng);
string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                          EchMode ech_mode, IRng &rng);
string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                          EchMode ech_mode);

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
