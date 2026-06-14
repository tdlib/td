<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# PR #22 Review — adaptive runtime profile rotation

Scope: static PR review of `telemt/tdlib-obf#22` at `origin/pr-22` (`e91e98f96ce3e884dc0bb0b929f22249c23d1c33`) against repo principles, architecture, TDD expectations, and production readiness. Per request, I did not run tests, sanitizers, or build verification in this pass.

Base used for diff: `origin/master` (`3085f8e104cab126807657f3143d0b8ae74dfd3f`).

## Findings

### 1. High — the main handoff still does not preserve the actual wire variant, only the profile id

Files:
- `td/telegram/net/ConnectionCreator.cpp:65-84`
- `td/mtproto/TlsInit.cpp:268-305`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1460-1516`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1614-1654`
- `docs/Plans/ADAPTIVE_RUNTIME_PROFILE_ROTATION_PLAN_2026-06-12.md:505-519`

`ConnectionCreator::stamp_runtime_profile_selection()` preselects only `BrowserProfile`, and it does so against the ECH decision that exists at connection-setup time. `TlsInit::send_hello()` later recomputes the ECH decision and derives the final `hello_uses_ech_` from that second read, but it does not recompute the adaptive selection and it does not receive a shared handshake snapshot.

That leaves a real TOCTOU seam in the exact place the plan said to close. The plan explicitly required one shared snapshot carrying the selected profile and the final `hello_uses_ech` value. The implementation carries only `selected_profile`.

Why this matters:
- quarantine is keyed by `(destination, BrowserProfile, hello_uses_ech)`, not just by `BrowserProfile`;
- if the ECH circuit-breaker state changes between `stamp_runtime_profile_selection()` and `TlsInit::send_hello()`, the selector can avoid or accept one wire variant while `TlsInit` emits the other;
- the result is a coherent profile id across config and hello, but not necessarily the wire variant the adaptive selector actually evaluated.

Concrete failure mode:
- selection runs while `ech_mode == Rfc9180Outer`, so `pick_runtime_profile_adaptive()` evaluates the ECH-on variant;
- before `send_hello()`, another connection trips the destination ECH circuit breaker, so the second `get_runtime_ech_decision()` returns `Disabled`;
- the same preselected profile is now emitted as the ECH-off variant, even if that exact ECH-off variant was the quarantined one the selector should have avoided.

This means H1 is only partially fixed: the PR removed profile-id divergence, but it did not actually make selection, wire emission, and quarantine operate on one immutable per-attempt handshake snapshot.

### 2. High — `AppleIosTls` and `IOS14` are wire-identical but the rotation/quarantine logic treats them as different escape candidates

Files:
- `td/mtproto/BrowserProfile.cpp:674-715`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:133-155`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1077-1112`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1460-1516`

`make_apple_ios_tls_impl()` is a direct clone of `make_ios14_impl()` with only `profile.name` changed. The registry mirrors that: the `PROFILE_SPECS` entries for `IOS14` and `AppleIosTls` are structurally identical on the wire.

But the adaptive rotation code keys quarantine by `BrowserProfile` enum value plus `hello_uses_ech`, and `pick_runtime_profile_adaptive()` treats both entries as independent alternatives.

Why this matters:
- on iOS, a block against the `IOS14` wire image is also a block against `AppleIosTls`, because the wire image is the same;
- the quarantine map does not know that, so the runtime can "rotate" from `IOS14` to `AppleIosTls` and record `avoided_quarantined_profile = true` even though the emitted fingerprint did not change;
- under `TransportConfidence::Unknown`, the selectable iOS pool can collapse to exactly these two aliases, so the feature burns failure budget on a no-op rotation before reaching the true all-blocked state.

This is not just a metadata purity issue. It makes the iOS rotation graph inaccurate and can produce false operator confidence that the runtime escaped a blocked fingerprint when it only switched labels.

### 3. Medium — `ProxyChecker` still bypasses the new single-selection handoff and reintroduces split-profile selection on a real network path

Files:
- `td/telegram/net/ProxyChecker.h:46-48`
- `td/telegram/net/ProxyChecker.cpp:65-68`
- `td/telegram/net/ProxyChecker.cpp:105-106`
- `td/mtproto/IStreamTransport.cpp:137-144`

The new handoff is wired only through `ConnectionCreator`. `ProxyChecker` still constructs a fresh `TransportType` every time `TestProxyRequest::get_transport()` is called, with no `selected_profile` stamped.

It then uses two separate fresh `TransportType` values:
- one for `ConnectionCreator::prepare_connection(...)`, which leads to `TlsInit`;
- another for `RawConnection::create(...)`, which leads to `create_transport(...)`.

Because both `TransportType` objects have empty `selected_profile`, `TlsInit` and `create_transport` fall back to independent selection again on this path. That recreates the same class of divergence H1 was meant to remove, just outside the three `ConnectionCreator` call sites.

This is a production-relevant path: proxy validation is part of the shipped behavior, and its traffic will not obey the PR's "one attempt uses one selected profile" contract if rotation is ever enabled.

### 4. Low — `TransportType.h` violates the repository's license-header rule for modified Boost-derived files

Files:
- `td/mtproto/TransportType.h:1-6`

The file was modified in this PR, but it still carries the old Boost-only header instead of the required dual SPDX header described in `AGENTS.md`. That is not a functional bug, but it is a direct repo-policy violation.

## Notes

- The PR adds a large targeted test surface, but it does not cover the two highest-risk seams above:
  - no integration test where the ECH decision changes between connection setup and `TlsInit::send_hello()`;
  - no test covering the `ProxyChecker` path;
  - no test asserting that wire-identical aliases share one quarantine outcome.

- The review plan in `docs/Plans/ADAPTIVE_RUNTIME_PROFILE_ROTATION_PLAN_2026-06-12.md` also called for integration tests around `TlsInit`/config coherence. I did not find those specific end-to-end tests in the PR; the added `test_runtime_profile_rotation_handoff.cpp` only proves the explicit-profile helper path, not the full actor/lifecycle flow.

## Bottom line

Evidence leans toward "not merge-ready for future rotation enablement" until the handoff carries a full wire-variant snapshot, the iOS aliasing issue is resolved or explicitly collapsed at quarantine time, and the `ProxyChecker` path is brought under the same single-selection contract.
