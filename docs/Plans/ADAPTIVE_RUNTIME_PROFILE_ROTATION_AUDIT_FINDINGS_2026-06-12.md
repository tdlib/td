<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Masking Subsystem Audit Findings (2026-06-12)

Logic + architecture + security audit of the masking/stealth subsystem after
implementing
[ADAPTIVE_RUNTIME_PROFILE_ROTATION_PLAN_2026-06-12.md](./ADAPTIVE_RUNTIME_PROFILE_ROTATION_PLAN_2026-06-12.md)
on branch `feat/adaptive-runtime-profile-rotation`.

**Scope audited:** `td/mtproto/stealth/TlsHelloProfileRegistry.{h,cpp}`,
`StealthRuntimeParams.{h,cpp}`, `StealthParamsLoader.cpp`, `StealthConfig.{h,cpp}`,
`td/mtproto/BrowserProfile.{h,cpp}`, `td/mtproto/TlsInit.{h,cpp}`,
`td/mtproto/IStreamTransport.cpp`, `StealthTransportDecorator.*`,
`TlsHelloBuilder.*`.

**Verdict:** No critical findings. The conservative failure attribution,
fail-closed quarantine-exhaustion path, ECH/profile state separation, weight-carve
arithmetic, and positional-array alignment are all correct (see *Checked and
confirmed correct*). Findings cluster around the un-wired cross-actor handoff and
two missing compile-time/validation backstops.

> **Verification note:** this host cannot build/run the suite (three pre-existing
> macOS portability blockers unrelated to this change: zlib `>= 1.3.2` gate,
> `tl-parser.c` glibc `htole32/64`, `logging.cpp` `std::atomic<std::shared_ptr<>>`).
> Findings are from static analysis; line numbers are from the working tree at
> audit time and may drift. Run the full `ctest` on the project's Linux CI.

## Status summary

| ID | Severity | Title | Status |
|----|----------|-------|--------|
| H1 | High | Split-profile wire incoherence when rotation is enabled | **Deferred** (gate before enabling rotation) |
| M1 | Medium | No compile-time guard tying `profile_index` to the positional arrays | **Resolved** |
| M2 | Medium | Small `ios14` policy values silently zero the verified iOS lanes | **Deferred / documented** |
| M3 | Medium | Active adversary can force fail-closed-on-baseline via forged post-hello rejection | **Deferred** (tuning/hardening) |
| M4 | Medium | Failure-driven rotation order is a deterministic, enumerable fingerprint | **Deferred** (hardening) |
| L1 | Low | Quarantine key has no `\|` delimiter sanitization (bounded aliasing) | **Accepted** (fail-safe) |
| L2 | Low | Sub-threshold quarantine count is reset by TTL expiry on read | **Accepted** (intentional, fail-safe) |
| L3 | Low | `note_runtime_profile_success` runs even when rotation is disabled | **Accepted** (intentional) |

---

## HIGH

### H1 — Split-profile wire incoherence: enabling rotation makes the ClientHello and the transport-shaping profile diverge per connection

**Status:** Deferred — hard gate before `profile_rotation.enabled = true` ships.
**Files:** `td/mtproto/TlsInit.cpp` (`send_hello`, adaptive pick) vs
`td/mtproto/stealth/StealthConfig.cpp` (`from_secret`, legacy pick).

`TlsInit::send_hello()` selects the **wire** profile with the adaptive selector:

```cpp
auto selection = stealth::pick_runtime_profile_adaptive(username_, hello_unix_time_, platform, decision.ech_mode);
```

while `StealthConfig::from_secret` independently selects with the **non-adaptive
legacy** selector for transport record shaping:

```cpp
return from_secret(secret, rng, unix_time, platform,
                   pick_runtime_profile(secret.get_domain(), unix_time, platform));
```

These two selections are never reconciled (the single-selection handoff is the
plan's deferred item).

**Why it matters.** With rotation OFF (default) they agree only by determinism, and
even then a cross-time-bucket divergence is possible. With rotation ON they will
routinely disagree: the adaptive selector rotates the ClientHello to a
non-quarantined profile while the legacy selector (which knows nothing about
quarantine) still shapes records for the quarantined baseline. The emitted
ClientHello then advertises one profile's `record_size_limit` / cipher fingerprint
while the decorator shapes records to a different profile's limit — an internally
inconsistent flow no real browser produces, and a strong DPI distinguisher. This is
exactly the wire incoherence the plan's "Select Once Per Connection Attempt" /
"One Shared Handshake Snapshot" sections were meant to prevent. The
`apply_profile_record_size_limit` floor-clamp partially mitigates the record-size
axis but not cipher order, ALPS, ECH-permittedness, or other per-profile traits.

**Recommended fix.** Thread the `RuntimeProfileSelectionDecision` chosen in
`TlsInit::send_hello()` into the `StealthConfig` for the same connection via the
existing explicit-profile overload
`StealthConfig::from_secret(..., BrowserProfile profile)` /
`make_transport_stealth_config(secret, rng, profile)`, instead of letting
`create_transport` re-pick. This requires plumbing the selected profile across the
`ConnectionCreator` → `RawConnection` → `create_transport` actor boundary (e.g. via
`TransportType` or the connection result). Add an integration test
("one connection attempt uses one profile in both config and emitted hello") and a
guard so rotation cannot be enabled while the two selection sites are independent.

---

## MEDIUM

### M1 — No compile-time guard tied `profile_index` to the three positional arrays

**Status:** **Resolved** in `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`.
**Files:** `TlsHelloProfileRegistry.cpp` — `ALL_PROFILES`, `PROFILE_SPECS`,
`PROFILE_FIXTURES`, `profile_index`.

`profile_index(p) == static_cast<size_t>(p)` indexes `PROFILE_SPECS` and
`PROFILE_FIXTURES` purely by position; the binding to the `BrowserProfile` enum was
enforced by nothing. A mis-insertion or reorder would silently return the wrong
wire spec or trust metadata for a profile — e.g. an advisory lane inheriting a
`Verified` / `release_gating = true` fixture and defeating the release gate, or a
profile emitting the wrong cipher / `record_size_limit` on the wire. No test or
compiler caught this. `PROFILE_FIXTURES` is the worst case: its entries carry no
`BrowserProfile id` field, so the binding is invisible and entirely positional.

**Fix applied:**

```cpp
constexpr size_t kRegisteredProfileCount = sizeof(ALL_PROFILES) / sizeof(ALL_PROFILES[0]);

constexpr bool profile_registry_arrays_are_index_aligned() {
  if (sizeof(PROFILE_SPECS) / sizeof(PROFILE_SPECS[0]) != kRegisteredProfileCount) return false;
  if (sizeof(PROFILE_FIXTURES) / sizeof(PROFILE_FIXTURES[0]) != kRegisteredProfileCount) return false;
  for (size_t i = 0; i < kRegisteredProfileCount; i++) {
    if (static_cast<size_t>(ALL_PROFILES[i]) != i) return false;
    if (PROFILE_SPECS[i].id != ALL_PROFILES[i]) return false;
  }
  return true;
}
static_assert(profile_registry_arrays_are_index_aligned(), "...must stay index-aligned...");
```

plus a bounds `CHECK(static_cast<size_t>(profile) < kRegisteredProfileCount)` in
`profile_index`. The `static_assert` passes today (arrays are aligned, including the
new `AppleIosTls` at index 16); any future position break now fails to compile.

**Remaining follow-up:** `PROFILE_FIXTURES` is only count-checked (no id field).
Give `ProfileFixtureMetadata` an explicit `BrowserProfile id` and extend the
alignment check to verify `PROFILE_FIXTURES[i].id == ALL_PROFILES[i]`.

### M2 — Small `policy.mobile.ios14` values silently zero the verified Apple iOS TLS lane

**Status:** Deferred / documented (default config is correct).
**Files:** `td/mtproto/stealth/StealthRuntimeParams.cpp`
(`effective_profile_weights_for_platform`), mirrored in
`StealthParamsLoader.cpp` (`flatten_profile_selection`); validators in
`StealthRuntimeParams.cpp`.

```cpp
auto ios_chromium_weight = static_cast<uint8>(policy.mobile.ios14 / kIosChromiumShareDivisor);   // /7
auto apple_ios_tls_weight = static_cast<uint8>(policy.mobile.ios14 / kAppleIosTlsShareDivisor);  // /7
```

The only mobile-policy constraint is `ios14 + android11_okhttp_advisory == 100`.
With `ios14 ∈ {1..6}`, integer division yields `apple_ios_tls = 0` **and**
`chrome147_ios_chromium = 0`; the whole iOS share collapses back onto the advisory
`IOS14` lane. The carve still conserves the total (no drift/underflow — verified),
but provides no nonzero floor for the verified lanes.
`validate_release_mode_profile_gating` catches `apple_ios_tls = 0` only when
`release_mode_profile_gating = true` AND `transport_confidence == Unknown`; in every
other iOS config a tiny `ios14` ships iOS with only the advisory lane — the exact
release-grade boundary this lane was added to close.

**Why not fixed now.** Enforcing "verified lane must be > 0" conflicts with
legitimate configs/tests that intentionally zero `apple_ios_tls`; flooring the carve
risks the `uint8` underflow that is currently correctly avoided. The default
(`ios14 = 70` → `apple_ios_tls = 10`) is correct.

**Recommended fix.** Either require `policy.mobile.ios14 == 0 || policy.mobile.ios14
>= 14` in `validate_runtime_profile_selection_policy` (so both `/7` carves are ≥ 2
when present), or guarantee a conservation-safe floor of 1 for the verified lanes
when the share can afford it, or add a targeted validator asserting the verified
Apple iOS TLS lane has nonzero effective weight on iOS whenever it is the designated
release lane.

### M3 — Active adversary can pin a destination to fail-closed-on-baseline via forged post-hello transport rejection

**Status:** Deferred (tuning/hardening; bounded and fail-safe).
**Files:** `td/mtproto/TlsInit.cpp` (`on_proxy_setup_error` →
`record_profile_failure_once(TransportRejectionAfterHello)`),
`TlsHelloProfileRegistry.cpp` (`pick_runtime_profile_adaptive` all-blocked path).

`on_proxy_setup_error` unconditionally attributes a post-hello transport/setup error
to the profile as a quarantine-eligible wire-shape rejection. A network adversary
who can inject a TCP RST or induce a timeout *after* the ClientHello (an
in-the-clear, pre-ECH observable point) can drive `recent_failures` past
`failure_threshold` (default 2) for each profile in turn. After rotating through the
allowed set, `available.empty()` is true and the selector fail-closes onto the
quarantined baseline.

**Why it is bounded.** Quarantine is in-memory, per `quarantine_ttl_seconds`
(≥ 30 s), and fail-closed never widens the allowed set, downgrades, or weakens ECH —
worst case the client keeps trying its legitimate baseline. The real cost is that
the adversary can force `avoided_quarantined_profile = true` rotations on demand,
feeding M4.

**Recommended fix.** Require corroboration before quarantining on
`TransportRejectionAfterHello` (e.g. ≥ 1 prior successful handshake to that
destination, or a higher threshold for transport-reject vs malformed-response), so a
destination with no established baseline cannot be cheaply pushed into rotation by
injected RSTs. At minimum, document that `failure_threshold` should be tuned up for
hostile networks.

### M4 — Failure-driven rotation order is deterministic, so the rotation sequence is itself an enumerable fingerprint

**Status:** Deferred (hardening; inherent tension with anti-churn stickiness).
**Files:** `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` (`weighted_pick` over the
surviving pool, `stable_selection_hash`).

When rotation triggers, the replacement is chosen by the same deterministic
`stable_selection_hash % total_weight` over the surviving pool. For a fixed
destination, time bucket, platform, and per-install salt, the rotation sequence as
profiles are quarantined one-by-one is fully deterministic. Combined with M3 (force
quarantines at will), a DPI can map a target's per-install rotation graph. The salt
de-correlates across installs but is constant within one, so a single target's
rotation order is a stable secondary fingerprint that survives the profile change.

**Recommended fix (optional for Phase 1).** Acknowledge in the threat model; if
hardening is wanted, mix a quarantine-epoch counter into the rotation hash so the
replacement order is not a pure function of the static key.

---

## LOW

### L1 — Quarantine key has no `|` delimiter sanitization (bounded aliasing only)

**Status:** Accepted (fail-safe).
**File:** `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` (`profile_quarantine_key`).

```cpp
string key = normalized_runtime_destination_key(destination);
key += '|'; key += to_string(static_cast<int>(profile)); key += '|'; key += hello_uses_ech ? '1' : '0';
```

The normalized destination is not stripped of `|`. Because `profile` (0–16) and the
ech bit are code-appended suffixes, a cross-destination alias would require an
attacker-controlled SNI whose tail exactly matches another destination's
`|<profile>|<bit>`; `|` is also not a legal DNS hostname character. The worst case is
two attacker-chosen destinations sharing one fail-safe quarantine entry — it cannot
weaken any victim's masking. The same unsanitized `|` exists in
`stable_selection_hash` material, where a crc32 collision is only a
non-security-relevant selection nudge.

**Optional fix.** Length-prefix the destination token or reject/encode `|` in
`normalized_runtime_destination_key`.

### L2 — Sub-threshold quarantine count is reset by TTL expiry on read

**Status:** Accepted (intentional, fail-safe).
**File:** `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
(`is_profile_variant_quarantined_locked` read-erase vs `note_runtime_profile_failure`
setting `quarantined_until` on every failure).

A variant that accrues one failure (threshold 2) and is then read after its TTL loses
the accumulated count. This is intentional aging and fail-safe (it makes quarantine
*harder*, not easier). The field name `recent_failures` accurately reflects "failures
within one sliding TTL," not "failures ever." No fix required; flagged so it is not
mistaken for a bug.

### L3 — `note_runtime_profile_success` runs even when rotation is disabled

**Status:** Accepted (intentional).
**File:** `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
(`note_runtime_profile_success` vs the `enabled` early-return in
`note_runtime_profile_failure`).

`note_runtime_profile_failure` early-returns when `profile_rotation.enabled` is
false; `note_runtime_profile_success` does not, so it takes the global mutex on every
successful handshake even with rotation disabled. With rotation off the map is empty
so the erase is a harmless no-op. The unconditional clear is deliberately defensive
(a previously-rotated entry still clears if rotation is toggled off). Trivial; no fix
required.

---

## Checked and confirmed correct

Examined and verified sound, so they are not re-litigated:

- **Weight-carve arithmetic** (`StealthRuntimeParams.cpp`): the remainder lanes
  (`ios14`, `android11_okhttp_advisory`) are computed by subtraction, so the
  share-sum is always exactly conserved — no truncation drift, no `uint8`
  underflow/wrap — for all tested policy values.
- **`flatten_profile_selection` ≡ `effective_profile_weights_for_platform`**: every
  computed assignment is byte-for-byte equivalent; only statement ordering differs.
- **`PROFILE_SPECS` / `ALL_PROFILES` / enum order**: 17 entries, `AppleIosTls` last
  at index 16, `profile_index` lands correctly (now backed by the M1 `static_assert`).
- **`response_hash_mismatch` / `wrong_regime` are NOT quarantine-eligible**;
  out-of-enum failure signals fail closed; `record_profile_failure_once` refuses to
  burn its once-guard on ineligible signals (false-positive resistance intact).
- **ECH circuit-breaker vs profile-quarantine**: independent, separately once-guarded
  (`hello_failure_recorded_` vs `hello_profile_failure_recorded_`); no shared state,
  no double-count; ECH is destination-scoped, quarantine is
  (destination, profile, ech)-scoped.
- **All-quarantined fail-closed**: increments `profile_quarantine_all_blocked_total`
  exactly once and returns the baseline; never widens the allowed set.
- **ECH unknown/RU fail-closed not weakened by rotation**: rotation re-picks only
  within the already confidence/release-filtered pool; `hello_uses_ech` is recomputed
  per candidate; the ECH circuit breaker is untouched.
- **Fail-open / unmasked-leak (`IStreamTransport.cpp`)**: `FailClosedStealthTransport`
  refuses to emit unmasked obfuscated-MTProto bytes; both the shaping-unavailable and
  stealth-disabled `emulate_tls` paths fail closed. No legacy-bytes-to-wire path found.
