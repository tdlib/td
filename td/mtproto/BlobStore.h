//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/RSA.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

enum class BlobRole : uint8 { Primary = 0x01, Secondary = 0x02, Auxiliary = 0x03 };

class BlobStore {
 public:
  static Result<RSA> load(BlobRole role);
  static Status verify_bundle();
  static int64 expected_slot(BlobRole role);

 private:
  static Result<string> decode_blob(BlobRole role);
};

}  // namespace mtproto
}  // namespace td