//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/BlobStore.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"

namespace td {
class ReferenceTable {
 public:
  static int64 slot_value(mtproto::BlobRole role) {
    switch (role) {
      case mtproto::BlobRole::Primary:
        return static_cast<int64>(0xd09d1d85de64fd85ULL);
      case mtproto::BlobRole::Secondary:
        return static_cast<int64>(0xb25898df208d2603ULL);
      case mtproto::BlobRole::Auxiliary:
        return static_cast<int64>(0x6f3a701151477715ULL);
      default:
        return 0;
    }
  }

  static size_t host_count() {
    return 6;
  }

  static string host_name(size_t index) {
    switch (index) {
      case 0:
        return "tcdnb.azureedge.net";
      case 1:
        return "dns.google";
      case 2:
        return "mozilla.cloudflare-dns.com";
      case 3:
        return "firebaseremoteconfig.googleapis.com";
      case 4:
        return "reserve-5a846.firebaseio.com";
      case 5:
        return "firestore.googleapis.com";
      default:
        return string();
    }
  }

  static bool contains_host(Slice host) {
    auto normalized_host = to_lower(host);
    for (size_t index = 0; index < host_count(); index++) {
      if (normalized_host == host_name(index)) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace td