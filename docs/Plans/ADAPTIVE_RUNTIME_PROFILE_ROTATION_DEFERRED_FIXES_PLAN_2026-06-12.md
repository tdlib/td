<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Deferred-Findings Implementation Plan (2026-06-12)

Closes the deferred items from
[ADAPTIVE_RUNTIME_PROFILE_ROTATION_AUDIT_FINDINGS_2026-06-12.md](./ADAPTIVE_RUNTIME_PROFILE_ROTATION_AUDIT_FINDINGS_2026-06-12.md)
so that failure-driven profile rotation can be safely enabled (fingerprints
should rotate). Branch: `feat/adaptive-runtime-profile-rotation` (PR #22).

**Goal:** wire the single-selection cross-actor handoff (H1) so one connection
attempt emits one coherent wire variant, close the iOS weight-carve gap (M2), and
record the deliberate decisions on the two threat-model items (M3, M4) whose naive
"fix" would *hurt* the rotation goal.

> Verification: this dev host cannot build/run (zlib / tl-parser / libc++ macOS
> gaps). All changes are `clang -fsyntax-only`-verified; the connection-path
> changes (H1) need a Linux-CI integration run before `profile_rotation.enabled`
> is flipped on.

---

## H1 — Single-selection cross-actor handoff (IMPLEMENT)

**Problem.** `TlsInit::send_hello` picks the wire profile with
`pick_runtime_profile_adaptive`, while `create_transport` →
`make_transport_stealth_config` → `StealthConfig::from_secret` re-picks
independently with the legacy `pick_runtime_profile`. With rotation enabled the
ClientHello and the transport-shaping config diverge → incoherent wire fingerprint.

**Flow (verified).** In `ConnectionCreator`, the same `extra.transport_type` value
reaches both consumers:
- the connection promise copies `transport_type = extra.transport_type`
  (`ConnectionCreator.cpp:1625`) → forwarded to `client_create_raw_connection`
  (`:1628`) → `RawConnection::create` → `RawConnectionDefault` ctor →
  `create_transport` (`RawConnection.cpp:134` / `IStreamTransport.cpp:137`);
- `prepare_connection` receives `extra.transport_type` by `const&`
  (`:1637`) and builds `TlsInit` (`:1443`).
The explicit-profile receivers already exist:
`StealthConfig::from_secret(..., BrowserProfile)` and
`make_transport_stealth_config(secret, rng, BrowserProfile)`.

**Implementation.**
1. Add `td::optional<mtproto::BrowserProfile> selected_profile{};` to
   `TransportType` (`td/mtproto/TransportType.h`). Non-breaking — every
   construction site uses the 3-arg ctor.
2. In `ConnectionCreator`, compute the profile **once** per attempt, *before* the
   connection promise copies `transport_type`, and stamp
   `extra.transport_type.selected_profile`. Compute with the inputs already in
   scope (domain, `route_hints_from_country_code(...)`, server-adjusted
   `unix_time`, `default_runtime_platform_hints()`,
   `get_runtime_ech_decision(...).ech_mode`, `pick_runtime_profile_adaptive(...)`),
   only for `secret.emulate_tls()`.
3. `prepare_connection` passes `transport_type.selected_profile` into `TlsInit`
   (new optional ctor arg). `TlsInit::send_hello` uses the pre-selected profile
   instead of calling `pick_runtime_profile_adaptive` again; it still computes the
   ECH decision and `hello_uses_ech` at send time and keeps the per-wire-variant
   accounting. When no profile is supplied (tests / non-proxy) it self-selects as
   before.
4. `create_transport` uses
   `make_transport_stealth_config(secret, rng, type.selected_profile.value())`
   when the field is set; otherwise the legacy no-profile overload.

**Result.** One `pick_runtime_profile_adaptive` per attempt, threaded to both the
ClientHello and the shaping config — no split profile state.

## M2 — Tiny `policy.mobile.ios14` zeros the verified iOS lanes (IMPLEMENT)

**Problem.** The carve `apple_ios_tls = ios14 / 7` (and `chrome147_ios_chromium =
ios14 / 7`) yields 0 for `ios14 ∈ [1,6]`, silently collapsing the iOS share onto
the advisory `IOS14` lane and re-opening the gap the verified lane closed — caught
today only when release gating + Unknown confidence both hold.

**Fix.** In `validate_runtime_profile_selection_policy`
(`StealthRuntimeParams.cpp`) require `policy.mobile.ios14 == 0 ||
policy.mobile.ios14 >= 7`, so whenever there is an iOS share at all both verified
carves are ≥ 1. This is a policy-path constraint only; explicit flat
`profile_weights` configs (which set `apple_ios_tls` directly) are unaffected, and
the default (`ios14 = 70`) is well clear of the floor.

## M3 — Adversary-forced rotation via post-hello rejection (DO NOT "fix"; tune)

**Decision: keep as-is; expose tuning, do not add a prior-success gate.**

The audit's suggested mitigation — require ≥ 1 prior successful handshake before a
`TransportRejectionAfterHello` quarantines — is **counter-productive for a DPI
tool**: the most common real block is a RST/drop right after the ClientHello on the
*first* attempt, so a "prior success required" gate would forbid rotating away from
a fingerprint that is blocked from day one — exactly when rotation is most needed.
The existing bound is already fail-safe: in-memory, `failure_threshold ≥ 2`,
`quarantine_ttl_seconds ≥ 30`, never widens the allowed set, never downgrades,
never weakens ECH. The correct operator control is the existing
`profile_rotation.failure_threshold` knob (raise it on hostile networks).
**Action:** document this in the rotation policy; no code change.

## M4 — Deterministic rotation order is enumerable (DO NOT "fix" naively)

**Decision: keep the rotation deterministic for Phase 1.**

Mixing a quarantine-epoch into the rotation hash to defeat enumeration directly
contradicts two stronger properties: (a) anti-churn stickiness (a non-deterministic
rotation order is itself a churn fingerprint), and (b) the H1 coherence requirement
(config and hello must pick the *same* rotated profile — a time-varying epoch would
re-introduce divergence). The per-install salt already de-correlates rotation order
*across* installations, which is the population-level defence that matters.
**Action:** document the residual single-install determinism in the threat model;
revisit only with field traces, ideally by deriving the rotation order from a
per-attempt nonce carried through the H1 handoff (so coherence is preserved).

---

## Test / verification plan

- Coherence: `pick_runtime_profile_adaptive` profile == the profile
  `StealthConfig::from_secret(secret, rng, t, platform, profile)` embeds (explicit
  path), and the explicit overload performs no second pick.
- M2: `validate_runtime_stealth_params` rejects `policy.mobile.ios14 ∈ [1,6]` and
  accepts `0` and `7..100`; default params still validate.
- `clang -fsyntax-only` on every touched translation unit.
- **Linux CI:** full `ctest`, plus a connection-path integration check that one
  attempt uses one profile in both the emitted hello and the shaping config, before
  enabling `profile_rotation.enabled`.
