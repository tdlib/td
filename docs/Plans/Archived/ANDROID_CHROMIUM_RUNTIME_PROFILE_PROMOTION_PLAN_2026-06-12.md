<!-- SPDX-FileCopyrightText: Copyright 2026 telemt community -->
<!-- SPDX-License-Identifier: MIT -->
<!-- telemt: https://github.com/telemt -->
<!-- telemt: https://t.me/telemtrs -->

# Android Chromium Runtime Profile Promotion Plan (2026-06-12)

## Objective

Promote the reviewed ALPS-bearing `android_chromium` family into the first real Android runtime profile:

- add `BrowserProfile::AndroidChromium_Alps`;
- keep `Android11_OkHttp_Advisory` as the conservative fallback;
- keep Android default `transport_confidence=Unknown`;
- make the new Android lane reachable only when runtime confidence is `Partial` or `Strong`;
- make the new Android lane `release_gating=true`.

## Scope

This work is intentionally narrow:

- only the ALPS-bearing reviewed Android Chromium family is promoted;
- no second Android runtime profile is added for the no-ALPS family;
- advisory OkHttp remains in place and non-zero for fail-closed unknown-confidence Android;
- release mode does not bypass transport-confidence rules.

## Implementation

1. Add a new browser/runtime profile:
   - `BrowserProfile::AndroidChromium_Alps`
   - wire shape modeled on the already-reviewed ALPS-bearing Chromium family currently proxied by `Chrome133`

2. Register the new profile in runtime metadata:
   - include it in `ALL_PROFILES`, `MOBILE_PROFILES`, and `ANDROID_MOBILE_PROFILES`
   - add a dedicated `ProfileSpec`
   - add `ProfileFixtureMetadata`:
     - `source_kind = BrowserCapture`
     - `trust_tier = Verified`
     - `has_independent_network_provenance = true`
     - `has_utls_snapshot_corroboration = false`
     - `release_gating = true`
     - `transport_claim_level = CrossLayerStrong`

3. Add a dedicated weight slot:
   - `android_chromium_alps`
   - update runtime selection and validation switches to understand it

4. Keep the legacy plan-style mobile schema backward-compatible:
   - continue accepting only `IOS14` and `Android11_OkHttp_Advisory` in `profile_weights.mobile`
   - flatten Android legacy share into:
     - verified `android_chromium_alps`
     - advisory `android11_okhttp_advisory`
   - current default split:
     - `30 -> 20 + 10`

5. Keep runtime fail-closed semantics:
   - `Unknown` confidence may still use only `TlsOnly` Android fallback
   - release mode validation must count only confidence-eligible release lanes

## Test Coverage

Required assertions for this promotion:

- Android at `Partial`/`Strong` can reach `AndroidChromium_Alps`
- Android at `Unknown` still resolves only to `Android11_OkHttp_Advisory`
- Android release mode succeeds only when confidence allows the verified lane
- plan-style mobile config still loads and bridges the Android share into both runtime lanes
- flat config accepts explicit `android_chromium_alps`
- runtime/profile alignment pins `AndroidChromium_Alps` to the reviewed `android_chromium / non_ru_egress` baseline
- desktop and iOS allowed-profile sets never expose the new Android lane
