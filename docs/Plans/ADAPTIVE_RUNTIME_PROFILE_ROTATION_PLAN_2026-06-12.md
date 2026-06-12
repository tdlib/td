<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Adaptive Runtime Profile Rotation Plan (2026-06-12)

> **For agentic workers:** REQUIRED SUB-SKILL: use
> `superpowers:test-driven-development` before implementation and use
> `superpowers:executing-plans` or `superpowers:subagent-driven-development`
> to execute this plan task-by-task. Steps below are a security-sensitive
> implementation handoff, not a loose design note.

**Goal:** Add bounded, failure-driven runtime profile rotation, remove the
current single-connection double-selection seam, and close the remaining
iOS/default-Unknown mobile release-grade gap without weakening existing release,
platform, transport-confidence, and ECH fail-closed gates.

**Architecture:** Keep profile quarantine in memory for the first slice, key it
by the actual emitted wire variant, and force one profile decision to drive both
transport shaping config and the TLS ClientHello. In the same plan, add one
reviewed browser-capture-backed Apple iOS TLS runtime lane that is both
`TlsOnly` and `release_gating=true`, because rotation by itself cannot close the
current iOS/default-Unknown release-grade boundary. Persistence is intentionally
a separate follow-up after selector behavior is proven.

**Tech Stack:** C++23 TDLib stealth/TLS runtime, CMake `run_all_tests`,
SocratiCode for code navigation, repo-local adversarial tests under
`test/stealth/`.

**Required repo protocol before edits:**

1. Re-read `AGENTS.md` and the relevant `.github/instructions/` files:
   - `.github/instructions/architecture.instructions.md`
   - `.github/instructions/TDD_approach.instructions.md`
   - `.github/instructions/Security_Requirements.instructions.md`
   - `.github/instructions/logging_subsystem.instructions.md`
   - `.github/instructions/c++_rules.instructions.md`
2. Use SocratiCode first:
   - `codebase_status`
   - `codebase_search` for the touched runtime paths
   - `codebase_flow` / `codebase_symbol` for `pick_runtime_profile`
3. Work in this order:
   - assess the current code;
   - assess project principles and security constraints;
   - write failing tests;
   - implement the minimal code;
   - run focused verification;
   - run full verification.

---

## Audit Result

This document replaces the earlier draft after a code-grounded audit against:

- `AGENTS.md`
- `.github/instructions/architecture.instructions.md`
- `.github/instructions/TDD_approach.instructions.md`
- `.github/instructions/Security_Requirements.instructions.md`
- `.github/instructions/logging_subsystem.instructions.md`

Evidence leans toward the original direction being correct, but the previous
draft was too broad for a safe first implementation. The main problems were:

1. it introduced persistent quarantine state immediately, which adds parser,
   migration, poisoning, and restart-semantics complexity before the selector
   logic itself is proven;
2. it keyed quarantine by `route_class` / platform metadata instead of the
   actual wire-affecting variant;
3. it treated too many TLS hello failures as profile-block evidence, including
   classes already used elsewhere to mean "wrong secret" or "wrong regime";
4. it allowed the `StealthConfig` vs `TlsInit` double-selection seam to remain
   as an optional follow-up, which is not acceptable once rotation state can
   diverge the two paths.

Confidence: ~85% that the corrected plan below is a materially safer and more
implementable first slice.

## Source-of-Truth Code Paths

SocratiCode status used for this audit:

- project: `/home/david_osipov/tdlib-obf`
- collection: `codebase_3f9428eea04a`
- status: `green`
- watcher: `active`

Live files confirmed during audit:

1. `td/mtproto/stealth/TlsHelloProfileRegistry.{h,cpp}`
   - owns `pick_runtime_profile(...)`
   - owns `allowed_profiles_for_platform(...)`
   - owns release-gating and transport-confidence filtering
   - owns runtime destination normalization
   - owns the existing ECH route-failure cache and per-install salt

2. `td/mtproto/TlsInit.{h,cpp}`
   - `send_hello()` computes ECH decision, then independently picks the runtime
     profile, stores only the profile name string, and builds the ClientHello
   - `wait_hello_response()` currently records only ECH circuit-breaker state
   - `on_proxy_setup_error()` currently records only ECH circuit-breaker state

3. `td/mtproto/stealth/StealthConfig.{h,cpp}`
   - `StealthConfig::from_secret(...)` independently calls
     `pick_runtime_profile(...)`
   - the selected profile is embedded into transport decoration config

4. `td/mtproto/stealth/TlsHelloBuilder.cpp`
   - `build_runtime_tls_client_hello(...)` also performs its own
     `pick_runtime_profile(...)` call

5. `td/telegram/net/ConnectionRetryPolicy.{h,cpp}`
   - already classifies proxy-backed failure reasons
   - explicitly treats `response_hash_mismatch` as a secret / TLS-init contract
     debugging signal, not as fingerprint-block evidence

## Current Runtime Facts

The current code already enforces several boundaries that rotation must not
weaken:

1. platform-local allowed sets exist and are enforced in
   `allowed_profiles_for_platform(...)`;
2. `TransportConfidence::Unknown` only allows `TlsOnly` claim levels;
3. `release_mode_profile_gating=true` suppresses non-release profiles;
4. `stable_selection_hash(...)` already includes destination, time bucket,
   platform hints, and optional per-install salt;
5. ECH circuit-breaker state is already stateful and global, but it is
   destination-scoped, not profile-scoped;
6. `StealthConfig::from_secret(...)` and `TlsInit::send_hello()` still select
   profiles independently, and the temporal-divergence tests already prove this
   seam exists.
7. iOS still lacks a profile that is simultaneously:
   - allowed at `TransportConfidence::Unknown`;
   - `release_gating=true`; and
   - backed by reviewed non-advisory evidence.

## Design Goal

Add failure-driven profile rotation that lets one connection attempt avoid a
recently rejected fingerprint for the same destination, without introducing:

- per-connect random churn;
- release-gating bypass;
- transport-confidence bypass;
- cross-platform widening;
- new persistent poisoning surface in the first slice.

This is not "shuffle every connect". It is "stay stable unless there is bounded
evidence that one specific wire shape is failing and another allowed wire shape
may work".

## Corrected Scope

### Phase 1: Required in this plan

1. In-memory-only quarantine state with bounded TTL.
2. Rotation keyed by the actual emitted wire variant:
   - normalized destination
   - selected `BrowserProfile`
   - whether this hello actually used ECH on the wire
3. Conservative failure attribution.
4. Single-selection plumbing per connection attempt.
5. Test-first implementation with dedicated contract / adversarial /
   integration / stress files.
6. One reviewed Apple iOS TLS runtime lane that closes the current
   iOS/default-Unknown release-grade gap without re-labeling advisory evidence.

### Phase 2: Explicitly out of scope for this plan

1. Persistent quarantine across restart.
2. Store serialization / migration / corruption recovery for profile quarantine.
3. Per-connect random profile shuffling.
4. Any unrelated new browser profiles. Exactly one Apple iOS TLS runtime lane is
   now in scope because it is required to close the live iOS/default boundary.
5. Any weakening of `unknown` / RU ECH fail-closed route behavior.

## Why the Previous Draft Needed Correction

### 1. Persistence first is the wrong first move

The repo already has a mature persistent route-failure cache, but it also has a
large adversarial test surface around malformed payloads, TTL reconciliation,
case aliases, migration, and lookup budgeting. Repeating that entire
persistence/security problem for profile rotation before proving the selector
policy is justified by KISS, YAGNI, or SRP.

For Phase 1, evidence does not justify:

- new `stealth_profile_cb#` serialization;
- new parser fail-closed rules;
- new restart semantics;
- new store poisoning tests;
- new permanent on-disk state for speculative profile blocks.

The first slice should be in-memory only.

### 2. Quarantine must be keyed by wire reality, not by route label

The earlier draft keyed quarantine by destination + route class + platform +
profile. That is not the correct fingerprint boundary.

The actual runtime wire shape depends on:

- `BrowserProfile`
- final ECH usage on the wire

It does not currently depend on raw `route_class` in any broader way. Two cases
make the earlier key incorrect:

1. `KnownNonRu` with ECH disabled by the circuit breaker and `KnownRu` with ECH
   disabled by route policy can converge to the same wire variant.
2. `RuntimeEchDecision` may be `Rfc9180Outer`, but a profile with
   `allows_ech=false` still emits a non-ECH hello.

So Phase 1 must key quarantine by:

```text
normalized_destination + BrowserProfile + hello_uses_ech
```

not by `route_class`.

### 3. Failure attribution must be conservative

The previous draft proposed quarantining on response hash mismatch, malformed
response, and setup rejection while waiting for the hello response.

That is too broad.

The current codebase already classifies some failures differently:

- `response_hash_mismatch` points at proxy secret / contract mismatch
- `wrong_regime` points at protocol mismatch

Rotating profiles on those classes would create false-positive quarantine and
"fingerprint roulette" against a misconfigured proxy where no profile can
possibly help.

Phase 1 must therefore:

1. default `failure_threshold` above `1`;
2. quarantine only on failure classes plausibly consistent with fingerprint
   blocking or wire-shape rejection;
3. explicitly exclude `wrong_regime` and `response_hash_mismatch` from profile
   quarantine.

### 4. Single-selection plumbing is mandatory, not optional

The existing TOCTOU seam between `StealthConfig::from_secret(...)` and
`TlsInit::send_hello()` is already proven by
`test_stealth_config_tls_init_profile_temporal_divergence.cpp`.

Once profile quarantine exists, leaving that seam in place is materially worse:

- the decorator path may keep baseline profile `A`;
- `TlsInit` may rotate to profile `B`;
- one connection attempt then carries split profile state.

This is not acceptable as a "temporary maybe". One connection attempt must have
one selected profile.

### 5. Rotation alone does not close the remaining iOS/default mobile gap

The remaining mobile issue is not a selector bug. It is the absence of an iOS
lane whose metadata simultaneously satisfies:

1. `TransportConfidence::Unknown` -> `TlsOnly`
2. `release_mode_profile_gating=true` -> `release_gating=true`
3. reviewed, non-advisory provenance

The current iOS choices do not satisfy that conjunction:

- `Chrome147_IOSChromium` has reviewed browser-capture provenance, but it is
  `CrossLayerStrong` and not release-gated;
- `IOS14` is `TlsOnly`, but it is still advisory and not release-gated.

So this plan must not pretend rotation solves that by itself. Proper closure
requires one additional runtime lane representing the reviewed `apple_ios_tls`
family with metadata intentionally chosen to match the actual policy boundary:

- browser-capture-backed / verified;
- `TlsOnly`;
- `release_gating=true`.

Do **not** "close" this by:

1. marking `IOS14` release-gated;
2. changing `Chrome147_IOSChromium` to `TlsOnly` without evidence;
3. weakening `TransportConfidence::Unknown`;
4. bypassing release-mode filtering for mobile.

## Corrected Implementation Plan

### 1. Close the Current iOS/Default Release-Grade Gap

Files:

- `td/mtproto/BrowserProfile.h`
- `td/mtproto/BrowserProfile.cpp`
- `td/mtproto/stealth/TlsHelloProfileRegistry.h`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
- `td/mtproto/stealth/StealthRuntimeParams.h`
- `td/mtproto/stealth/StealthRuntimeParams.cpp`
- `test/stealth/test_tls_profile_registry.cpp`
- `test/stealth/test_tls_mobile_release_grade_lane.cpp`
- `test/stealth/test_tls_runtime_profile_policy_fail_closed.cpp`
- `test/stealth/test_tls_runtime_release_profile_gating_contract.cpp`
- `test/stealth/test_tls_multi_dump_ios_apple_tls_baseline.cpp`

Add one dedicated iOS runtime profile for the reviewed Apple TLS family.

Required shape:

1. Add a concrete runtime/browser profile such as
   `BrowserProfile::AppleIosTls`.
2. Its wire image must be anchored to the already reviewed `apple_ios_tls`
   family under `test/analysis/fixtures/clienthello/ios/` and the corresponding
   family-lane baselines, not to advisory utls snapshots.
3. Its `ProfileFixtureMetadata` must be:
   - `source_kind = BrowserCapture`
   - `trust_tier = Verified`
   - `has_independent_network_provenance = true`
   - `release_gating = true`
   - `transport_claim_level = TlsOnly`
4. `IOS14` stays advisory and non-release-gated.
5. `Chrome147_IOSChromium` keeps its current stronger cross-layer semantics
   unless a separate evidence review proves otherwise.

Effective-weight requirement:

1. Add a dedicated effective weight slot for the new Apple iOS TLS lane.
2. Keep the mobile loader backward-compatible, but flatten the existing iOS
   mobile policy so the validated defaults guarantee:
   - the new verified Apple iOS TLS lane has non-zero effective weight;
   - `Chrome147_IOSChromium` may keep a non-zero established-confidence share;
   - `IOS14` is no longer the only `Unknown`-confidence iOS lane.
3. Release mode plus `TransportConfidence::Unknown` on iOS must now have at
   least one `TlsOnly` + `release_gating` candidate, and that candidate must be
   the new verified Apple iOS TLS lane rather than advisory fallback.

### 2. Add a Minimal Runtime Rotation Policy

Files:

- `td/mtproto/stealth/StealthRuntimeParams.h`
- `td/mtproto/stealth/StealthRuntimeParams.cpp`
- `td/mtproto/stealth/StealthParamsLoader.cpp`

Add a dedicated sibling policy block on `StealthRuntimeParams`:

```cpp
struct RuntimeProfileRotationPolicy final {
  bool enabled{false};
  uint32 failure_threshold{2};
  double quarantine_ttl_seconds{300.0};
};
```

Notes:

1. Default `enabled=false` for the first landing. This avoids a silent global
   behavior change before the red suite proves the policy.
2. Do not add `max_quarantined_profiles_per_destination`. It is unnecessary
   configuration surface because the selectable profile set per platform is
   already naturally small and bounded.
3. Validate:
   - `failure_threshold` in `[2, 8]`
   - `quarantine_ttl_seconds` in `[30.0, 3600.0]`
4. Loader schema:
   - add an optional strict object named `profile_rotation`;
   - accepted fields are exactly `enabled`, `failure_threshold`, and
     `quarantine_ttl_seconds`;
   - missing `profile_rotation` means the default policy above;
   - unknown fields must fail strict loading.

Example config fragment:

```json
"profile_rotation": {
  "enabled": false,
  "failure_threshold": 2,
  "quarantine_ttl_seconds": 300.0
}
```

### 3. Keep the Adaptive Selector Internals Minimal

Files:

- `td/mtproto/stealth/TlsHelloProfileRegistry.h`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`

Prefer a minimal API surface. Do not expose broad new public helpers unless
tests or non-registry callers genuinely need them.

Acceptable public seam:

```cpp
struct RuntimeProfileSelectionDecision final {
  BrowserProfile profile{BrowserProfile::Chrome133};
  bool hello_uses_ech{false};
  bool avoided_quarantined_profile{false};
  uint32 quarantined_candidate_count{0};
};
```

Recommended design:

1. keep the quarantine map and candidate filtering helper internal to
   `TlsHelloProfileRegistry.cpp`;
2. expose only the minimal selection and reset hooks needed by `TlsInit` and
   tests;
3. keep `pick_runtime_profile(...)` unchanged as the stable legacy wrapper.

### 4. Quarantine the Actual Wire Variant

Represent one quarantinable unit as:

```cpp
struct RuntimeProfileWireVariant final {
  BrowserProfile profile{BrowserProfile::Chrome133};
  bool hello_uses_ech{false};
};
```

Represent one in-memory entry as:

```cpp
struct RuntimeProfileFailureEntry final {
  uint32 recent_failures{0};
  Timestamp quarantined_until;
};
```

Use a normalized key derived only from:

- normalized destination
- `BrowserProfile`
- `hello_uses_ech`

Do not include:

- raw route labels
- platform identity strings already implied by `BrowserProfile`
- serialized status text
- secrets

### 5. Attribute Failures Conservatively

Do not quarantine profiles on every TLS hello rejection.

Phase 1 profile quarantine should count only failure classes plausibly caused by
wire-shape rejection. Evidence-aware initial rule:

1. count:
   - malformed TLS hello response
   - transport/setup rejection after the hello was sent and while waiting for
     the hello response
2. do not count:
   - wrong regime
   - response hash mismatch
   - failures before the hello is sent

The exact mapping should be implemented through typed failure classification,
not string matching on log text.

If practical, reuse or mirror the existing typed failure taxonomy in
`td/telegram/net/ConnectionRetryPolicy.{h,cpp}` instead of inventing a second
semantic vocabulary.

### 6. Select Once Per Connection Attempt

Files:

- `td/mtproto/TlsInit.h`
- `td/mtproto/TlsInit.cpp`
- `td/mtproto/stealth/StealthConfig.h`
- `td/mtproto/stealth/StealthConfig.cpp`
- `td/mtproto/IStreamTransport.cpp`

This is a hard requirement.

Required contract:

1. a connection attempt chooses one runtime profile once;
2. that exact selection drives both transport shaping config and the emitted
   ClientHello;
3. failure/success accounting is recorded for that exact selected wire variant.

Minimum acceptable approach:

1. add an explicit-profile `StealthConfig` construction path;
2. plumb the selected profile from the connection path into both config creation
   and hello generation;
3. remove any adaptive-path dependence on a second independent
   `pick_runtime_profile(...)` call inside the same connection attempt.

Do not keep "prove it is harmless" as the fallback. The current divergence test
already proves the seam exists.

### 7. Use One Shared Handshake Snapshot

Inside `TlsInit::send_hello()`:

1. read one runtime snapshot;
2. compute the ECH decision once;
3. compute the adaptive profile decision against that same snapshot and ECH
   decision;
4. store:
   - selected `BrowserProfile`
   - final `hello_uses_ech`
   - whether quarantine affected the choice
   - how many candidates were quarantined

This avoids a smaller but real "reload between two runtime reads" seam.

### 8. Logging and Counters

Files:

- `td/mtproto/TlsInit.cpp`
- `td/mtproto/stealth/TlsHelloProfileRegistry.{h,cpp}`

Keep logging structured and secret-safe.

Add only compact operator-facing fields:

- `profile`
- `hello_uses_ech`
- `profile_rotation_enabled`
- `profile_rotation_avoided_quarantined`
- `profile_rotation_quarantined_candidates`
- `profile_rotation_failure_recorded`

Counters may be extended with:

```cpp
uint64 advisory_blocked_total;
uint64 profile_quarantine_hit_total;
uint64 profile_quarantine_all_blocked_total;
uint64 profile_failure_recorded_total;
uint64 profile_success_cleared_total;
```

Do not log:

- serialized quarantine entries
- secrets
- raw status payloads beyond the existing sanitized status message path

### 9. Persistence Is a Separate Follow-Up

Only after Phase 1 is proven should the project consider:

1. persistent quarantine state in `KeyValueSyncInterface`;
2. serialization format;
3. corruption handling;
4. restart semantics;
5. migration from older key shapes.

That follow-up would need its own plan and adversarial persistence suite, just
as the ECH route-failure cache already has.

## Test-First Plan

All tests must be written before implementation. New tests stay in separate
files.

### Contract Tests

Create:

- `test/stealth/test_runtime_profile_rotation_contract.cpp`

Pin:

1. legacy `pick_runtime_profile(...)` behavior remains deterministic when
   rotation is disabled;
2. the new adaptive path never returns a profile outside
   `allowed_profiles_for_platform(...)`;
3. one adaptive selection returns both `BrowserProfile` and final
   `hello_uses_ech`;
4. one connection attempt cannot report different selected profiles to
   `StealthConfig` and `TlsInit`.
5. iOS `TransportConfidence::Unknown` + `release_mode_profile_gating=true`
   becomes valid only when the new verified Apple iOS TLS lane has non-zero
   effective weight and `TlsOnly` + `release_gating` metadata.

### Positive Tests

Create:

- `test/stealth/test_runtime_profile_rotation_positive.cpp`

Cover:

1. iOS unknown-confidence release lane:
   - the new verified Apple iOS TLS lane is reachable at `Unknown`;
   - release-mode selection picks it and never advisory `IOS14`
2. Android strong-confidence non-release lane:
   - quarantine `AndroidChromium_Alps` wire variant
   - selector can move to `Firefox149_Android` if weighted and allowed
3. Darwin:
   - quarantining one release-eligible Darwin lane still allows another
4. Windows:
   - `Chrome147_Windows` can rotate to `Firefox149_Windows`
5. Linux:
   - one Chrome lane can rotate to another Linux-local lane or Firefox

### Negative Tests

Create:

- `test/stealth/test_runtime_profile_rotation_negative.cpp`

Cover:

1. `TransportConfidence::Unknown` on Android:
   - quarantining advisory `Android11_OkHttp_Advisory` must not unlock
     `AndroidChromium_Alps` or `Firefox149_Android`
2. release-gated Android:
   - quarantining `AndroidChromium_Alps` must not unlock
     `Firefox149_Android` or `Android11_OkHttp_Advisory`
   - selector stays fail-closed and increments the all-blocked counter
3. iOS before the new Apple iOS TLS metadata lands:
   - `Unknown` + release mode still fails validation in the red phase
4. `response_hash_mismatch`:
   - does not quarantine the selected profile
5. wrong-regime rejection:
   - does not quarantine the selected profile
6. all candidates quarantined:
   - selector stays inside the already-allowed platform set
   - increments `profile_quarantine_all_blocked_total`

### Edge Case Tests

Create:

- `test/stealth/test_runtime_profile_rotation_edge.cpp`

Cover:

1. `failure_threshold` minimum and maximum are accepted;
2. `failure_threshold` below `2` and above `8` are rejected;
3. `quarantine_ttl_seconds` minimum and maximum are accepted;
4. `quarantine_ttl_seconds` below `30.0` and above `3600.0` are rejected;
5. missing `profile_rotation` in strict config preserves disabled defaults;
6. unknown fields inside `profile_rotation` fail strict loading;
7. zero weighted alternatives do not become fallback candidates when the
   originally selected profile is quarantined.

### Adversarial Tests

Create:

- `test/stealth/test_runtime_profile_rotation_adversarial.cpp`

Cover:

1. empty destination:
   - no crash
   - no ambiguous key fanout
2. case aliases:
   - `Example.COM` and `example.com` share quarantine state
3. ECH split:
   - quarantining `profile=X, hello_uses_ech=true` must not poison
     `profile=X, hello_uses_ech=false`
4. platform isolation:
   - Android quarantine state never affects Darwin / Windows / Linux
5. false-positive resistance:
   - repeated `response_hash_mismatch` never rotates profile
6. iOS closure integrity:
   - no test may pass by silently relabeling `IOS14` advisory metadata as
     release-grade

### Integration Tests

Create:

- `test/stealth/test_tls_init_profile_rotation_integration.cpp`
- `test/stealth/test_stealth_config_tls_init_profile_rotation_coherence.cpp`

Cover:

1. `TlsInit` malformed-response path records failure for the actual selected
   wire variant
2. `TlsInit` valid-response path clears quarantine for the actual selected wire
   variant
3. setup rejection while waiting for hello response counts only once
4. one connection attempt uses one profile in both config and emitted hello
5. iOS `Unknown` + release mode uses the new verified Apple iOS TLS lane
   end-to-end without crossing through advisory `IOS14`

### Stress Tests

Create:

- `test/stealth/test_runtime_profile_rotation_stress.cpp`

Cover:

1. concurrent selection and failure recording for the same destination
2. repeated fail-connect-success loops
3. TTL expiry and re-eligibility under concurrency

### Light Fuzz Tests

Create:

- `test/stealth/test_runtime_profile_rotation_fuzz.cpp`

Cover deterministic mutation sets rather than non-reproducible randomness:

1. mutated destination strings:
   - empty
   - dot-only
   - mixed case
   - leading and trailing dots
   - repeated dots
   - very long label sequences within existing project string limits
2. mutated `profile_rotation` JSON values:
   - negative integers
   - large integers
   - fractional threshold values
   - booleans where numbers are required
   - strings where numbers are required
   - `null`
3. malformed failure-class inputs to the quarantine-recording seam must fail
   closed without incrementing quarantine counters.

## Implementation Order

1. Add red tests for the missing iOS/default release-grade closure.
2. Add contract tests for single-selection and disabled-path compatibility.
3. Add negative/adversarial tests proving what must *not* rotate.
4. Add the new verified Apple iOS TLS runtime profile and effective-weight
   bridge.
5. Add selector internals with in-memory-only quarantine.
6. Add conservative failure classification.
7. Add single-selection plumbing through `StealthConfig` and `TlsInit`.
8. Add integration tests.
9. Add edge-case and light-fuzz tests.
10. Add stress tests.
11. Run focused verification.
12. Run full verification.

## Verification

Focused:

```bash
cmake --build build --target run_all_tests --parallel 12
./build/test/run_all_tests --filter 'RuntimeProfileRotation|TlsInitProfileRotation|StealthConfigTlsInitProfileRotation'
./build/test/run_all_tests --filter 'RuntimeProfileRotationEdge|RuntimeProfileRotationFuzz'
./build/test/run_all_tests --filter 'MobileReleaseGradeLane|TlsRuntimeProfilePolicyFailClosed|TlsRuntimeReleaseProfileGatingContract|TLS_MultiDumpIosAppleTlsBaseline'
```

Full:

```bash
ctest --test-dir build --output-on-failure -j 12
```

## Security Checklist

1. Rotation never becomes a downgrade path.
2. Rotation never crosses platform-local allowed sets.
3. Rotation never bypasses `TransportConfidence::Unknown`.
4. Rotation never promotes advisory lanes into release mode.
5. The iOS/default closure never works by re-labeling `IOS14` advisory evidence
   as release-grade.
6. Quarantine keys never contain secrets.
7. Failure attribution never treats wrong-secret / wrong-regime errors as
   profile-block evidence.
8. One connection attempt never splits config profile and wire profile.
9. Healthy repeated success does not cause churn.
10. The first slice adds no new persistent poisoning surface.

## Follow-Up Questions

1. After Phase 1, is there enough real evidence to justify persistence across
   restart, or is in-memory TTL sufficient?
2. Should profile-rotation enablement stay opt-in until field traces show it
   helps more than it harms?
3. If later persistence is needed, should it reuse the existing runtime KV store
   namespace or use a dedicated store abstraction to avoid coupling with ECH
   route-failure semantics?
