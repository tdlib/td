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

enum class VaultKeyRole : uint8 { MainMtproto = 0x01, TestMtproto = 0x02, SimpleConfig = 0x03 };

class RsaKeyVault {
 public:
  static Result<RSA> unseal(VaultKeyRole role);
  static Status verify_integrity();
  static int64 expected_fingerprint(VaultKeyRole role);

 private:
  static Result<string> reassemble_and_decrypt(VaultKeyRole role);
};

}  // namespace mtproto
}  // namespace td