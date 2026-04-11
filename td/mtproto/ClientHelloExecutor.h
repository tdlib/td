// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/ClientHelloOp.h"
#include "td/mtproto/stealth/Interfaces.h"

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
  // Wire codepoint advertised in the X25519MlKem768 hybrid key_share
  // entry. Real Chrome 131+ uses the IANA-final 0x11EC (X25519MLKEM768);
  // older Chrome 120 PQ snapshots used the legacy 0x6399
  // (X25519Kyber768Draft00) with the same hybrid layout. The same
  // codepoint is also surfaced in the `supported_groups` extension.
  // Tests that exercise the legacy lane construct a builder with
  // `pq_group_id_override = 0x6399`; production callers leave it at
  // the default 0x11EC.
  uint16 pq_group_id_override{0x11EC};

  // When > 0, the executor's `Op::PaddingToTarget` handler emits a
  // padding extension with EXACTLY this many body bytes, ignoring the
  // op's normal "extend wire to target" calculation. Used by tests that
  // need a deterministic padding extension shape (e.g.
  // `TlsContextEntropy::ExplicitPaddingExtensionLengthIsHonored` sets
  // it to 23 and asserts the wire produces a `0x0015` extension with a
  // 23-byte body). Production callers leave this at 0 and rely on the
  // target+entropy logic.
  size_t padding_extension_payload_length_override{0};
};

class ClientHelloExecutor {
 public:
  // The `rng` parameter MUST drive every byte of randomness consumed
  // during ClientHello generation: GREASE pool initialization, ECH and
  // padding random bodies, X25519 / ML-KEM rejection sampling, and the
  // `Permutation` shuffle. Production callers pass a `SecureRng` that
  // wraps the global crypto PRNG; tests pass a deterministic `MockRng`
  // so that wire-image equality assertions are reproducible across
  // builds. Without an injected `IRng`, the executor would silently
  // fall back to the global PRNG and the determinism of any seeded
  // test would collapse.
  static Result<string> execute(const vector<ClientHelloOp> &ops, Slice domain, Slice secret, int32 unix_time,
                                const ExecutorConfig &config, stealth::IRng &rng);
};

}  // namespace mtproto
}  // namespace td
