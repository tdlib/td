//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {
namespace mtproto {
namespace stealth {

namespace detail {

constexpr uint16 kCurrentSingleLanePqGroupId = 0x11EC;
constexpr uint16 kCurrentSingleLanePqKeyShareLength = 0x04C0;
constexpr uint16 kCorrectEchEncKeyLen = 32;

struct TlsHelloBuildOptions final {
  size_t padding_extension_payload_length{0};
  int ech_payload_length{144};
  uint16 pq_group_id{kCurrentSingleLanePqGroupId};
  uint16 ech_enc_key_length{kCorrectEchEncKeyLen};
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

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
