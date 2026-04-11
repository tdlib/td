// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/ClientHelloOp.h"

#include "td/utils/Status.h"

namespace td {
namespace mtproto {

struct ExecutorConfig {
  size_t grease_value_count{7};
  bool has_ech{false};
  int ech_payload_length{144};
  int ech_enc_key_length{32};
  uint16 alps_type{0};
  // Per-build entropy added to every `padding_to_target` op so the
  // total wire length varies across builds even when ECH is disabled.
  // Real Chrome shows length variability in this range from internal
  // session-state changes; without this, the unpadded ECH-disabled
  // wire collapses to a single fixed length and becomes a fingerprint.
  int padding_target_entropy{0};
};

class ClientHelloExecutor {
 public:
  static Result<string> execute(const vector<ClientHelloOp> &ops, Slice domain, Slice secret, int32 unix_time,
                                const ExecutorConfig &config);
};

}  // namespace mtproto
}  // namespace td
