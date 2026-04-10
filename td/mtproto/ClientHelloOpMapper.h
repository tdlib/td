// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/ClientHelloExecutor.h"

namespace td {
namespace mtproto {

class ClientHelloOpMapper {
 public:
  static vector<ClientHelloOp> map(const BrowserProfileSpec &profile, const ExecutorConfig &config);
};

}  // namespace mtproto
}  // namespace td
