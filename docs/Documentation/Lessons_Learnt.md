<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# ECH Wire Entropy — Lessons Learnt (2026-04-22)

**Context:** Adversarial TDD session covering stealth masking subsystem tests.  
**Trigger:** Tests written assuming Firefox and Safari produce variable-length wires — they do not.  
**Resolution:** Tests corrected against real Wireshark fixture data (`test/analysis/fixtures/clienthello/`).

---

## 1. What the Real Captures Reveal

The following table is derived directly from `test/analysis/fixtures/clienthello/` fixture files.
Every claim here can be verified by reading the JSON samples.

| Profile family | Platform | ECH present | ECH payload lengths | Padding op | Wire entropy |
|---|---|---|---|---|---|
| Chrome 133/131/120/146/147 | Linux, Windows | ✅ Yes | `{144, 176, 208, 240}` (sampled) | ✅ Yes | **High** — ECH length + padding both vary |
| Chrome 147 | macOS | ✅ Yes | `{176, 208, 240}` (sampled) | ✅ Yes | **High** |
| Chrome 146/147 | Android | ✅ Yes | `{144, 176, 208, 240}` (sampled) | ✅ Yes | **High** |
| Chrome 147 iOS Chromium | iOS 26.3 | ✅ Yes (`{144, 240}` in early builds) | varies | ✅ Yes (if Chromium layout) | Medium |
| Chrome 147 iOS Chromium | iOS 26.4 (a, b) | ❌ No ECH | — | ✅ Yes | **Low** — only padding varies |
| Firefox 148 | Linux desktop | ✅ Yes | `{239}` **fixed** | ❌ None | **Zero** — deterministic wire |
| Firefox 149 | Linux, macOS, Windows, Android | ✅ Yes | `{239, 399}` two states | ❌ None | **Binary** — exactly two wire lengths |
| Safari 26.x | iOS 26.1–26.5 | ❌ Never | — | ❌ None | **Zero** — deterministic wire |
| Safari 18.x | iOS 18.7 | ❌ Never | — | ❌ None | **Zero** |
| iOS 14 (Apple TLS) | iOS 26.x | ❌ Never | — | ❌ None | **Zero** |
| Brave 188 | iOS 26.4 | ❌ No ECH | — | unknown | Low |
| Samsung Internet 29 | Android 16 | ✅ Yes | `{208}` fixed | unknown | Low |

**Key fixture references:**
- `linux_desktop/firefox148_linux_desktop.clienthello.json` — confirms `ech_lengths={239}` single value
- `linux_desktop/firefox149_0_2_linux6_19_6_edc237c0.clienthello.json` — confirms `ech_lengths={399, 239}`
- `ios/safari26_4_ios26_4_a.clienthello.json` — confirms `ech=null` on all iOS Safari
- `ios/chrome147_0_7727_47_ios26_4_a.clienthello.json` — confirms `ech=null` on Chrome/iOS 26.4
- `android/chrome146_0_7680_177_android14_ada4e248.clienthello.json` — confirms `ech_lengths={240, 176, 208}`

---

## 2. How the Implementation Encodes This

### 2.1 ECH payload length resolution (`TlsHelloBuilder.cpp`)

```cpp
int resolve_ech_payload_length(const ProfileSpec &spec, bool enable_ech, IRng &rng) {
  if (!enable_ech) {
    return 144 + static_cast<int>(rng.bounded(4u) * 32u);  // "dark" entropy for disabled path
  }
  if (spec.ech_payload_length != 0) {
    return spec.ech_payload_length;   // Firefox: returns 239 or 399 — fixed by spec
  }
  return 144 + static_cast<int>(rng.bounded(4u) * 32u);    // Chrome: sampled {144,176,208,240}
}
```

`ProfileSpec::ech_payload_length` in `TlsHelloProfileRegistry.cpp`:
- Chrome 133/131/120/147 → `0` → sampled → `{144, 176, 208, 240}`
- Firefox 148 → `239` → fixed
- Firefox 149 Windows/Linux → `239` → fixed
- Firefox 149 macOS 26.3 → `399` → fixed
- Chrome 147 iOS Chromium → `144` → fixed (real captures show mostly 144 on that profile)

### 2.2 Padding entropy (`TlsHelloBuilder.cpp`)

```cpp
config.padding_target_entropy = static_cast<int>(rng.bounded(256u));
```

This value is consumed by `ClientHelloExecutor` via the `Op::padding_to_target(N)` operation.
**Only Chrome-family profiles include `Op::padding_to_target` in their layout.**

Firefox layout (`make_firefox_layout`) — no padding op → `padding_target_entropy` is ignored.  
iOS layout (`make_ios_layout`) — no padding op → same.  
Safari layout (shares iOS path) — no padding op → same.

### 2.3 Consequence for wire diversity

| Condition | Wire size behaviour |
|---|---|
| Chrome + ECH enabled (non-RU) | Varies: 4 ECH payload choices × 256 padding targets = 1024 distinct lengths |
| Chrome + ECH disabled (RU) | Varies: padding only = 256 distinct lengths |
| Firefox 148 + ECH enabled | Fixed: 1 distinct length per build |
| Firefox 149 macOS + ECH enabled | Fixed: 1 distinct length (399) |
| Firefox 149 (others) + ECH enabled | Fixed: 1 distinct length (239) |
| Firefox any + ECH disabled (RU) | Fixed: 1 distinct length — zero variation |
| Safari / iOS all variants | Fixed: 1 distinct length — zero variation |

---

## 3. Why Fixed-Length Firefox and Safari Are Still Correct

This was the key insight corrected by real captures:

**A fixed wire length is not a security problem as long as the wire is indistinguishable from real browser traffic.**

DPI classifies traffic in two steps:
1. **Anomaly detection** — does this wire look different from any known browser?
2. **Protocol identification** — which known protocol is this?

For Firefox, the fixed `239`-byte ECH payload is what real Firefox 148 produces.  
A DPI device would see our wire and say "this looks like Firefox 148" — which is the correct conclusion.  
It cannot distinguish us from a real Firefox user on the same network.

The threat would arise only if:
- We produced a wire that _no_ real browser produces (wrong length, wrong extension order), OR
- We produced a wire with a fixed fingerprint that a DPI device could use to separate us from real Firefox users (e.g., identical random bytes, fixed GREASE values, fixed key share bytes) — which GREASE randomization prevents.

**Conclusion:** Fixed wire length for Firefox and Safari is acceptable and expected behaviour.

---

## 4. RU-Route Specific Considerations

ECH is explicitly disabled on RU-egress routes (see `ech_mode_for_route()` and `MaskingEchCbTemporalAdversarial` tests).

Consequences per profile when on RU route:

| Profile | ECH disabled effect | Wire behaviour |
|---|---|---|
| Chrome 133/131/120/147 | `padding_to_target` still active → 256 distinct lengths | **Acceptable** — real Chrome also disables ECH on blocked routes |
| Firefox 148 | No padding, ECH disabled → 1 length | **Zero variation** — identical to real Firefox with ECH blocked |
| Firefox 149 | Same as Firefox 148 on RU | **Zero variation** |
| Safari / iOS | No change — ECH was never present | **Zero variation** — matches real Safari exactly |

The implication: **on RU routes, Chrome profiles are significantly stronger than Firefox/Safari from an entropy standpoint.** The profile selection weights should favour Chrome over Firefox/Safari in RU-egress mode when available platforms allow it.

Current weights (`default_profile_weights`):
```
chrome133: 50, chrome131: 20, chrome120: 15
firefox148: 15, safari26_3: 20
```
Firefox and Safari have non-zero weights even on RU routes. This is a design tradeoff:
maintaining realistic profile distribution vs maximising entropy. This is intentional — a
network that only sees Chrome fingerprints is itself anomalous.

---

## 5. Circuit Breaker Temporal Behaviour

The ECH circuit breaker keys failures by `(destination, day_bucket)` where `day_bucket = unix_time / 86400`.

This means:
- If ECH fails at 23:59 UTC, it stays blocked until 00:00 UTC next day (less than 1 minute).
- If ECH fails at 00:01 UTC, it stays blocked until 00:00 UTC the *following* day (~24 hours).

**Worst case lockout**: ~24 hours for a single destination.

The `reset_runtime_ech_failure_state_for_tests()` function exists to clear this state in tests.
In production, state is persisted in `KeyValueSyncInterface` (see `set_runtime_ech_failure_store()`).

**Known limitation documented in tests:**
`MaskingEchCbTemporalAdversarial_CircuitBreakerStateNotCarriedAcrossDayBuckets` verifies that
a failure in day N does not affect day N+1. This is by design — the day-bucket TTL acts as an
automatic expiry without needing an explicit timer.

---

## 6. Actionable Recommendations

### 6.1 Completed (as of this session)
- [x] Tests now assert Firefox/Safari fixed-length wires — documents the design contract
- [x] Chrome ECH payload variation `{144,176,208,240}` is tested across seeds
- [x] RU-route ECH suppression tested for all profile types
- [x] Circuit breaker temporal isolation tests added

### 6.2 Future work
- [ ] Consider adding Firefox 149 dual-state test (`ech_lengths ∈ {239, 399}` — exactly 2 distinct lengths)
- [ ] Profile selection weights on RU routes: evaluate whether Firefox/Safari weight should decrease further to improve entropy budget
- [ ] Monitor RU DPI for Firefox fixed-fingerprint detection — if blocked, reduce Firefox weight on RU
- [ ] Track Chrome iOS 26.4 ECH status — fixtures show ECH absent; if Chrome re-enables ECH on iOS, `Chrome147_IOSChromium` profile spec needs updating
- [ ] Corpus test for `Firefox149_MacOS26_3` (399-byte ECH) — only 1 fixture source currently (Tier 1)

---

## 7. Code Locations

| Concern | File |
|---|---|
| ECH payload length logic | `td/mtproto/stealth/TlsHelloBuilder.cpp` → `resolve_ech_payload_length()` |
| Padding entropy injection | `td/mtproto/stealth/TlsHelloBuilder.cpp` → `make_config()` |
| Profile specs (ECH payload, allows_ech, allows_padding) | `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` → `PROFILE_SPECS[]` |
| Padding op placement | `td/mtproto/BrowserProfile.cpp` → `make_chrome_layout()`, `make_firefox_layout()`, `make_ios_layout()` |
| Circuit breaker / temporal state | `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` → `note_runtime_ech_failure()` |
| Route-level ECH decisions | `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` → `ech_mode_for_route()` |
| Adversarial tests (this session) | `test/stealth/test_masking_padding_entropy_adversarial.cpp` |
| | `test/stealth/test_masking_ech_cb_temporal_adversarial.cpp` |
| | `test/stealth/test_masking_proxy_alpn_all_profiles_adversarial.cpp` |
| | `test/stealth/test_masking_traffic_classifier_contract_adversarial.cpp` |
| | `test/stealth/test_masking_ipt_controller_adversarial.cpp` |
| | `test/stealth/test_masking_profile_platform_isolation_adversarial.cpp` |

---

# TLS Extension Order Semantics — Lessons Learnt (2026-04-22)

**Context:** Follow-up TDD session on stealth ClientHello extension-order coverage.  
**Trigger:** A new regression test was initially written as exact reviewed-template matching for Chromium/Linux. That assumption was challenged against the checked-in fixture corpus under `test/analysis/fixtures/clienthello/`.  
**Resolution:** Production kept truthful `ChromeShuffleAnchored` semantics; the new tests were rewritten to derive legality from fixture-backed policy and upstream rule documents instead of hardcoded reviewed order catalogs.

---

## 1. What the Corpus and Analysis Layer Actually Say

The decisive evidence was not a single reviewed baseline header entry, but the combination of the fixture corpus, the analysis README, and the upstream-rule mirror.

| Source | What it says | Practical implication |
|---|---|---|
| `test/analysis/README.md` | Imported Safari profiles stay `FixedFromFixture`; all other imported browser families use `ChromeShuffleAnchored` | Chromium-family order must be treated as browser-like shuffle, not a single frozen template |
| `test/analysis/check_fingerprint.py` | `ChromeShuffleAnchored` passes when `Counter(observed_order) == Counter(expected_order)` | The analysis contract is multiset legality, not exact order equality |
| `test/analysis/upstream_tls_rules.json` | Chromium extension order uses `shuffle_all_except_pre_shared_key` with GREASE/padding anchors | Runtime should preserve anchored shuffle semantics |
| `docs/Samples/utls-code/u_parrots.go` | `ShuffleChromeTLSExtensions` shuffles every extension except GREASE, padding, and `pre_shared_key` | The bundled upstream reference matches the analysis contract |
| `test/stealth/ReviewedFamilyLaneBaselines.h` | `chromium_linux_desktop` carries many reviewed extension-order templates | Exact reviewed-template pinning for Chromium is stale by construction |

Two concrete takeaways from the reviewed baseline tables:

1. `chromium_linux_desktop` is explicitly multi-template. Any production logic that tries to restrict Chromium to a curated list of reviewed exact templates stops imitating Chromium and starts imitating a stale subset of captures.
2. Even nominally fixed-order families can have more than one reviewed template in the lane table. For example, `firefox_linux_desktop` has multiple reviewed templates because the corpus contains structurally different but still legitimate samples. “Fixed-order builder” does not mean “the reviewed corpus contains only one possible template”.

---

## 2. The Wrong Assumption

The failed assumption was:

> If the reviewed baseline table contains exact extension-order templates, production Chromium should only emit one of those reviewed exact templates.

That is too strong and wrong for this codebase.

Why it fails:

- reviewed baselines are a conservative evidence snapshot, not a complete generative specification;
- Chromium-family order is intentionally high-cardinality under `ChromeShuffleAnchored`;
- the analysis layer already encodes the intended semantics as policy, and that policy is weaker than exact-template equality by design.

The result of following the wrong assumption was predictable: it pushed the runtime toward hardcoded catalog behaviour that reduced truthfulness instead of improving it.

---

## 3. The Correct Contract Per Family

| Family | Runtime contract | Test contract |
|---|---|---|
| Chromium-family (`Chrome133`, `Chrome131`, etc.) | Preserve truthful anchored shuffle: GREASE/padding/PSK anchors stay special; the remaining extension block is order-variable | Assert legal `ChromeShuffleAnchored` permutation, correct extension set, correct ALPS type per profile, and seed-driven order diversity |
| Firefox fixed profiles | Keep the emitted order seed-stable for the profile spec | Assert the produced order is stable across seeds and matches at least one reviewed template for that lane |
| Apple TLS / Safari / iOS fixed profiles | Keep emitted order seed-stable and fixture-faithful | Assert seed stability and membership in the reviewed fixed-template set |

The crucial distinction is:

- **Chromium tests** should prove *policy compliance plus variability*.
- **Fixed-profile tests** should prove *stability plus reviewed-template compatibility*.

They should not be collapsed into one “every family must match an exact reviewed template catalog” rule.

---

## 4. What Changed in This Session

This session ended up being primarily a **test correction**, not a runtime redesign.

### 4.1 Production-side outcome

- The attempted exact-template catalog plumbing for Chromium was removed before completion.
- The runtime remains on the truthful anchored-shuffle path already modeled by the builder/executor and by the upstream/uTLS references.

### 4.2 Test-side outcome

The new coverage in `test/stealth/test_tls_extension_order_template_catalog_contract.cpp` now does the following:

- verifies that reviewed Chromium/Linux corpus data is genuinely order-variable;
- verifies that generated `Chrome133` and `Chrome131` obey anchored-shuffle legality rather than exact-template equality;
- verifies that Chromium extension content still matches fixture-derived extension sets and ALPS-type expectations;
- verifies that fixed-order families (`Firefox148`, `Safari26_3`, `IOS14`) stay seed-stable and match one reviewed fixed template for their lane.

This keeps the test suite aligned with the corpus without teaching the runtime to overfit reviewed snapshots.

---

## 5. Validation Pattern to Reuse

When extension-order work touches stealth fingerprints again, the safe validation order is:

1. Check `test/analysis/fixtures/clienthello/` first.
2. Check `test/analysis/README.md` and `test/analysis/check_fingerprint.py` to see the policy the corpus tooling already enforces.
3. Cross-check upstream semantics in `test/analysis/upstream_tls_rules.json` and `docs/Samples/utls-code/u_parrots.go`.
4. Only then decide whether the change belongs in runtime generation or only in tests.

If steps 1-3 already define a weaker policy than “exact reviewed template equality”, production should not be tightened beyond that policy without new evidence.

---

## 6. Focused Validation Results

The final focused checks for this session were:

```bash
./build/test/run_all_tests --filter TlsExtensionOrder
./build/test/run_all_tests --filter ChromeCorpusExtensionSet1k
./build/test/run_all_tests --filter TlsExtensionOrderTemplateCatalogContract
```

All three passed after the test logic was corrected.

---

## 7. Code and Evidence References

| Concern | File |
|---|---|
| Imported-corpus policy description | `test/analysis/README.md` |
| Corpus policy implementation | `test/analysis/check_fingerprint.py` |
| Upstream Chromium legality mirror | `test/analysis/upstream_tls_rules.json` |
| Upstream uTLS shuffle reference | `docs/Samples/utls-code/u_parrots.go` |
| Reviewed lane baseline tables | `test/stealth/ReviewedFamilyLaneBaselines.h` |
| Existing Chromium shuffle regression tests | `test/stealth/test_tls_extension_order_policy.cpp` |
| New contract tests from this session | `test/stealth/test_tls_extension_order_template_catalog_contract.cpp` |
| Fixture-derived Chrome extension-set coverage | `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp` |

---

## Real-Corpus Similarity Evidence

Self-calibrated generator tests are not real-browser similarity evidence. Release-facing fingerprint claims must use reviewed fixture evidence from real packet captures, disclose the cohort denominator, and fail closed when exact release-critical fields are unavailable or mixed. Seed-stress diagnostics remain useful, but they prove generator diversity and stability rather than similarity to browser dumps.

A practical corollary learned while wiring the fixture-derived wire-length gate: a byte-exact wire-length equality check is the wrong gate, because `TlsHelloBuilder` injects 0..255 bytes of per-build padding-target entropy as an anti-DPI measure. The release gate must bound the generated length to the reviewed catalog with a tolerance derived from that documented entropy budget, not assert a single byte length.
