<!-- SPDX-FileCopyrightText: Copyright 2026 telemt community -->
<!-- SPDX-License-Identifier: MIT -->
<!-- telemt: https://github.com/telemt -->
<!-- telemt: https://t.me/telemtrs -->

# Runtime Profile Gap Audit (2026-06-12)

Scope: audit for the same class of issue as the former Android Chromium gap:

> a reviewed browser-capture family has substantial real corpus backing, but the runtime either has no dedicated `BrowserProfile` for it or still proxies it through another family instead of selecting it directly.

This is an independent audit. I did not assume that every family with many dumps must automatically become a runtime profile. I checked whether the family is:

1. present in the reviewed corpus / family baselines;
2. represented in `BrowserProfile` and `TlsHelloProfileRegistry`;
3. reachable from runtime platform selection;
4. or still being proxied / ignored despite corpus maturity.

## Source-of-truth census

Reviewed fixture census from `test/analysis/profiles_validation.json`:

| Family | Reviewed fixtures | Independent sources | Current runtime identity |
|---|---:|---:|---|
| `chromium_windows` | 130 | 22 | dedicated `Chrome147_Windows` |
| `android_chromium` | 69 | 19 | dedicated `AndroidChromium_Alps` after current change |
| `firefox_android` | 59 | 10 | **no runtime identity** |
| `firefox_windows` | 52 | 9 | dedicated `Firefox149_Windows` |
| `apple_ios_tls` | 42 | 16 | runtime `IOS14` exists |
| `firefox_macos` | 28 | 6 | dedicated `Firefox149_MacOS26_3` |
| `chromium_macos` | 21 | 5 | **no runtime identity** |
| `firefox_linux_desktop` | 20 | 5 | runtime `Firefox148` exists |
| `chromium_linux_desktop` | 16 | 6 | runtime `Chrome133` / `Chrome131` / `Chrome120` exist |
| `apple_macos_tls` | 16 | 3 | runtime `Safari26_3` exists |
| `ios_chromium` | 5 | 5 | dedicated `Chrome147_IOSChromium` |

Bottom line: after the Android Chromium promotion, I see **2 remaining same-class gaps**:

1. `firefox_android`
2. `chromium_macos`

I do **not** count `apple_ios_tls` or `apple_macos_tls` as the same issue, because runtime identities already exist there. Their remaining problem is evidence/gating quality, not identity absence.

## Finding 1: `firefox_android` is reviewed but still has no runtime profile

Confidence: ~95%

### Why this is the same issue

The family is real and reviewed:

- `WireClassifierFeatures.cpp` classifies Android Firefox as `firefox_android`.
- `ReviewedFamilyLaneBaselines.h` contains a full `firefox_android` family-lane baseline.
- The reviewed corpus behind it is not small: 59 reviewed fixtures from 10 independent sources.

But the runtime has no direct representation for it:

- `td/mtproto/BrowserProfile.h` has no `FirefoxAndroid`-style enum entry.
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` has no Android Firefox `ProfileSpec`, no `ProfileFixtureMetadata`, and no Android weight slot for it.
- Android runtime selection currently exposes only:
  - `AndroidChromium_Alps`
  - `Android11_OkHttp_Advisory`

So even though the corpus explicitly knows this family, runtime can never intentionally select it.

### Strong evidence

- `test/stealth/ReviewedFamilyLaneBaselines.h`
  - `family_id=firefox_android` exists.
  - Non-RU reviewed lane exists with real extension-order templates, supported groups, supported versions, ECH payload lengths, wire lengths, and a 59-sample extension-count histogram.
- `test/stealth/test_tls_multi_dump_android_chromium_no_alps_baseline.cpp`
  - the comment explicitly acknowledges that `firefox_android` is a reviewed Android lane that exists separately from `android_chromium`.
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
  - `ANDROID_MOBILE_PROFILES` does not include any Firefox-Android family.
- `test/stealth/test_tls_runtime_real_fixture_alignment.cpp`
  - there is now runtime alignment for `android_chromium`, `ios_chromium`, Windows, Firefox macOS, etc.
  - there is still no runtime family mapping for `firefox_android`.

### Why this matters

This is not just “nice to have more variety.”

If Android real traffic in the corpus already contains both Chromium-derived and Firefox-derived families, but runtime selection can only emit Chromium-derived or advisory-no-ALPS Android, then the reviewed family inventory and runtime family inventory are still materially out of sync.

That creates the same architectural weakness we just closed for Android Chromium:

- corpus evidence says a family exists;
- tests and classifiers know it exists;
- runtime cannot use it.

### Recommended remediation

Create a dedicated Android Firefox runtime profile:

1. add `BrowserProfile::FirefoxAndroid` (name bikeshedding optional, but keep it explicit);
2. add a dedicated `BrowserProfileSpec` modeled on the reviewed `firefox_android` family, not on Linux Firefox by assumption;
3. register it in `TlsHelloProfileRegistry` with dedicated fixture metadata and Android-only allow-list placement;
4. add a distinct weight slot;
5. keep Android `TransportConfidence::Unknown` fail-closed unless this lane is intentionally modeled as `TlsOnly`;
6. add runtime alignment tests from the new profile to the reviewed `firefox_android / non_ru_egress` baseline.

## Finding 2: `chromium_macos` still exists only as a reviewed family, not as a runtime identity

Confidence: ~85%

### Why this is the same issue

The family is real and non-trivial:

- `WireClassifierFeatures.cpp` classifies macOS Chromium captures as `chromium_macos`.
- `ReviewedFamilyLaneBaselines.h` contains a dedicated `chromium_macos` baseline.
- Corpus size is not tiny: 21 reviewed fixtures from 5 independent sources.

But the runtime still does not expose a dedicated macOS Chromium profile:

- `BrowserProfile.h` has no macOS Chromium enum.
- `TlsHelloProfileRegistry.cpp` has no dedicated `chromium_macos` profile in `DARWIN_DESKTOP_PROFILES`.
- The direct evidence I found for actual usage is still proxy-based:
  - `test/stealth/test_tls_fingerprint_classifier_blackhat.cpp`
  - `LOOCVExtOrderChromiumMacosNotGrosslyLeaking`
  - runs `chromium_macos` against `BrowserProfile::Chrome133`

That is exactly the pattern we just removed for Android Chromium:

- reviewed family exists;
- generator/corpus comparison proxies another profile against it;
- runtime has no dedicated lane.

### Important nuance

This one is **not** as clean to promote as Android Chromium was.

The reviewed `chromium_macos` baseline is not a single neat shape:

- `ReviewedFamilyLaneBaselines.h` shows `observed_alps_types = {0x4469, 0x44CD}` for `chromium_macos / non_ru_egress`.

That means the current reviewed macOS Chromium family is already mixing at least two ALPS cohorts. So while this is a real runtime-profile gap, the correct fix is probably **not** “just add one `ChromeMacOS` profile and point it at the mixed family.”

### Why this matters

Today Darwin desktop allows:

- generic Chromium profiles (`Chrome133`, `Chrome131`, `Chrome120`)
- Safari
- Firefox macOS

But it does **not** allow a dedicated Chromium-macOS identity, even though the reviewed corpus says that family exists separately and in meaningful volume.

So the runtime can never intentionally emit a macOS Chromium lane as such; at best it approximates it through Linux Chromium profiles.

### Recommended remediation

Do not promote `chromium_macos` as a single runtime lane yet.

First split or normalize the family:

1. separate the macOS Chromium corpus into coherent ALPS cohorts (`0x4469` vs `0x44CD`) or otherwise prove one can safely represent the other;
2. then add one or more dedicated macOS Chromium runtime profiles;
3. add Darwin-only allow-list entries and runtime alignment tests;
4. remove proxy use of `BrowserProfile::Chrome133` for `chromium_macos` classifier/baseline gating once the dedicated lane exists.

## Non-findings: big corpora that already do have runtime identities

These are not the same bug class.

### `chromium_windows`

Not a gap anymore.

- 130 reviewed fixtures / 22 sources.
- Dedicated runtime identity exists: `Chrome147_Windows`.
- Windows-specific baseline suites and runtime placement exist.

### `firefox_windows`

Not a gap anymore.

- 52 reviewed fixtures / 9 sources.
- Dedicated runtime identity exists: `Firefox149_Windows`.

### `firefox_macos`

Not a gap anymore.

- 28 reviewed fixtures / 6 sources.
- Dedicated runtime identity exists: `Firefox149_MacOS26_3`.

### `apple_ios_tls` and `apple_macos_tls`

These still have open evidence/gating questions, but not this identity-absence bug.

- `apple_ios_tls` runtime identity exists as `IOS14`.
- `apple_macos_tls` runtime identity exists as `Safari26_3`.

Their problem is that the runtime identities are still advisory / conservative, not that they are missing.

## Priority order

If the goal is to close the next real runtime-profile omission with the highest ROI, my order would be:

1. `firefox_android`
   - strongest remaining same-class gap
   - large reviewed corpus
   - no runtime identity at all
   - cleanest “promote family into runtime” target after Android Chromium

2. `chromium_macos`
   - real gap, but promotion is blocked by mixed ALPS cohorts
   - needs corpus/cohort cleanup first, then runtime work

## Final judgment

After the current Android Chromium promotion, I do **not** see a broad repo-wide pattern where many other mature reviewed families were simply forgotten.

I see **two** real remaining same-class issues:

- `firefox_android`: definite missing runtime profile
- `chromium_macos`: reviewed family still proxying through generic Chrome, but direct promotion is not logically clean yet

Everything else I checked with large reviewed corpora already has some runtime identity and therefore falls into a different problem class.
