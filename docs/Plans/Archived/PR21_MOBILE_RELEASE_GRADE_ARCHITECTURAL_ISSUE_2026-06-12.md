<!-- SPDX-FileCopyrightText: Copyright 2026 telemt community -->
<!-- SPDX-License-Identifier: MIT -->
<!-- telemt: https://github.com/telemt -->
<!-- telemt: https://t.me/telemtrs -->

# PR #21 Mobile Release-Grade Architectural Issue (2026-06-12)

Review target: `origin/stealth-corpus-real-dump-similarity` after the follow-up runtime fixes that landed on top of PR #21.

## Bottom line

The remaining mobile problem is narrower than the pre-fix state, but it is still
not a small code defect. The current runtime policy now provides a real Android
release-grade lane once `transport_confidence` is established, yet it still does
not provide an iOS/default-Unknown path that is both confidence-allowed and
release-gated.

The live boundary is:

1. mobile runtime selection still depends on `transport_confidence`;
2. release-mode runtime selection still depends on `release_gating`;
3. Android now satisfies that combination at established confidence, but iOS
   still does not satisfy it under the default Unknown-confidence posture.

So the remaining open issue is no longer "mobile has no release-grade lane at
all"; it is that mobile does not have a fully closed default-policy posture
across both platforms.

## Source-of-truth evidence

### 1. Runtime confidence gate

At `TransportConfidence::Unknown`, runtime selection only allows `TlsOnly` profiles:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:945-946`

That is the reason the current iOS Chromium lane is not eligible under unknown confidence:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:138-140`
- `BrowserProfile::Chrome147_IOSChromium` is marked `TransportClaimLevel::CrossLayerStrong`

By contrast, the current advisory iOS/Android lanes are `TlsOnly`:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:147-151`

### 2. Runtime release gate

When `release_mode_profile_gating=true`, runtime requires at least one `release_gating` profile for the current platform:

- `td/mtproto/stealth/StealthRuntimeParams.cpp:297-314`

The selector also suppresses advisory/non-release choices in release mode:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1222-1259`

### 3. Current mobile metadata shape

Current mobile metadata now splits into two different cases:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:138-151`

Specifically:

- `Chrome147_IOSChromium`:
  - has browser-capture provenance,
  - has `TransportClaimLevel::CrossLayerStrong`,
  - but `release_gating=false`.

- `IOS14`:
  - is advisory / `UtlsSnapshot`,
  - is `TlsOnly`,
  - but `release_gating=false`.

- `AndroidChromium_Alps`:
  - has browser-capture provenance,
  - has `release_gating=true`,
  - has `TransportClaimLevel::CrossLayerStrong`,
  - so it is a real Android release-grade lane once confidence is `Partial` or
    `Strong`.

- `Android11_OkHttp_Advisory`:
  - is advisory / `UtlsSnapshot`,
  - is `TlsOnly`,
  - but `release_gating=false`.

- `Firefox149_Android`:
  - has browser-capture provenance,
  - has `TransportClaimLevel::CrossLayerStrong`,
  - but `release_gating=false`.

### 4. Current tests already expose the boundary

The runtime policy tests now pin this behavior:

- `test/stealth/test_tls_mobile_release_grade_lane.cpp`
  - iOS Chromium is reachable only once confidence is established
  - iOS defaults to advisory lane at `Unknown`
  - Android Chromium is reachable once confidence is established
  - Android defaults to advisory lane at `Unknown`

- `test/stealth/test_tls_runtime_profile_policy_fail_closed.cpp`
  - release mode is rejected for current iOS curation
  - release mode is allowed for verified Android curation at established confidence
  - release mode is rejected for Android at `Unknown`

## Why this is architectural

This is not just "some weight is zero" or "a branch is wrong".

The problem is that three independent concepts are modeled separately:

1. **profile provenance / trust tier**
2. **transport-claim strength**
3. **release-gating eligibility**

That separation is correct in principle, but it exposes a real system-level requirement:

> a platform can only be release-grade if it has at least one profile whose evidence and claim level jointly satisfy the runtime confidence policy and the release-gating policy.

Today Android satisfies that requirement once confidence is established; iOS still
does not under the default Unknown-confidence posture.

So any attempt to "fix mobile release-grade in code" by only changing selection weights or defaults would do one of two bad things:

1. silently promote advisory evidence into release mode; or
2. silently weaken the transport-confidence gate and allow a cross-layer claim without the confidence evidence it was designed to require.

Both would be logically wrong.

## Why I did not "fix" it in code

There are only a few possible code-only moves:

1. Mark `Chrome147_IOSChromium` as `release_gating=true`
2. Change `Chrome147_IOSChromium` from `CrossLayerStrong` to `TlsOnly`
3. Mark `IOS14` or `Android11_OkHttp_Advisory` as release-gating
4. Disable the release-mode or transport-confidence fail-closed behavior for mobile

I do not consider any of those acceptable without new evidence and an explicit policy decision:

- (1) changes release semantics, not implementation mechanics
- (2) weakens the meaning of the transport-claim model
- (3) re-labels advisory evidence as release-grade
- (4) makes the runtime more permissive exactly where it should remain fail-closed

So the honest state is: the original broad mobile blocker has been materially
narrowed, but the remaining iOS/default-policy issue is still open for
architectural reasons, not because the branch forgot a small runtime tweak.

## What would actually close it

One of these must happen:

1. **iOS path**
   - curate a real mobile profile as `release_gating=true` with evidence sufficient for that label; and
   - decide whether its transport claim should remain `CrossLayerStrong` or be narrowed based on the actual reviewed evidence.

2. **Android path**
   - largely implemented on this staged head for the ALPS-bearing Chromium lane;
   - remaining Android work, if any, is incremental breadth/provenance expansion,
     not the original "Android is advisory-only" gap.

3. **Policy redesign**
   - redesign the relationship between `transport_confidence`, `TransportClaimLevel`, and `release_gating` for mobile;
   - but that is an explicit architecture change, not a bugfix.

## Recommended wording for PR review

Suggested reviewer-facing summary:

> The PR materially improves mobile posture and now adds a real Android
> release-gated lane at established confidence, but it still does not close the
> iOS/default-Unknown mobile release-grade boundary. This is not merely a missing
> weight tweak. Under the current runtime model, iOS still lacks a profile that is
> simultaneously confidence-allowed at Unknown and release-gated. Closing that
> remaining gap requires new evidence and/or an explicit policy redesign, not a
> permissive runtime shortcut.
