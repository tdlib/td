<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# DPI Connection Lifetime Mitigation Plan

**Date:** 2026-04-09
**Repository:** `tdlib-obf`
**Threat model:** TSPU / hard inline DPI with long-window per-flow telemetry, ECH blocked in RU, QUIC blocked for RU -> non-RU.
**Scope:** Active stealth proxy connection lifetime, rotation, overlap, and reconnect camouflage.
**Method:** TDD first. Tests are written before code. All new tests live in separate files.

---

## 1. Executive Verdict

You are **not wrong** to treat a 20-30 minute pinned proxy connection as a serious DPI risk.

But the precise statement matters:

- **Long-lived TLS connections are not universally anomalous.** Real browsers and mobile apps do keep some TLS sessions open for a long time.
- **Under the current `tdlib-obf` stealth design, the risk is still real.** The proxy path advertises `http/1.1`-style cover, does not implement a true browser-like HTTP/WebSocket semantics layer, and currently does **not** enforce an active-connection max-age rotation policy.
- **Therefore the problem is not "duration alone".** The problem is a **long-lived, single-origin, MTProto-carrying proxy flow** whose lifetime, reuse, idle behavior, and reconnect shape are not yet aligned to a measured browser-like baseline.

The correct mitigation is **not** a blind timer that kills sockets every N seconds.

The correct mitigation is a new **capture-driven active connection lifecycle camouflage layer**:

1. measure realistic lifetime baselines first;
2. enforce them in production on active stealth sessions;
3. rotate with **make-before-break overlap**;
4. gate the rotation through existing anti-churn and destination-budget logic;
5. validate with adversarial tests and offline smoke artifacts.

---

## 2. Verified Current State

The repository already contains part of the policy surface, but not the full enforcement path.

### 2.1. What exists today

- `td/mtproto/stealth/StealthRuntimeParams.h` already defines:
  - `min_conn_lifetime_ms`
  - `max_conn_lifetime_ms`
  - `anti_churn_min_reconnect_interval_ms`
  - `max_connects_per_10s_per_destination`
  - `max_destination_share`
- `td/telegram/net/ConnectionPoolPolicy.cpp` uses `max_conn_lifetime_ms` only to clamp **pooled ready-connection retention**.
- `td/telegram/net/Session.cpp` uses `ConnectionPoolPolicy::is_pooled_connection_expired(...)` only for `cached_connection_`.
- `td/telegram/net/Session.cpp` also expires only **non-main idle sessions** via `ACTIVITY_TIMEOUT`.
- `test/analysis/check_flow_behavior.py` already declares a detection rule for:
  - `pinned-socket-anomaly`: lifetime > `max_conn_lifetime_ms` with sustained traffic.

### 2.2. What is missing today

There is no verified production path that says:

- "this active main or long-poll stealth connection has aged out; prepare a successor now";
- "route new queries to a fresh socket while the old one drains";
- "do not keep a high-traffic stealth socket open past policy unless an explicit exemption applies".

So the current state is:

- **policy fields exist**;
- **analysis expectations exist**;
- **idle pooled retention exists**;
- **active lifetime camouflage does not yet exist**.

That is the architectural gap this plan closes.

---

## 3. Why This Matters To DPI

For a strong censor, per-flow duration is a cheap and powerful feature when combined with the rest of the metadata already visible on path:

- TCP 5-tuple lifetime
- bytes sent / received over lifetime
- idle-gap structure
- reconnect cadence
- overlap windows between old and new sockets
- number of concurrent connections to the same proxy IP:port
- post-handshake TLS record sizes and directional asymmetry

### 3.1. Why 20-30 minutes is dangerous in the current cover story

With the current design, the flow looks like:

- one proxy destination;
- browser-like TLS hello;
- then opaque Application Data for a very long time;
- no real HTTP request/response semantics;
- no genuine WebSocket upgrade;
- no genuine browser tab/navigation/resource lifecycle.

That creates a classifier opportunity:

- a **single pinned origin flow**;
- with persistent bidirectional activity;
- over a duration inconsistent with ordinary `http/1.1` web-navigation cover;
- often paired with MTProto-specific keepalive / update behavior.

### 3.2. Why a naive reconnect timer is also dangerous

If you simply kill the socket every 120 or 180 seconds, you create a new detector feature:

- periodic reconnects to the same destination;
- repeated greeting phase fingerprints;
- reconnect storms on bad links;
- bursty overlap with other session sockets;
- user-visible stalls during auth or long-poll transitions.

So the mitigation must optimize **two opposing risks** at once:

1. avoid unnaturally long pinned flows;
2. avoid unnaturally frequent reconnects.

---

## 4. Design Principles

1. **Capture-driven only.** No magic fixed lifetime like "always 180 seconds" unless it comes from a measured envelope and is used only as a hard safety ceiling.
2. **Make-before-break.** Never hard-kill an active session if a successor can be prepared first.
3. **Session-layer ownership.** Connection lifecycle policy belongs in `Session` / `ConnectionCreator`, not in the transport decorator.
4. **Decorator assists, not governs.** `StealthTransportDecorator` may shape the greeting and drain phases of a successor socket, but must not decide when sockets rotate.
5. **Bounded overlap.** Rotation is allowed to create at most a tightly bounded temporary overlap to avoid self-DoS and destination-count spikes.
6. **Anti-churn aware.** Existing `ConnectionFlowController` and `ConnectionDestinationBudgetController` remain authoritative guards.
7. **Fail closed on invalid config.** Bad lifetime/overlap/jitter settings must be rejected at validation/load time.
8. **Role-aware.** `main`, `long_poll`, upload, download, and cached/ready sockets do not all need the same policy.

---

## 5. Strategic Recommendation

Under the **current** stealth cover, assume that a 20-30 minute pinned proxy connection is a real anomaly until proven otherwise by captures.

That means:

- do **not** justify the behavior with "browsers also keep connections open" in the abstract;
- do **not** add a crude periodic reconnect;
- do **add** a dedicated PR after PR-S7:
  - **PR-S8: Stealth Active Connection Lifecycle Camouflage**.

If future captures prove that the chosen cover family really does support 20-30 minute long-lived single-origin TLS flows with matching timing and connection-count behavior, then the runtime policy can be relaxed. Until that evidence exists, the safer engineering assumption is that the current behavior remains detector-visible.

---

## 6. PR-S8 Architecture

### 6.1. PR-S8.0: Lifetime Baseline Extraction

The existing packet-size corpus is not enough by itself. It captures record-size behavior well, but active-lifetime camouflage needs a dedicated baseline set.

#### Required new analysis outputs

Add a root-to-test analysis pipeline that extracts:

- per-connection lifetime in milliseconds;
- first-payload timestamp vs TCP-establishment timestamp;
- idle-gap histogram per connection;
- bytes sent / received before close;
- overlap windows when a browser replaces an old connection with a new one;
- destination concentration over time windows;
- lifetime distribution by platform and cover family.

#### Required new corpus classes

Use real captures only. No synthetic lifetime generation.

At minimum, collect or derive:

- short-lived domestic page-load HTTPS sessions;
- medium-lived domestic authenticated web sessions;
- mobile background-sync / notification-like HTTPS sessions;
- long-lived but legitimate TLS sessions to Russian-reachable services where TCP persists without QUIC.

#### Important constraint

Because the proxy path currently uses `http/1.1`-style cover rather than real `h2` multiplexing or WebSocket semantics, the baseline must not be contaminated with connection families we cannot plausibly imitate on the wire.

In practice this means:

- ordinary `http/1.1` browsing and authenticated app-background flows are relevant;
- true WebSocket/SSE/gRPC cover is **not** relevant unless we implement that protocol surface for real.

#### New analysis files

- `test/analysis/extract_tls_connection_lifetime_baselines.py`
- `test/analysis/check_connection_lifetime_distribution.py`
- optional integration into `test/analysis/run_corpus_smoke.py`

#### Output artifact

`artifacts/connection_lifecycle_baseline.json`

Example schema:

```json
{
  "profile_family": "desktop_http1_proxy_like",
  "lifetime_ms": {
    "p10": 18000,
    "p50": 91000,
    "p90": 240000,
    "p99": 540000
  },
  "idle_gap_ms": {
    "p50": 7200,
    "p90": 28000
  },
  "replacement_overlap_ms": {
    "p50": 350,
    "p95": 1800
  },
  "max_parallel_per_destination": 6
}
```

### 6.2. PR-S8.1: Active Connection Lifecycle State Machine

Add a dedicated state machine for active stealth connections.

Suggested states:

- `Warmup`
- `Eligible`
- `RotationPending`
- `Draining`
- `Retired`

Each active connection tracks:

- `opened_at`
- `first_payload_at`
- `last_payload_at`
- `bytes_sent`
- `bytes_received`
- `queries_sent`
- `rotation_attempts`
- `has_successor`
- `role` (`main`, `long_poll`, `upload`, `download`, `cached`)
- `rotation_exemption_reason`

#### Policy model

Add a new `ActiveConnectionLifecyclePolicy` under runtime params:

```cpp
struct ActiveConnectionLifecyclePolicy {
  uint32 rotate_after_ms_min;
  uint32 rotate_after_ms_p50;
  uint32 rotate_after_ms_p90;
  uint32 hard_ceiling_ms;
  uint32 idle_retire_after_ms;
  uint32 overlap_max_ms;
  uint32 rotation_backoff_ms;
  uint32 max_overlap_connections_per_destination;
  uint32 byte_budget_before_rotation;
  bool enable_active_rotation;
};
```

This policy must be **derived from the capture baseline**, not manually invented.

#### Critical rule

`max_conn_lifetime_ms` must stop meaning only "ready pooled retention".

It should become:

- pooled retention ceiling for cached/ready sockets;
- input ceiling for active lifecycle rotation;
- analysis invariant for smoke artifacts.

### 6.3. PR-S8.2: Make-Before-Break Rotation

When an active stealth connection ages into the rotation window:

1. **Do not close it immediately.**
2. Request a successor connection through the existing creation path.
3. Apply normal stealth greeting camouflage to the successor.
4. Once successor is authenticated / ready, route **new** queries to it.
5. Let the old connection drain in-flight work for a bounded overlap period.
6. Close the old connection once drain completes or overlap budget expires.

#### Rotation triggers

- connection age exceeds sampled rotation target;
- connection age exceeds hard ceiling with sustained traffic;
- idle-after-active transition exceeds idle retire threshold;
- byte budget exceeded on a single socket;
- route change / proxy IP change / profile change invalidates the cover continuity.

#### Rotation suppression rules

Do **not** rotate immediately when:

- auth key exchange is in progress;
- the only available socket is still in greeting/auth warmup;
- logout / shutdown is active;
- the destination budget says opening a successor would exceed safe overlap;
- the flow controller says reconnect would violate anti-churn;
- a large upload/download chunk is mid-flight and no safe handover point exists.

#### Hard ceiling behavior

If a connection crosses the hard ceiling and no clean overlap is possible:

- mark it as `RotationPending`;
- prioritize successor creation as soon as budget allows;
- do not create repeated immediate retries;
- emit explicit telemetry that the flow is in over-age degraded mode.

### 6.4. PR-S8.3: Session Role Semantics

Rotation must be role-aware.

#### `main_connection_`

- new queries can be steered to the successor once ready;
- the old socket drains sent but unanswered requests for a bounded window.

#### `long_poll_connection_`

- old long-poll may remain briefly while the successor is established;
- avoid a period where no long-poll is alive at all;
- overlap must still respect destination-count caps.

#### upload / download sessions

- rotate only at chunk boundaries or other explicit quiescent points;
- if a transfer is too large for the lifetime envelope, expose this as a policy tradeoff instead of silently violating the cap.

#### cached / ready sockets

- continue using `ConnectionPoolPolicy`, but unify the policy language with active lifetime semantics.

### 6.5. PR-S8.4: Integration Points

Likely implementation surface:

- `td/mtproto/stealth/StealthRuntimeParams.h`
- `td/mtproto/stealth/StealthRuntimeParams.cpp`
- `td/mtproto/stealth/StealthParamsLoader.cpp`
- `td/telegram/net/Session.h`
- `td/telegram/net/Session.cpp`
- `td/telegram/net/ConnectionCreator.cpp`
- `td/telegram/net/ConnectionPoolPolicy.h`
- `td/telegram/net/ConnectionPoolPolicy.cpp`
- `td/telegram/net/ConnectionFlowController.cpp`
- `td/telegram/net/ConnectionDestinationBudgetController.cpp`
- `td/mtproto/stealth/StealthTransportDecorator.cpp`

Implementation split by responsibility:

- `Session` owns active socket age, state, and role-aware handover.
- `ConnectionCreator` owns successor opening and budget-gated overlap.
- `ConnectionPoolPolicy` remains the utility for idle/ready retention.
- `StealthTransportDecorator` shapes the new socket's greeting and drain-phase output only.

### 6.6. PR-S8.5: Artifact And Smoke Validation

Extend the smoke pipeline with a lifecycle artifact:

`artifacts/connection_lifecycle_report.json`

Per connection include:

- destination key
- proxy id
- role
- started_at_ms
- first_payload_at_ms
- ended_at_ms
- bytes_sent
- bytes_received
- reused
- rotation_reason
- successor_opened_at_ms
- overlap_ms
- over_age_exemption

The report must fail when:

- any active connection with `bytes_sent > 0` exceeds the hard ceiling without exemption;
- median lifetime is below the baseline floor due to over-rotation;
- reconnect cadence violates anti-churn;
- overlap count exceeds allowed per-destination temporary budget;
- one destination monopolizes the connection budget during rotation.

---

## 7. TDD Test Plan

All tests must be written first. All new tests must be in separate files.

### 7.1. Analysis Baseline Extraction

**File:** `test/analysis/test_extract_connection_lifetime_baselines.py`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `test_extracts_lifetime_percentiles_from_known_capture` | Positive | Parses a known capture and emits monotonic p10/p50/p90/p99 lifetime values. |
| 2 | `test_idle_gap_histogram_is_monotonic` | Invariant | Idle-gap percentiles must be ordered and non-negative. |
| 3 | `test_rejects_capture_without_tcp_close_or_timeout_marker` | Negative | Baseline extractor must reject ambiguous lifetime samples instead of inventing a close time. |
| 4 | `test_profile_family_split_does_not_mix_http2_and_http11_cover` | Security | Prevent accidental baseline contamination across incompatible cover families. |
| 5 | `test_overlap_window_extraction_handles_connection_replacement` | Edge case | Two flows with short overlap produce correct overlap metrics. |
| 6 | `test_empty_corpus_fails_closed` | Negative | No baseline file should be emitted for an empty input set. |

**File:** `test/analysis/test_check_connection_lifetime_distribution.py`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `test_real_browser_like_lifetimes_pass_baseline_check` | Positive | Known-good corpus distribution passes. |
| 2 | `test_single_pinned_socket_anomaly_is_rejected` | Adversarial | One 30-minute active flow with sustained bytes fails. |
| 3 | `test_periodic_120_second_reconnect_pattern_is_rejected` | Adversarial | Fixed reconnect timer pattern fails as synthetic churn. |
| 4 | `test_median_lifetime_too_short_is_rejected` | Adversarial | Excessive churn is caught. |
| 5 | `test_overlap_spike_above_budget_is_rejected` | Adversarial | Too many overlapping successor sockets fail. |
| 6 | `test_destination_monopoly_during_rotation_is_rejected` | Security | Rotation cannot quietly violate destination share policy. |

### 7.2. Policy And Validation

**File:** `test/stealth/test_active_connection_lifecycle_policy.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `PolicyDefaultsWithinValidatedBounds` | Positive | Default lifecycle policy is valid. |
| 2 | `HardCeilingCannotBeBelowRotateWindow` | Negative | Invalid ordering fails validation. |
| 3 | `OverlapLimitCannotExceedConnectionCap` | Security | Config that could self-DoS the destination is rejected. |
| 4 | `ByteBudgetMustBeNonZeroWhenRotationEnabled` | Edge case | Invalid zero budget fails closed. |
| 5 | `LifecyclePolicyDisabledLeavesLegacyBehaviorExplicit` | Negative | Disabled policy is an explicit state, not an accidental fallback. |

### 7.3. State Machine Logic

**File:** `test/stealth/test_active_connection_rotation_state_machine.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `WarmupDoesNotRotateBeforeMinimumLifetime` | Positive | No early rotation. |
| 2 | `EligibleConnectionEntersRotationPendingAtSampledDeadline` | Positive | Age threshold triggers pending state. |
| 3 | `RotationPendingTransitionsToDrainingWhenSuccessorReady` | Positive | Handover only starts after successor readiness. |
| 4 | `DrainingTransitionsToRetiredAfterOutstandingQueriesFinish` | Positive | Clean drain path works. |
| 5 | `HardCeilingWithoutSuccessorRaisesOverAgeSignal` | Adversarial | Over-age degraded mode is visible and deterministic. |
| 6 | `ClockRollbackDoesNotTriggerPrematureRotation` | Edge case | Timer skew cannot create spurious age-outs. |

### 7.4. Rotation Gates And Exemptions

**File:** `test/stealth/test_active_connection_rotation_gates.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `AuthHandshakeBlocksRotation` | Integration | Active auth setup suppresses rotation. |
| 2 | `LongPollMaintainsAtLeastOneLiveSocketDuringRotation` | Integration | No long-poll blackout window. |
| 3 | `AntiChurnGateDefersRotationWhenReconnectTooSoon` | Adversarial | Flow controller remains authoritative. |
| 4 | `DestinationBudgetGateDefersOverlapWhenCapWouldBeExceeded` | Adversarial | Budget controller blocks unsafe successor opening. |
| 5 | `LargeUploadDefersRotationUntilChunkBoundary` | Edge case | No mid-chunk teardown. |
| 6 | `LogoutSuppressesNewSuccessorCreation` | Negative | Shutdown path stays deterministic. |

### 7.5. Adversarial Runtime Behavior

**File:** `test/stealth/test_active_connection_rotation_adversarial.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `PinnedHighTrafficSocketEventuallyForcesSuccessorPreparation` | Adversarial | Sustained traffic cannot live forever silently. |
| 2 | `FixedPeriodReconnectSignatureNotProduced` | Adversarial | Rotation deadlines are sampled from baseline envelope, not periodic. |
| 3 | `SuccessorOverlapNeverCreatesMoreThanOneTemporaryExtraSocketPerRole` | Security | Overlap remains bounded. |
| 4 | `RepeatedRotationDeferralsDoNotSpinInTightLoop` | Security | No retry storm under blocked conditions. |
| 5 | `RotationDoesNotBypassProxyRoute` | Security | No direct DC fallback during successor creation. |
| 6 | `PooledRetentionAndActiveRotationPoliciesCannotConflictIntoPingPongCloseOpen` | Adversarial | Prevent close/reopen oscillation. |

### 7.6. Integration With Session And Creator

**File:** `test/stealth/test_active_connection_rotation_integration.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `MainConnectionHandoverPreservesOutstandingQueryDelivery` | Integration | Queries survive rotation. |
| 2 | `LongPollHandoverPreservesReceiveContinuity` | Integration | Updates are not dropped by socket swap. |
| 3 | `CachedConnectionStillExpiresUnderUnifiedPolicy` | Integration | Idle pool semantics remain intact. |
| 4 | `SuccessorGreetingUsesNormalStealthGreetingCamouflage` | Integration | Rotation does not skip greeting shaping on the new socket. |
| 5 | `ConnectionLifecycleArtifactContainsRotationMetadata` | Integration | Smoke artifact contains the expected fields. |

### 7.7. Light Fuzz

**File:** `test/stealth/test_active_connection_rotation_fuzz.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `LightFuzz_RandomAgesBudgetsAndOutstandingQueries_NoInvariantBreak` | Fuzz | Randomized state inputs never break lifecycle invariants. |
| 2 | `LightFuzz_RandomRotationSuppressionReasons_NoDuplicateSuccessors` | Fuzz | No double-successor race. |
| 3 | `LightFuzz_RandomClockSkewAndTimeouts_NoPrematureRetire` | Fuzz | Time anomalies remain safe. |

### 7.8. Stress

**File:** `test/stealth/test_active_connection_rotation_stress.cpp`

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `StressManySessionsRotationBudgetStable` | Stress | Large session sets do not exceed overlap or destination budgets. |
| 2 | `StressFrequentAgeChecksRemainDeterministic` | Stress | Repeated lifecycle polling remains stable and allocation-safe enough for the path. |
| 3 | `StressNetworkFlapDuringRotationDoesNotCreateReconnectStorm` | Stress | Bad-network conditions do not amplify rotation into a storm. |

---

## 8. Security And ASVS L2 Considerations

This plan must stay aligned with the repository's security-first rules.

1. **No direct-route fallback during rotation.** Successor creation must honor the existing proxy-routing hardening.
2. **Bound all overlap counts and timers.** Invalid policy values must fail closed in `StealthRuntimeParams` validation and loader parsing.
3. **No attacker-controlled resource blowup.** Rotation retries, overlap sockets, and artifact buffers must be bounded.
4. **No silent behavior change outside stealth mode.** Active lifetime camouflage must activate only on the intended stealth proxy path.
5. **Telemetry must not expose secrets.** Artifacts contain timing, counts, and reasons only, not payload material.

---

## 9. Rollout Order

1. **Write red analysis tests first.** Baseline extraction and lifetime-distribution rejection tests.
2. **Add telemetry-only artifacts.** No behavioral changes yet; prove the current pinned-socket anomaly exists in generated reports.
3. **Add policy types and validation.** Loader + runtime params + unit tests.
4. **Add state machine logic with no network side effects.** Red unit tests first.
5. **Add successor overlap in `Session` / `ConnectionCreator`.** Integration tests first.
6. **Enable only in stealth mode.** Keep non-stealth behavior unchanged.
7. **Run smoke and adversarial suites.** Reject both over-long pinned sockets and over-frequent churn.

---

## 10. Acceptance Criteria

The mitigation is not complete until all of the following are true:

1. No active stealth socket with sustained traffic exceeds the hard ceiling without an explicit, test-covered exemption.
2. Median and upper-quantile connection lifetimes remain inside the measured baseline envelope for the selected cover family.
3. Rotation does not create reconnect storms or destination-share spikes.
4. Main and long-poll session continuity survive handover.
5. Cached/ready socket expiry semantics remain correct.
6. All new tests are in separate files and pass.
7. Offline artifact validation catches both failure modes:
   - pinned sockets;
   - synthetic churn.

---

## 11. Final Recommendation

Treat this as a **new dedicated stealth workstream**, not as a footnote under packet sizing.

The clean way forward is:

- keep the current packet-size work on its own track;
- add **PR-S8: Stealth Active Connection Lifecycle Camouflage**;
- make it capture-driven, role-aware, overlap-safe, anti-churn-gated, and TDD-first.

If later captures prove that your chosen cover family legitimately sustains 20-30 minute single-origin TLS sessions under the same observable semantics, then the runtime policy can be widened.

Until then, the present behavior should be treated as an unresolved DPI fingerprint.

---

## 12. Handover

### 12.1. Current implementation status

The repository is now past the "plan only" stage for active lifetime work.

The following runtime pieces are already implemented:

- `td/telegram/net/ConnectionLifecycleReport.{h,cpp}` now provides a thread-safe lifecycle report builder with:
  - `begin_connection(...)`
  - `add_write(...)`
  - `add_read(...)`
  - `mark_reused(...)`
  - `end_connection(...)`
  - JSON snapshot/export support
- `td/telegram/net/ConnectionCreator.{h,cpp}` now uses the raw-connection stats callback path to emit live lifecycle telemetry:
  - a connection record is opened when `RawConnection` is created;
  - bytes sent / received are accumulated through the existing stats callback;
  - ready pooled connections that age out are explicitly closed in the report;
  - `get_connection_lifecycle_report_json()` exposes the current artifact as JSON.
- `td/telegram/net/Session.cpp` now participates in the lifecycle telemetry and initial active-lifetime mitigation:
  - cached connection reuse calls `on_connection_reused()`;
  - expired cached pooled connections call `on_connection_closed(...)` before reset;
  - each ready active connection receives a sampled `retire_at_` deadline derived from `flow_behavior.min_conn_lifetime_ms` and `flow_behavior.max_conn_lifetime_ms`;
  - `main_connection_` and `long_poll_connection_` are retired only when the deadline has passed and the specific socket has no in-flight work.
- `td/telegram/net/ConnectionLifecyclePolicy.{h,cpp}` now exists as a dedicated utility for:
  - randomized active retirement deadline sampling;
  - `retire_due` checks.
- `td/mtproto/RawConnection.{h,cpp}` now exposes lifecycle hooks on the stats callback so the live report path can observe:
  - open
  - reuse
  - close

### 12.2. What this actually gives us today

This is **not** the full PR-S8 design yet.

What exists now is a conservative first enforcement slice:

- active sockets no longer have to remain pinned forever;
- retirement timing is randomized inside the existing runtime lifetime window instead of using a fixed reconnect period;
- retirement is suppressed when the socket still has in-flight work;
- the codebase now has a real runtime telemetry path for connection lifetime artifacts instead of only a hypothetical analysis schema.

### 12.3. What is still missing

The major architectural gap is still the same one identified in the plan: **make-before-break handover is not implemented yet**.

Still missing:

- no successor-preparation state machine;
- no explicit `RotationPending -> Draining -> Retired` overlap flow;
- no handover where new queries move to a ready successor while the old socket drains;
- no explicit over-age degraded-mode telemetry when a successor cannot be opened;
- no artifact file-output path wired into the Python smoke pipeline for live runtime lifecycle reports;
- no role-specific upload/download chunk-boundary rotation behavior;
- no full PR-S8 integration tests for handover continuity.

In other words: current code can retire aged sockets conservatively, but it still does **break-before-make** rather than **make-before-break**.

### 12.4. Verified tests that passed for this slice

Focused native validation that was run and passed:

- `./build/test/run_all_tests --filter ConnectionLifecyclePolicy`
- `./build/test/run_all_tests --filter ConnectionLifecycleReport`
- `./build/test/run_all_tests --filter ConnectionPoolPolicy`
- `./build/test/run_all_tests --filter SessionWakeup`
- `./build/test/run_all_tests --filter SessionHintWiring`
- `./build/test/run_all_tests --filter ConnectionCreatorProxyRouteSecurity`

Important local quirk observed during validation:

- the rebuilt `build/test/run_all_tests` binary sometimes loses execute permission after the build task finishes, so `chmod +x build/test/run_all_tests` may be needed before filtered test runs.

### 12.5. Recommended next PR-sized step

The next implementation step should be the smallest slice that closes the current architectural gap:

1. Add explicit successor state to `Session::ConnectionInfo` or adjacent lifecycle state.
2. Teach `Session` / `ConnectionCreator` to prepare a successor before retiring an aged active socket.
3. Route new queries to the successor once ready while the old socket drains for a bounded overlap window.
4. Add integration tests first for:
   - main-connection handover without lost outstanding queries;
   - long-poll continuity without blackout;
   - overlap bounded by destination/share/anti-churn gates.

### 12.6. Practical warning for the next engineer

Do **not** mistake the current `retire_at_` logic for completion of PR-S8.

It is a useful mitigation and telemetry foundation, but by itself it does not yet produce browser-like connection replacement behavior. The next engineer should treat this branch as:

- telemetry path: present;
- randomized active lifetime ceiling: present;
- safe overlap handover: absent.