# Plan: Fixture-Driven 1000+ Iteration Fingerprint Corpus Tests

**TL;DR**: Create 12 new C++ test files (≥1024-iteration statistical corpus tests) that exercise each fingerprint generator against real browser captures from fixtures and `docs/Samples/Traffic dumps/`, checking both structural fidelity and statistical distributions. Every test is designed to expose real bugs — not to confirm correct behavior.

---

### What already exists (do NOT duplicate)

| File | N | What it tests |
|---|---|---|
| test_tls_capture_chrome144_differential.cpp | 10–20 | Chrome structural invariants |
| test_tls_capture_firefox148_differential.cpp | 10–20 | Firefox cipher/extension structure |
| `test_tls_capture_safari26_3_differential.cpp` | 10–20 | Safari structural invariants |
| test_tls_total_length_distribution_adversarial.cpp | 100–200 | Total wire size not fixed 517 |
| test_tls_extension_order_entropy_adversarial.cpp | 500 | Chrome shuffle produces many distinct orderings |

**Gap**: nothing runs N≥1024, no statistical uniformity tests, no Android/iOS corpus, no ALPS type stability at scale, no JA3/JA4 bulk hash stability, no GREASE slot independence.

---

### Platform-OS Corpus Matrix

The checked-in fixture corpus currently covers 4 platforms with strictly different TLS fingerprint families. Tests **must not** mix observations across platforms — each row below is a separate ground truth. Windows Desktop is a planned fifth platform and must be added as its own corpus family, not folded into Linux Desktop or macOS Desktop assumptions.

#### Linux Desktop (verified fixtures)

| Fixture family | Browser | Non-GREASE ext SET / fixed order | Key structural properties |
|---|---|---|---|
| `chromium_44cd_mlkem_linux_desktop` | Chrome 133/144/146 | SET (shuffled): `{000A,0000,002D,0023,000B,002B,0033,FF01,FE0D,0010,000D,0017,44CD,001B,0005,0012}` | GREASE cipher[0]+ext[0]+ext[-1], ALPS=0x44CD, PQ=0x11EC, ECH={144,176,208,240}, 16+GREASE exts |
| `firefox_fixed_order_linux_desktop` | Firefox 148/149 | FIXED ORDER: `{0000,0017,FF01,000A,000B,0023,0010,0005,0022,0012,0033,002B,000D,002D,001C,001B,FE0D}` | No GREASE anywhere, 17 ciphers, FFDHE groups (0100/0101), delegated_creds(0022), record_size_limit(001C), ECH payload **239 bytes fixed** |

#### macOS Desktop (verified fixture — Firefox only)

| Fixture family | Browser | Non-GREASE ext SET / fixed order | Key structural differences vs Linux |
|---|---|---|---|
| `firefox149_macos26_3` | Firefox 149 on macOS 26.3 (Tahoe) | FIXED ORDER: `{0000,0017,FF01,000A,000B,0010,0005,0022,0012,0033,002B,000D,002D,001C,001B,FE0D}` | **No 0x0023** (session_ticket absent), ECH payload **399 bytes** (not 239), 16 non-GREASE exts (one fewer than Linux Firefox) |

> macOS Firefox is the **same cipher suite set** as Linux Firefox but produces a **different extension set** (missing `0x0023`) at a **different ECH payload length**. These are separate fixture families and must not be confused.

#### Windows Desktop (planned future fixtures)

| Future fixture family | Browser(s) | Status | Required separation rules |
|---|---|---|---|
| `*_windows_desktop` | Chrome / Edge / Firefox / other Windows browsers | **Pending future dumps** | Treat as a separate desktop platform. Do **not** reuse Linux Desktop assertions for Windows. Do **not** reuse macOS Desktop assertions for Windows. Every Windows fixture family must get its own extension-set, cipher-order, ECH-length, ALPS, GREASE, and contamination tests. |

> When Windows Desktop dumps land, add a dedicated Windows section to `ReviewedClientHelloFixtures.h`, add Windows-specific corpus phases, and extend Phase 12a cross-platform contamination tests with Linux-vs-Windows, macOS-vs-Windows, iOS-vs-Windows, and Android-vs-Windows checks.

Reserved future Windows phases and filenames:
- `Phase W1 — Windows Desktop Chrome Corpus at 1024 Iterations`
	Future file: `test/stealth/test_tls_corpus_windows_chrome_1k.cpp`
	Future suite: `WindowsChromeDesktopCorpus1k`
- `Phase W2 — Windows Desktop Firefox Corpus at 1024 Iterations`
	Future file: `test/stealth/test_tls_corpus_windows_firefox_1k.cpp`
	Future suite: `WindowsFirefoxDesktopCorpus1k`
- `Phase W3 — Windows Desktop Edge Corpus at 1024 Iterations`
	Future file: `test/stealth/test_tls_corpus_windows_edge_1k.cpp`
	Future suite: `WindowsEdgeDesktopCorpus1k`
- `Phase W4 — Windows Desktop Cross-Browser Contamination at 1024 Iterations`
	Future file: `test/stealth/test_tls_corpus_windows_cross_browser_1k.cpp`
	Future suite: `WindowsDesktopCrossBrowser1k`

Reserved future Windows fixture family IDs:
- `chrome*_windows_desktop`  
	Preferred canonical form: `chrome<major>_windows_desktop`  
	Examples: `chrome146_windows_desktop`, `chrome147_windows_desktop`
- `firefox*_windows_desktop`  
	Preferred canonical form: `firefox<major>_windows_desktop`  
	Examples: `firefox149_windows_desktop`, `firefox150_windows_desktop`
- `edge*_windows_desktop`  
	Preferred canonical form: `edge<major>_windows_desktop`  
	Examples: `edge141_windows_desktop`, `edge142_windows_desktop`

Windows fixture naming rules:
- Match the existing corpus convention: lowercase browser name + major version + `_windows_desktop`.
- Put Windows build / edition / channel details in fixture metadata first, not in the family ID.
- Add an extra suffix only when captures prove materially different TLS families for the same browser major.  
	Examples: `chrome146_windows11_24h2_desktop`, `edge141_windows10_22h2_desktop`.
- Keep family IDs stable enough that `ReviewedClientHelloFixtures.h`, `profiles_validation.json`, and test suite names do not need churn after the first checked-in Windows corpus.

Windows phase reservation rules:
- `Phase W1` is for Windows Chrome only. Do not widen it to generic Chromium-on-desktop.
- `Phase W2` is for Windows Firefox only. Do not merge it with Linux Firefox or macOS Firefox phases.
- `Phase W3` is for Windows Edge only. Keep Edge distinct even if its fingerprint is Chromium-adjacent.
- `Phase W4` is for intra-Windows discrimination only; inter-platform Windows-vs-Linux/macOS/iOS/Android coverage still belongs in `Phase 12a`.

#### iOS (verified fixtures — two distinct TLS families by iOS version)

| Fixture family | Browser(s) | Non-GREASE ext SET | Key structural properties |
|---|---|---|---|
| `Apple TLS family` (iOS 26.x + iOS 18.x) | Safari 18.7.6, Safari 26.x, Brave 1.88 iOS, Firefox 149.2 iOS, **Chrome 147 on iOS 26.4** | `{0000,0017,FF01,000A,000B,0010,0005,000D,0012,0033,002D,002B,001B}` | **No GREASE**, **no ALPS**, **no ECH**, **no session_ticket**, 13 exts, cipher count 13 (no 3DES, no FFDHE) |
| `iOS Chromium family` (iOS 26.1–26.3 only) | Chrome 146 iOS 26.1, Chrome 147 iOS 26.3 | SET (shuffled): `{0000,0017,FF01,000A,000B,0010,0005,000D,0012,0033,002D,002B,001B,0023,44CD,FE0D,0029}` | GREASE cipher[0]+ext[0], ALPS=0x44CD, ECH, PSK(0x0029 resumption), 17 non-GREASE exts including ALPS |

> **Critical iOS platform-version regression**: Chrome on iOS **26.4** switched to Apple TLS (same set as Safari — no ALPS, no ECH). Chrome on iOS **26.1/26.3** used its own Chromium TLS (with ALPS and ECH). These are genuinely different behaviors from the same browser on different OS versions. Tests for the iOS Chromium family must NOT assert ALPS/ECH for iOS 26.4. The current `BrowserProfile::IOS14` does not cover the Chromium-based iOS Chrome path — this is a documented profile gap.

#### Android (verified fixtures — two distinct structural families by ALPS presence)

| Fixture family | Browser(s) | Non-GREASE ext SET | Key structural properties |
|---|---|---|---|
| `Android Chromium-ALPS` | Brave 1.88 Android 13, Yandex 26.3.4 on Samsung Galaxy (Android 16) | SET (shuffled): `{FF01,0012,FE0D,0033,000D,0023,0005,001B,0000,002B,0010,000A,44CD,0017,002D,000B,0029}` | GREASE cipher[0]+ext[0]+ext[-1], ALPS=0x44CD, PQ=0x11EC, ECH, PSK(0x0029 resumption), 17 non-GREASE exts — same structural family as Linux Desktop Chrome |
| `Android Chromium-no-ALPS` | Chrome 146 Android 16 (all devices), Samsung Internet 29, Yandex 26.3.4 on OnePlus OxygenOS | SETs vary per browser but no entry has ALPS — e.g. Chrome 146 stock: `{0023,0012,0010,FE0D,0017,000D,002B,FF01,000B,0033,0005,002D,0000,000A,001B}` | GREASE cipher[0]+ext[0], **No ALPS**, ECH present, **no PSK**, 15 non-GREASE exts |

> **Yandex browser diverges by Android device**: Yandex on Samsung Galaxy (S25+) is in the Chromium-ALPS family (17 exts, ALPS, PSK). Yandex on OnePlus OxygenOS is in the Chromium-no-ALPS family (15 exts). Same browser app, different TLS fingerprint. Tests must use device-specific fixture IDs.

#### PSK extension note

`0x0029` (pre_shared_key) appears in Linux/macOS Chrome captures (`chrome146_177_linux_desktop:frame5`), iOS Chromium captures, Android-ALPS captures, and macOS Firefox. It is always a **TLS session resumption marker** — fresh connections from the generator under test do NOT produce `0x0029`. All tests that check extension sets must exclude `0x0029` from the "must be present" set. Any test that asserts `0x0029` is absent is testing fresh-connection behavior and is correct.

---

### Phase 0 — Shared Statistics Helper Header

**File**: `test/stealth/CorpusStatHelpers.h`

Contains (header-only inline helpers):
- `struct FrequencyCounter<T>`: `std::unordered_map<T,uint32>`, `count()`, `distinct_values()`, `min_observed()`, `max_observed()`, `coverage_ratio()`
- `check_uniform_distribution(counter, expected_n, num_buckets, min_fraction)` — asserts each possible value's count ≥ `expected_n / num_buckets * min_fraction`
- `extension_set_non_grease_no_padding(const ParsedClientHello&)` → `std::unordered_set<uint16>` (exclude GREASE, exclude `0x0015`)
- `non_grease_extension_sequence(const ParsedClientHello&)` → `vector<uint16>` (preserve order, exclude GREASE and padding)
- `compute_ja3_string()`, `compute_ja4_segments()` — inline reimplementations already in `test_tls_ja3_ja4_cross_validation.cpp` will be migrated here
- Constant tables: `kChrome133EchExtensionSet`, `kChrome133NoEchExtensionSet`, `kFirefox148ExtensionOrder`, `kSafariIosExtensionSet` derived from fixture ground truth

**Dependencies**: TlsHelloParsers.h, MockRng.h

---

### Phase 1 — Chrome Extension Set Fidelity at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp`  
**Test suite**: `ChromeCorpusExtensionSet1k`

Tests (all N=1024, seeds 0–1023):

1. `Chrome133EchExtensionSetExactMatch` — every generated Chrome133+ECH hello produces exactly `kChrome133EchExtensionSet`, not a superset or subset. Catches: stray extension added by bug, extension dropped, always-missing ECH
2. `Chrome133NoEchExtensionSetExactMatch` — every Chrome133+no-ECH hello has set = `kChrome133EchExtensionSet \ {0xFE0D}`
3. `Chrome131ExtensionSetContainsAlps4469NotAlps44CD` — every Chrome131 hello contains `0x4469`, never `0x44CD` (across 1024 runs)
4. `Chrome133ExtensionSetContainsAlps44CDNotAlps4469` — every Chrome133 hello contains `0x44CD`, never `0x4469` (across 1024 runs)
5. `Chrome120ExtensionSetPQAbsent` — Chrome120 must never include `0x11EC` in supported_groups (Chrome120 = non-PQ profile)
6. `NoPaddingExtensionInExtensionSetCount` — `0x0015` never in the SET (it's structural, not counted); passes every chrome profile for 1024 runs
7. `NoSessionTicketExtensionOnFreshConnections` — `0x0023` (session_ticket) is present; `0x0029` (pre_shared_key/PSK) is NOT present (fresh connection without TLS resumption context). This is the `chrome146_177_linux_desktop:frame5` edge case test — PSK extension only appears on resumption.
8. `ChromeOnly3DesAbsenceAcrossAllSeeds` — no 3DES ciphers ever in 1024 runs for Chrome133/131/120 — adversarial bulk regression of existing 10-seed test
9. `Chrome133ExtensionCountMatchesFixture` — count of non-GREASE, non-padding extensions must be exactly 16 (with ECH) or 15 (without ECH) every time across 1024 seeds

**Adversarial negative tests** (designed to catch subtle bugs):
- `NegativeTestDuplicateExtensionNeverAppears` — across 1024 runs, no generated hello has any extension type appearing more than once (catches a bug where GREASE lands both at anchor position AND inside the shuffled block)
- `NegativeTestNoFirefoxOnlyExtensionInChrome` — 0x0022 (delegated_credentials) and 0x001C (record_size_limit) — Firefox-only — must never appear in Chrome profiles across 1024 runs

---

### Phase 2 — Chrome GREASE Distribution Uniformity at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_chrome_grease_uniformity_1k.cpp`  
**Test suite**: `ChromeGreaseUniformity1k`

GREASE values: `{0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A, 0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA}` (16 valid GREASE values). In 1024 runs, expected ~64 each. Min threshold: ≥20 (very conservative, catches stuck at one value).

Tests:

1. `CipherSuiteSlot0GreaseIsDistributedAcrossMultipleValues` — collect cipher[0] GREASE across 1024 seeds. Must see ≥8 distinct GREASE values. Catches: init_grease() with broken RNG producing same value.
2. `SupportedGroupsSlot0GreaseIsDistributedAcrossMultipleValues` — collect supported_groups[0] GREASE. Must see ≥8 distinct values.
3. `ExtensionSlot0GreaseIsDistributedAcrossMultipleValues` — first extension (GREASE anchor) must vary. ≥8 distinct values.
4. `ExtensionLastSlotGreaseIsDistributedAcrossMultipleValues` — last extension (GREASE tail anchor) must vary. ≥8 distinct values.
5. `KeyShareSlot0GreaseGroupVariesAcross1024Seeds` — key_share entries[0].group is GREASE; must see ≥8 distinct values
6. `SupportedVersionsGreaseVariesAcross1024Seeds` — supported_versions[0] is GREASE for Chrome; ≥4 distinct values
7. `NoCipherGreaseValueExceedsHalfOfAllRuns` — no single GREASE value should dominate ≥512/1024 runs (catches stuck RNG at one value producing skewed distribution)
8. `NoCipherGreaseIsZeroOrAllFF` — no GREASE value should be 0x0000 or 0xFFFF (invalid, non-GREASE values)

**Adversarial statistical tests** (black-hat view — exploit distribution bias):
9. `GreaseCipherSlot0MostFrequentValueUnderThresholds` — the most common GREASE value must appear < 30% of the time (< 307/1024). A badly seeded PRNG could collapse to 3-4 values.
10. `GreaseSupportedGroupsSlot0MostFrequentValueUnderThreshold` — same check per 30% threshold

---

### Phase 3 — ECH Payload Length Distribution at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_chrome_ech_payload_uniformity_1k.cpp`  
**Test suite**: `ChromeEchPayloadUniformity1k`

Sampling function: `144 + bounded(4) * 32` → values `{144, 176, 208, 240}`. N=1024 → expected 256 each. Minimum threshold: ≥128 per value (ensures none starved).

Tests:

1. `EchPayloadLengthAllFourValuesAppear` — across 1024 Chrome133 ECH-enabled hellos, all 4 payload lengths appear ≥1 time (basic coverage)
2. `EchPayloadLengthEachValueMeets12p5PercentMinimum` — each of {144,176,208,240} appears ≥128 times. Catches off-by-one in `bounded(4)` that skips value 3 (length 240).
3. `EchPayloadLength144MustNotDominateMoreThanHalf` — value 144 must appear ≤512 times. Catches bounded(4) always returning 0.
4. `EchPayloadLengthNoValueExceeds512Runs` — max count ≤512 (50% cap). 
5. `EchPayloadLengthDistributionApproximatelyUniform` — χ² approximation: max_count/min_count ratio < 3.0 across 1024 runs
6. `EchPayloadLengthOnlyAllowedValuesObserved` — no payload length outside {144,176,208,240} ever appears (catches unbounded sampling)
7. `EchPayloadLengthConsistentWithWireLengthIncrements` — ECH payload length from parser matches actual ECH extension body size (not just declared)
8. `Chrome131EchPayloadLengthUniformityCheck` — same 4-value uniformity for Chrome131 profile
9. `EchPayloadLengthEncKeyAlwaysX25519` — enc key length is always 32 bytes (X25519) across 1024 runs — not one of the PQ lengths
10. `EchDisabledLaneHasNoEchExtension` — 1024 ECH-disabled Chrome133 hellos: 0xFE0D never appears

**Adversarial:**
11. `EchPayloadLengthNotConstantWhenRngIsAllOnes` — override MockRng to return 0xFFFFFFFF for bounded() — must still produce only valid lengths from the sampling formula (not overflow into illegal value)
12. `EchPayloadLengthNotConstantWhenRngIsAllZeros` — override MockRng to return 0 — `bounded(4)` → 0, payload must be 144, not 0 or garbage

---

### Phase 4 — Chrome Extension Permutation Position Stability at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_chrome_permutation_position_1k.cpp`  
**Test suite**: `ChromePermutationPosition1k`

Tests (deeper than existing 500-seed entropy test):

1. `RenegotiationInfoAppearsManyPositions` — 0xFF01 appears in ≥8 distinct non-anchor positions across 1024 Chrome133 runs (broken in pre-V7 tail-anchor fix)
2. `SignatureAlgorithmsAppearsManyPositions` — 0x000D in ≥8 positions
3. `SessionTicketAppearsManyPositions` — 0x0023 in ≥8 positions
4. `AlpsExtensionAppearsManyPositions` — 0x44CD in ≥8 positions (not pinned to end)
5. `StatusRequestAppearsManyPositions` — 0x0005 in ≥8 positions
6. `ExtensionAtPosition0IsAlwaysGrease` — absolute position 0 extension is GREASE in 100% of 1024 runs
7. `ExtensionAtLastPositionIsAlwaysGrease` — last extension is GREASE in 100% of 1024 runs (pre-padding)
8. `PaddingExtensionOnlyAtAbsoluteLastPosition` — 0x0015 if present, only at the very last position
9. `EchExtensionAlwaysInShuffledBlock` — 0xFE0D must not be stuck at a fixed position in the shuffled block; appears in ≥6 distinct positions across 1024 runs
10. `SniAlwaysInPermutationPool` — 0x0000 (SNI) appears in ≥8 distinct positions within the permuted block across 1024 Chrome133 runs

**Adversarial:**
11. `NegativeRenegotiationInfoNotAlwaysLast` — across 1024 runs, 0xFF01 is not at the last non-GREASE position in >95% of connections (catches the V7-regressed tail-anchor bug)
12. `FirefoxPositionIsIdenticalForAllSeeds` — for Firefox148, all 1024 runs have 0xFF01 at EXACTLY the same position (position 2, 0-indexed non-GREASE) — tests fixed-order invariance from opposite direction

---

### Phase 5A — Firefox Extension Order Invariance at 1024 Iterations (Linux Desktop)

**File**: `test/stealth/test_tls_corpus_firefox_invariance_1k.cpp`  
**Test suite**: `FirefoxLinuxDesktopInvariance1k`  
**Platform scope**: Linux Desktop only. Ground truth: `firefox148_linux_desktop:frame5` and `firefox149_linux_desktop:frame5`.

Fixed order: `{0x0000, 0x0017, 0xFF01, 0x000A, 0x000B, 0x0023, 0x0010, 0x0005, 0x0022, 0x0012, 0x0033, 0x002B, 0x000D, 0x002D, 0x001C, 0x001B, 0xFE0D}`

Tests:

1. `Firefox148AllRunsHaveIdenticalExtensionSequence` — 1024 runs: ALL have EXACTLY the reference extension order. Any deviation fails.
2. `Firefox148NoExtensionOrderVariation` — collect distinct extension sequences; must be exactly 1 distinct sequence
3. `Firefox148NoGreaseInCipherSuites` — 1024 runs: no GREASE in cipher suites (ever)
4. `Firefox148NoGreaseInExtensions` — 1024 runs: no GREASE in extension list (ever)
5. `Firefox148NoGreaseInSupportedGroups` — 1024 runs: no GREASE in supported groups (ever)
6. `Firefox148CipherSuiteExactOrder1024Runs` — cipher suite sequence is identical across 1024 runs (no variation)
7. `Firefox148EchIsAlwaysLast` — 0xFE0D is the last non-padding extension in 100% of 1024 ECH-enabled runs
8. `Firefox148EchIsAlwaysLastEvenWithDifferentDomains` — ECH still last when domain changes (seeds vary but domain prefix also varies in 100 runs with different domains)
9. `Firefox148HasDelegatedCredentials0x0022` — 0x0022 present 100% of 1024 runs (Firefox-exclusive; Chrome never has it)
10. `Firefox148HasRecordSizeLimit0x001C` — 0x001C present 100% of 1024 runs (Firefox-exclusive)
11. `Firefox148FfdheGroupsPresent` — 0x0100 or 0x0101 present in supported groups 100% of 1024 runs
12. `Firefox148KeyShareTripleEntries` — key_share has exactly 3 entries (PQ:11EC + x25519 + secp256r1) in 100% of 1024 runs
13. `Firefox148EchPayloadIs239Fixed` — 1024 ECH-enabled FF148 runs: payload always exactly 239 bytes (Firefox uses fixed ECH size, not sampled)
14. `Firefox148NoPsKeyModeExtension` — 0x002D absent... wait, Firefox DOES have 0x002D — see fixture. Remove this test.
14. `Firefox148AlpsExtensionNeverPresent` — 0x44CD and 0x4469 must never appear in 1024 Firefox148 runs

**Light fuzz:**
15. `Firefox148WithFuzzedDomainStillProducesFixedOrder` — 20 distinct domain names × 51 seeds each: extension order invariant
16. `Firefox148WithFuzzedTimestampStillProducesFixedOrder` — 1024 distinct unix_time values: extension order invariant to timestamp

---

### Phase 5B — Firefox Extension Order and ECH Invariance at 1024 Iterations (macOS Desktop)

**File**: `test/stealth/test_tls_corpus_firefox_macos_1k.cpp`  
**Test suite**: `FirefoxMacosDesktopInvariance1k`  
**Platform scope**: macOS Desktop only. Ground truth: `firefox149_macos26_3:frame8`.

Fixed order (macOS differs from Linux — **no `0x0023` session_ticket**):  
`{0x0000, 0x0017, 0xFF01, 0x000A, 0x000B, 0x0010, 0x0005, 0x0022, 0x0012, 0x0033, 0x002B, 0x000D, 0x002D, 0x001C, 0x001B, 0xFE0D}`  
(16 non-GREASE extensions; ECH payload **399 bytes fixed**, not 239)

Tests (N=1024):

1. `MacosFirefoxExtensionOrderExactMatch` — all 1024 runs produce EXACTLY the reference macOS extension order. Any deviation fails.
2. `MacosFirefoxHasNo0x0023SessionTicket` — `0x0023` is absent in 100% of 1024 runs. This is the primary macOS-vs-Linux distinguisher.
3. `MacosFirefoxEchPayloadAlwaysIs399` — ECH payload length is 399 in 100% of 1024 ECH-enabled runs (not 239, not sampled from {144,176,208,240}).
4. `MacosFirefoxEchPayloadDiffersFromLinuxFirefoxEchPayload` — 399 ≠ 239: assert them explicitly to document the intentional difference.
5. `MacosFirefoxNoGreaseInCipherSuites` — 1024 runs: no GREASE in cipher suites.
6. `MacosFirefoxNoGreaseInExtensions` — 1024 runs: no GREASE in extension list.
7. `MacosFirefoxCipherSuiteExactOrder1024Runs` — cipher suit sequence is identical across 1024 runs (same 17-cipher set as Linux Firefox, same order: {1301,1303,1302,C02B,C02F,CCA9,CCA8,C02C,C030,C00A,C009,C013,C014,009C,009D,002F,0035}).
8. `MacosFirefoxHasDelegatedCredentials0x0022` — 0x0022 present 100% of 1024 runs.
9. `MacosFirefoxHasRecordSizeLimit0x001C` — 0x001C present 100% of 1024 runs.
10. `MacosFirefoxFfdheGroupsPresent` — FFDHE groups present in supported_groups in 100% of 1024 runs (same as Linux Firefox).
11. `MacosFirefoxEchIsAlwaysLast` — 0xFE0D is the last non-padding extension in 100% of 1024 runs.
12. `MacosFirefoxNoPskOnFreshConnections` — 0x0029 is absent in 100% of 1024 fresh-connection runs.

**Adversarial cross-platform:**
13. `MacosFirefoxExtensionSetDiffersFromLinuxFirefox` — macOS extension set (sans ECH/PSK) must differ from Linux Firefox set — the missing `0x0023` is the discriminant. Computing set-symmetric-difference must yield `{0x0023}`.
14. `MacosFirefoxEchPayloadDiffersFromChrome133EchPayload` — macOS Firefox ECH payload length (399) must differ from every Chrome payload length in {144,176,208,240}.

---

### Phase 6a — iOS Apple TLS Corpus at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`  
**Test suite**: `IosAppleTlsCorpus1k`  
**Platform scope**: iOS Apple TLS family. Covers: Safari 18.x, Safari 26.x, Brave 1.88 iOS, Firefox 149.2 iOS, Chrome 147 on **iOS 26.4 only** (regressed to Apple TLS). All use the same 13-extension set with NO GREASE.  
**Runtime profile mapping**: `BrowserProfile::IOS14` (advisory, structural tests only)

Ground truth: `{0x0000, 0x0017, 0xFF01, 0x000A, 0x000B, 0x0010, 0x0005, 0x000D, 0x0012, 0x0033, 0x002D, 0x002B, 0x001B}` — 13 extensions, no GREASE, no ALPS, no ECH, no session_ticket (0x0023), no delegated_credentials (0x0022), no record_size_limit (0x001C).

Tests (N=1024):

1. `IOS14ProfileNeverHasEch` — 0xFE0D never present in 1024 IOS14 runs (allows_ech=false)
2. `IOS14ProfileNeverHasAlps` — neither 0x44CD nor 0x4469 in 1024 IOS14 runs
3. `IOS14ProfileNeverHasPq` — 0x11EC not in supported_groups in 1024 IOS14 runs (has_pq=false)
4. `IOS14ProfileNeverHasSessionTicket` — 0x0023 absent in 100% of 1024 runs (Apple TLS family does not include session_ticket)
5. `IOS14ProfileNeverHasDelegatedCredentials` — 0x0022 absent in 100% of 1024 runs (Firefox-only extension)
6. `IOS14ProfileExtensionCount13` — exactly 13 non-GREASE non-padding extensions in 100% of 1024 runs
7. `IOS14ProfileExtensionSetMatchesAppleTlsFamily` — extension SET equals the 13-extension Apple TLS reference in 100% of 1024 runs
8. `IOS14ProfileNoGreaseInCipherOrExtensions` — no GREASE in cipher suites AND no GREASE in extensions in 100% of 1024 runs (Apple TLS stack does not use GREASE)
9. `IOS14ProfileNoPsk` — 0x0029 absent in 100% of 1024 fresh-connection runs
10. `IOS14ProfileExtensionOrderIsFixed` — collect distinct extension sequences across 1024 runs; IOS14 profile has `ExtensionOrderPolicy::FixedFromFixture` → exactly 1 distinct order

**Adversarial iOS-vs-other-platform:**
11. `IOS14ProfileExtensionSetDiffersFromLinuxDesktopChrome` — 13-ext Apple TLS set is a proper subset of the 16-ext Linux Chrome set; assert they are NOT equal
12. `IOS14ProfileExtensionSetDiffersFromFirefox148` — Apple TLS set never matches Linux Firefox set (different count, no 0x0022, no 0x001C, no 0x001B... wait 0x001B is present in Apple TLS — but the overall set still differs due to 0x0022 and 0x001C)
13. `NegativeIOS14ProfileDoesNotMatchIosChromiumFamily` — IOS14 extension set must NOT contain ALPS (0x44CD) — this guards against accidentally selecting the iOS Chromium profile behaviour (Chrome on iOS 26.1/26.3 had ALPS; IOS14 must not)

---

### Phase 6b — iOS Chromium Corpus Gap Documentation at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_ios_chromium_gap_1k.cpp`  
**Test suite**: `IosChromiumGap1k`  
**Platform scope**: iOS Chromium family. Real-world captures: Chrome 146 on iOS 26.1, Chrome 147 on iOS 26.3. These show ALPS=0x44CD, ECH, PSK — NOT the same as Safari/iOS 26.4 Chrome. The **current generator has no BrowserProfile for this** — these tests document the gap and guard against profile mis-selection.

Ground truth extension SET from `chrome146_0_7680_151_ios26_1:frame40` and `chrome147_0_7727_47_ios26_3:frame27`:  
`{0x0000, 0x0017, 0xFF01, 0x000A, 0x000B, 0x0010, 0x0005, 0x000D, 0x0012, 0x0033, 0x002D, 0x002B, 0x001B, 0x0023, 0x44CD, 0xFE0D, 0x0029}` (17 exts, includes ALPS=0x44CD, ECH, PSK)

Tests (N=1024):

1. `IOS14ProfileDoesNotMatchChromiumOnIos261Set` — IOS14 profile extension set must differ from the iOS Chromium 17-extension set; assert set-inequality for 1024 runs
2. `IOS14ProfileDoesNotHaveAlpsWhichIosChromiumHas` — IOS14 profile never produces 0x44CD; Chromium-on-iOS-26.1 always had 0x44CD — this gap is documented here
3. `Chrome133NonRuProfileNotTheSameAsIosChromiumProfile` — Chrome133 (Linux profile) + non-RU route ≠ iOS Chromium fingerprint: distinguished by ALPN differences and key share structure (must differ in at least 3 structural dimensions)
4. `IosChromiumFamilyPlatformVersionRegressionNote` — **documentation test** (always passes): asserts that `chrome147_0_7727_47_ios26_4_a` fixture has 13 extensions (iOS 26.4 regressed to Apple TLS) while `chrome147_0_7727_47_ios26_3` fixture had 17 extensions (Chromium TLS). Verifies the fixture data in ReviewedClientHelloFixtures.h reflects this version split.

---

### Phase 6c — Android Chromium-ALPS Corpus at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_android_chromium_alps_1k.cpp`  
**Test suite**: `AndroidChromiumAlpsCorpus1k`  
**Platform scope**: Android browsers in the Chromium-ALPS family. Verified fixtures: `brave188_138_android13:frame100`, `yandex26_3_4_android16_samsung:frame174`. These share ALPS=0x44CD, ECH, PSK, and the same 17-extension shuffled pool as Linux Desktop Chrome — **plus `0x0029`**.  
**Runtime profile mapping**: `BrowserProfile::Android11_OkHttp_Advisory` is NOT this family (OkHttp is HTTP-only, not Chromium); the closest mapping is `BrowserProfile::Chrome133` with Android-specific platform hints. **This is a documented generator gap** — no profile today exactly produces the Android-Brave/Yandex-Samsung fingerprint.

Ground truth extension SET: `{0xFF01, 0x0012, 0xFE0D, 0x0033, 0x000D, 0x0023, 0x0005, 0x001B, 0x0000, 0x002B, 0x0010, 0x000A, 0x44CD, 0x0017, 0x002D, 0x000B}` (16 non-PSK, non-GREASE, non-padding extensions — same count as Linux Chrome with ECH; additionally `0x0029` appears on resumption connections)

Tests (N=1024 against Chrome133 profile with ChromeShuffleAnchored, used as best available approximation):

1. `AndroidAlpsChromiumFamilyHasAlps44CD` — BrowserProfile::Chrome133 (best proxy): 0x44CD present 100% of 1024 runs (shared with Brave/Yandex-Samsung Android)
2. `AndroidAlpsChromiumFamilyHasEch` — ECH present with non-RU route in 100% of 1024 runs
3. `AndroidAlpsChromiumFamilyHasPq` — 0x11EC in supported_groups in 100% of 1024 runs
4. `AndroidAlpsChromiumFamilyNoPskOnFresh` — 0x0029 absent on fresh connections in 100% of 1024 runs
5. `AndroidAlpsChromiumFamilyExtensionSetMatchesLinuxChrome` — extension SET (sans PSK) matches the Linux Desktop Chromium-44CD family in 100% of 1024 runs (same 16-extension SET with ECH)
6. `AndroidAlpsChromiumFamilyShuffleIsPresent` — extension ordering varies (ChromeShuffleAnchored policy): ≥100 distinct orderings across 1024 runs
7. `AndroidAlpsChromiumFamilyCipherSuiteMatchesCapture` — cipher suites (non-GREASE, ordered) match the Brave Android 13 captured order in 1024 runs

**Platform gap contracts:**
8. `NegativeTestAndroid11OkHttpAdvisoryIsNotChromiumAlpsBrowser` — Android11_OkHttp_Advisory profile extension set must differ from Brave-Android extension set (Android11_OkHttp_Advisory has no ALPS, no PQ, fixed-order — different structural family)
9. `NegativeTestAndroidAlpsChromiumNotSameAsAndroidNoAlpsChromium` — Chrome133 (as Android-ALPS proxy) produces ALPS; must never equal a 15-extension "no-ALPS" Android Chrome fingerprint. Any hello with 0x44CD cannot simultaneously match the no-ALPS set (verified by set-inequality)

---

### Phase 6d — Android Chromium-no-ALPS Corpus at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_android_chromium_no_alps_1k.cpp`  
**Test suite**: `AndroidChromiumNoAlpsCorpus1k`  
**Platform scope**: Android browsers NOT in the Chromium-ALPS family. Verified fixtures: `chrome146_177_android16:frame94`, `chrome146_177_android16_pixel9proxl:frame129`, `chrome146_177_oxygenos16_oneplus13:frame102`, `samsung_internet29_android16_galaxy_s25plus:frame133`, `yandex26_3_4_128_oxygenos16_oneplus13:frame76` (Yandex on OnePlus). All have 15 non-GREASE non-padding extensions with NO ALPS.  
**Runtime profile mapping**: `BrowserProfile::Android11_OkHttp_Advisory` (advisory, structural tests only). **Generator gap exists**: current profile uses a simple fixed-order structure; real Android Chrome uses ChromeShuffleAnchored without ALPS.

Ground truth extension SET for Chrome 146 Android 16 (representative): `{0x0023, 0x0012, 0x0010, 0xFE0D, 0x0017, 0x000D, 0x002B, 0xFF01, 0x000B, 0x0033, 0x0005, 0x002D, 0x0000, 0x000A, 0x001B}` — 15 extensions, ECH present, NO ALPS, NO PSK on fresh connections.

Tests (N=1024):

1. `Android11OkHttpAdvisoryProfileNeverHasAlps` — 0x44CD and 0x4469 absent in 100% of 1024 Android11_OkHttp_Advisory runs
2. `Android11OkHttpAdvisoryProfileNeverHasPq` — 0x11EC absent from supported_groups in 100% of 1024 runs (has_pq=false)
3. `Android11OkHttpAdvisoryProfileNeverHasEch` — 0xFE0D absent in 100% of 1024 runs (allows_ech=false)
4. `Android11OkHttpAdvisoryProfileNoPsk` — 0x0029 absent in 100% of 1024 fresh-connection runs
5. `Android11OkHttpAdvisoryProfileExtensionOrderIsFixed` — ExtensionOrderPolicy::FixedFromFixture → exactly 1 distinct extension order across 1024 runs
6. `Android11OkHttpAdvisoryProfileNoGrease` — no GREASE in cipher suites or extension list in 100% of 1024 runs
7. `NegativeTestAndroidNoAlpsExtensionSetDiffersFromDesktopChrome` — Android11_OkHttp_Advisory extension set differs from Chrome133 (Linux Desktop) extension set: no ALPS (0x44CD), different count (15 vs 16), different members
8. `NegativeTestAndroidNoAlpsFamilyDiffersFromAndroidAlpsFamily` — Android11_OkHttp_Advisory set must NOT contain 0x44CD, which is present in Brave/Yandex-Samsung Android corpus — explicit cross-family discriminant
9. `NegativeTestYandexOneplusVsYandexSamsungAreDifferentFamilies` — Yandex-OnePlus (no-ALPS: 15 exts) fixture differs from Yandex-Samsung (ALPS: 17 exts). Uses fixture data from ReviewedClientHelloFixtures.h to assert extension count inequality and ALPS presence/absence. **This is the canonical cross-device cross-family test within the same browser app.**
10. `NegativeTestAndroidNoAlpsNotSameAsIosAppleTls` — Android no-ALPS extension SET differs from Apple TLS 13-extension set (different count, different members — e.g. Android has 0xFE0D ECH, Apple TLS family does not)

---

### Phase 7 — JA3/JA4 Hash Corpus Stability at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_ja3_ja4_stability_1k.cpp`  
**Test suite**: `JA3JA4CorpusStability1k`

Tests:

1. `Chrome133EchJA3IsIdenticalAcross1024Seeds` — compute JA3 for each of 1024 Chrome133+ECH hellos; all must be equal to each other (GREASE+permutation don't affect JA3 when computed correctly — types sorted, GREASE excluded)
2. `Chrome133EchJA3NotKnownTelegramHash` — computed JA3 ≠ `kKnownTelegramJa3` ("e0e58235789a753608b12649376e91ec")
3. `Chrome131EchJA3IsIdenticalAcross1024Seeds` — same for Chrome131 (different ALPS type → different JA3)
4. `Chrome133JA3DiffersFromChrome131JA3` — Chrome133 JA3 ≠ Chrome131 JA3 (ALPS type difference)
5. `Firefox148EchJA3IsIdenticalAcross1024Seeds` — FF148 JA3 stable
6. `Firefox148JA3DiffersFromChrome133JA3` — must differ (different cipher count, different extensions)
7. `Chrome133JA4SegmentBIsIdenticalAcross1024Seeds` — JA4 segment B (sorted cipher hash) is same across 1024 Chrome133 runs (cipher suite set doesn't change)
8. `Chrome133JA4SegmentCEchEnabledIsIdenticalAcross1024Seeds` — JA4 segment C (sorted extension hash) is same when ECH included  
9. `Chrome133JA4SegmentCEchDisabledIsIdenticalAcross1024Seeds` — same for ECH-disabled lane
10. `Chrome133JA4SegmentCEchEnabledDiffersFromEchDisabled` — ECH presence changes the extension set → different JA4-C hash
11. `Chrome133JA4SegmentACorrectTlsVersionCount` — segment A encodes TLS 1.3, correct cipher count, "d" (SNI present), "hh" (h2 ALPN)
12. `Firefox148JA4SegmentCIsIdenticalAcross1024Seeds` — FF148 JA4-C stable
13. `AllProfilesJA3NotKnownTelegramHash` — iterate all 7 profiles × seeds 0-9; none must equal Telegram hash
14. `Chrome133JA4SegmentAEncodesTls13` — first 2 chars of segment A after transport marker are "13"
15. `Firefox148JA4SegmentBDiffersFromChrome133JA4SegmentB` — different cipher suite sets → different JA4-B

**Adversarial:**
16. `JA3StabilityWithFuzzedTimestampAcross100Times` — changing unix_time (100 distinct values) doesn't change JA3 
17. `JA3StabilityWithFuzzedDomain` — changing domain (100 domains) doesn't leak into JA3 (SNI is excluded from JA3 extension hash)

---

### Phase 8 — ALPS Type Consistency at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_alps_type_consistency_1k.cpp`  
**Test suite**: `AlpsTypeConsistency1k`

Tests:

1. `Chrome133AlwaysHasAlps44CD_1024Runs` — 0x44CD present in extension set in 100% of 1024 Chrome133 runs
2. `Chrome133NeverHasAlps4469_1024Runs` — 0x4469 never present in Chrome133 extension set across 1024 runs  
3. `Chrome131AlwaysHasAlps4469_1024Runs` — 0x4469 in 100% of 1024 Chrome131 runs
4. `Chrome131NeverHasAlps44CD_1024Runs` — 0x44CD never present in Chrome131 runs
5. `Chrome120AlwaysHasAlps4469_1024Runs` — Chrome120 spec has alps_type=0x4469 in PROFILE_SPECS
6. `Firefox148NeverHasAnyAlpsExtension_1024Runs` — neither 0x44CD nor 0x4469 in 1024 Firefox runs
7. `Safari26_3NeverHasAnyAlpsExtension_1024Runs` — no ALPS in Safari
8. `IOS14NeverHasAnyAlpsExtension_1024Runs` — no ALPS in IOS14
9. `Android11OkHttpNeverHasAnyAlpsExtension_1024Runs` — no ALPS in Android11
10. `AlpsExtensionBodyBytesMatchAlpsTypeBytes` — for Chrome133: extension body of 0x44CD must contain`\x00\x05\x00\x03\x02\x68\x32` (ALPS body with h2 protocol) in 1024 runs
11. `NegativeTestNoProfileProducesBothAlpsTypes` — across all 7 profiles × 20 seeds: no hello contains BOTH 0x44CD and 0x4469 simultaneously

**Adversarial:**
12. `NegativeTestAlpsTypeMismatchNotIntroducedByShuffling` — Chrome133 extension shuffle must not accidentally swap the ALPS type value between 0x44CD and some other extension body. Verify the ALPS extension body bytes match the expected body content (not just the extension type).

---

### Phase 9 — Wire Size Distribution at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_wire_size_distribution_1k.cpp`  
**Test suite**: `WireSizeDistribution1k`

Tests:

1. `Chrome133EchEnabledHasExactly4DistinctSizes` — 1024 Chrome133+ECH runs → exactly 4 distinct wire sizes (corresponding to {144,176,208,240} ECH payload lengths)
2. `Chrome133EchEnabledSizeDistributionApproximatelyUniform` — 4 bins, each must appear ≥128/1024 times
3. `Chrome133EchDisabledHasAtLeast7DistinctSizes` — 1024 runs without ECH: `sample_padding_entropy_length` produces 8 values (0..7)*8; at least 7 distinct total sizes (one value could be missing by chance at N=1024 but 7 is safe lower bound)
4. `Chrome133EchDisabledNoDominantSize` — no single wire size > 512/1024 (50% cap)
5. `Firefox148HasFixedSizeAcross1024Runs` — Firefox148 ECH-enabled: wire size should be identical across 1024 runs (Firefox has no padding entropy, fixed ECH=239 bytes, fixed cipher suite, fixed extensions)
6. `Firefox148EchDisabledSizeIsAlsoFixed` — without ECH, Firefox has zero padding entropy (allows_padding=false) → exactly 1 distinct size
7. `Safari26_3EchDisabledHasZeroPaddingEntropy` — Safari26_3 has `allows_padding=false` → no padding entropy → fixed size in 1024 runs
8. `IOS14EchDisabledHasFixedSize` — no padding entropy → stable size
9. `AllProfilesNoWireSizeEquals517` — bulk regression: no wire size == 517 across all 7 profiles × 1024 seeds (blocks S1 regression)
10. `Chrome133WireSizesAlwaysGreaterThan200` — basic sanity lower bound (prevent truncation bug)
11. `Chrome133WireSizesAlwaysLessThan16000` — upper bound sanity (prevent runaway allocation bug)

**Adversarial:**
12. `NegativeTestEchEnabledLaneNeverProducesSameSizeAsEchDisabledLane` — Chrome133 with ECH enabled must produce sizes that would be impossible with ECH disabled. Since ECH adds >100 bytes (144 + overhead), the ECH-enabled minimum size should be > ECH-disabled maximum.

---

### Phase 10 — Adversarial DPI Resilience Corpus at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_adversarial_dpi_1k.cpp`  
**Test suite**: `AdversarialDpiCorpus1k`

Tests:

1. `ChromeSessionIdAlways32Bytes` — all Chrome profiles: session ID is always exactly 32 random bytes across 1024 runs (browsers send 32-byte fake session ID)
2. `FirefoxSessionIdAlways32Bytes` — Firefox148: always 32 bytes (Firefox also sends legacy session ID)
3. `SessionIdVariesAcrossConnections` — two consecutive Chrome133 hellos have different session IDs (they're randomized)
4. `ClientRandomIsNotStaticAcrossConnections` — consecutive Chrome133 hellos: client_random bytes differ (32-byte field randomized each connection)
5. `ClientRandomTimestampTailEmbedded` — for a given unix_time T, the last 4 bytes of client_random encode T in LE — verify with known time values (confirms HMAC embedding is working)
6. `ClientRandomTimestampTailDoesNotLeakPatterns` — 1024 connections at unix_time incremented by 1 each: the last 4 bytes of client_random must not be monotonically increasing raw bytes (HMAC must hash T, not store T directly). This test may FAIL if HMAC embedding is broken.
7. `KeyShareX25519IsRandomPerConnection` — the 32-byte X25519 key share in key_share[last] differs in every connection across 1024 runs
8. `PqKeyShareIsRandomPerConnection` — the 1184-byte ML-KEM-768 key share varies per connection
9. `PqKeyShareDifferentFromX25519KeyShare` — key_share PQ bytes ≠ key_share X25519 bytes (catches copy-paste bug)
10. `EchEncKeyIs32RandomBytesPerConnection` — ECH enc key varies across 1024 runs (not static)
11. `NoConsecutiveConnectionShareIdenticalWire` — run 1024 consecutive connections with seed=i; no two consecutive hellos share identical wire bytes. Tests that time-based entropy is working.
12. `Chrome133RuRouteNeverHasEch` — `NetworkRouteHints{is_known=true, is_ru=true}` → 1024 runs, 0xFE0D never present
13. `Chrome133UnknownRouteNeverHasEch` — `{is_known=false}` → 1024 runs, 0xFE0D never present
14. `Chrome133NonRuRouteAlwaysHasEch` — `{is_known=true, is_ru=false}` → 1024 runs, 0xFE0D always present
15. `ProxyBuildAlwaysProducesHttp11AlpnOnly` — `build_proxy_tls_client_hello_for_profile(...)` → 1024 runs, ALPN always `http/1.1` only (never h2)

**Adversarial black-hat simulation:**
16. `NoDpiDetectablePeriodicPatternInExtensionPosition` — run 1000 connections with seeds 0–999; compute position of RenegotiationInfo (0xFF01) in each. The sequence of positions must not be periodic (autocorrelation at lag=N/2 must be < 0.1). Catches a broken PRNG that cycles.
17. `NoDpiDetectablePatternInGreaseValues` — 1000 cipher[0] GREASE values from seeds 0-999 must have low autocorrelation — the GREASE sequence should not be predictable.
18. `LegacyVersionField0x0303InAllHellos` — TLS 1.3 ClientHello must always have legacy version 0x0303, not 0x0301 or 0x0302 across 1024 runs

---

### Phase 11 — GREASE Slot Statistical Independence at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_grease_slot_independence_1k.cpp`  
**Test suite**: `GreaseSlotIndependence1k`

Tests:

1. `CipherGreaseAndExtensionGreaseAreNotIdenticalPairs` — in 1024 runs: cipher[0] GREASE and extension[0] GREASE are NOT always the same value. A DPI that observes both slots can detect if they're always identical (lazy PRNG reuse).
2. `CipherGreaseAndSupportedGroupsGreaseAreNotIdenticalPairs` — same check for cipher vs supported_groups
3. `Extension0GreaseAndExtensionLastGreaseAreNotIdenticalPairs` — first and last GREASE extension differ in >99% of runs (they come from different `init_grease` calls with different seeds)
4. `CipherGreaseContingencyTableHasLowChiSquared` — build a 16×16 contingency table of (cipher_grease, ext_grease) pairs across 1024 runs; check that the chi-squared statistic isn't dominated by diagonal entries (i.e., they're not correlated)
5. `SubmittedGreaseVectorNoPairwiseIdenticalWithinHello` — within a SINGLE hello, all GREASE values must be distinct from each other. Chrome's `init_grease` produces distinct pairs at different seeds — this tests that the multi-seed `init_grease` call doesn't reuse a value.
6. `GreaseSlotsDoNotCorrelateWithSeed` — for seeds 0–1023 in order, the GREASE cipher value must NOT be linearly correlated with the seed value (i.e., it doesn't simply increment)

---

### Phase 12a — Cross-Platform Contamination Tests at 1024 Iterations

**File**: `test/stealth/test_tls_corpus_cross_platform_contamination_1k.cpp`  
**Test suite**: `CrossPlatformContamination1k`  
**Purpose**: Ensure that no profile from one OS/platform can be mistaken for a profile from a different OS/platform. These tests simulate a DPI classifier that has seen real browser traffic for each platform and checks whether our generated traffic could be mis-classified as the WRONG platform.

Tests (N=1024 unless noted):

**Linux Desktop vs iOS:**
1. `LinuxDesktopChromeNeverMatchesAppleTlsSet` — Chrome133 extension set (16 exts + ALPS + ECH) must never equal the 13-extension Apple TLS family set. Assert count ≠ 13 AND assert 0x44CD present (ALPS), which Apple TLS never has.
2. `LinuxDesktopFirefoxNeverMatchesAppleTlsSet` — Firefox148 (Linux) extension set has 17 members; Apple TLS has 13. Assert set-inequality across 1024 runs.
3. `LinuxDesktopChromeNeverMatchesIosChromiumFamily` — Chrome133 (Linux) extension set must differ from iOS Chromium 26.1/26.3 family in ordering policy (Linux uses shuffle; iOS Chromium used shuffle too, but has PSK as extra member). Assert: Linux Chrome fresh connections never have 0x0029, which iOS Chromium captures had.

**Linux Desktop vs Android:**
4. `LinuxDesktopChromeExtensionSetDiffersFromAndroidNoAlpsChrome` — Chrome133 (16 exts, ALPS) must never match Android Chromium-no-ALPS (15 exts, no ALPS) in extension SET. Assert: 0x44CD always in Linux Chrome, never in Android no-ALPS Chrome.
5. `LinuxDesktopFirefoxNeverMatchesAndroidNoAlps` — Firefox148 extension set (17 exts, 0x0022, 0x001C) must never match Android no-ALPS Chrome set (15 exts, no 0x0022, no 0x001C).

**iOS Apple TLS vs Android:**
6. `IosAppleTlsSetNeverMatchesAndroidChromiumNoAlpsSet` — Apple TLS (13 exts, no ECH, no GREASE) must differ from Android no-ALPS Chrome (15 exts, ECH present, GREASE in cipher suite). Test both count and ECH presence.
7. `IosAppleTlsSetNeverMatchesAndroidChromiumAlpsSet` — Apple TLS (13 exts) must differ from Brave/Yandex-Samsung Android (17 exts, ALPS, ECH, GREASE).

**iOS Apple TLS vs macOS Firefox:**
8. `IosAppleTlsSetNeverMatchesMacosFirefoxSet` — Apple TLS (13 exts) must differ from macOS Firefox (16 exts, 0x0022, 0x001C, ECH at 399 bytes). Assert count inequality.

**Android ALPS vs Android no-ALPS (intra-platform discrimination):**
9. `AndroidChromiumAlpsVsNoAlpsAreDifferentFamilies1024Runs` — Brave Android (via Chrome133 proxy) always has 0x44CD; Android11_OkHttp_Advisory never has 0x44CD. Using both profiles across 1024 runs each, assert they produce non-overlapping fingerprints on the ALPS dimension.
10. `YandexAndroidDeviceDivergence` — Using ReviewedClientHelloFixtures.h constants: `yandex26_3_4_android16_samsung` extension set (17 exts, ALPS) must differ from `yandex26_3_4_128_oxygenos16_oneplus13` extension set (15 exts, no ALPS). The same browser on a different Android device is a different corpus family.

**iOS version divergence (Chrome platform-version regression):**
11. `ChromeIos261FamilyHasAlps_ChomeIos264FamilyDoesNot` — Using ReviewedClientHelloFixtures.h: `chrome146_0_7680_151_ios26_1` extension set contains 0x44CD; `chrome147_0_7727_47_ios26_4_a` does NOT contain 0x44CD. Assert set-symmetric-difference includes 0x44CD. This test exists purely as a fixture-data regression guard — it passes iff the fixtures correctly encode the iOS 26.1 vs 26.4 behavioral difference.
12. `ChromeIos261FamilyHasEch_ChromeIos264FamilyDoesNot` — `chrome146_0_7680_151_ios26_1` contains 0xFE0D; `chrome147_0_7727_47_ios26_4_a` does NOT. Same fixture-regression guard.

**macOS vs Linux (same browser, different OS):**
13. `MacosFirefoxAndLinuxFirefoxAreDistinctFamilies` — macOS Firefox 149 extension set differs from Linux Firefox 148/149 set by exactly `{0x0023}` (macOS missing session_ticket). Using reviewer fixture constants, assert set-symmetric-difference == `{0x0023}`. Passes only if fixture data correctly reflects the macOS behavior.
14. `MacosFirefoxEchPayloadSizeDiffersFromLinuxFirefoxEchPayload` — macOS ECH payload (399 bytes, if profile is ever implemented) vs Linux ECH payload (239 bytes). Uses fixture data to verify constants are distinct.

**Safari/macOS uTLS profile contamination guard:**
15. `Safari26_3ProfileNeverMatchesIosAppleTlsProfileByCipherCount` — `BrowserProfile::Safari26_3` is a uTLS snapshot that may have different cipher counts or PQ extensions vs the real Apple TLS captures from iOS. Assert Safari26_3 cipher set includes 0x11EC in supported_groups (it has PQ), while IOS14 profile does not (has_pq=false). This ensures the two profiles are not accidentally identical.
16. `WindowsDesktopFamiliesAddedAsSeparateContaminationTargets` — **future placeholder rule**: when Windows dumps arrive, add explicit contamination tests instead of widening any Linux/macOS desktop assertions to include Windows. Windows must be modeled as a separate platform axis.

---

### Phase 12b — CMakeLists.txt Update

**File**: `test/CMakeLists.txt`

Add all 15 new `.cpp` files plus `CorpusStatHelpers.h` (header needs no CMake entry) to the `run_all_tests` target. Follow existing pattern for adding cpp files.

New files to add (in addition to those present before this plan):
- `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp`
- `test/stealth/test_tls_corpus_chrome_grease_uniformity_1k.cpp`
- `test/stealth/test_tls_corpus_chrome_ech_payload_uniformity_1k.cpp`
- `test/stealth/test_tls_corpus_chrome_permutation_position_1k.cpp`
- `test/stealth/test_tls_corpus_firefox_invariance_1k.cpp` (Phase 5A — Linux Desktop)
- `test/stealth/test_tls_corpus_firefox_macos_1k.cpp` (Phase 5B — macOS Desktop)
- `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp` (Phase 6a)
- `test/stealth/test_tls_corpus_ios_chromium_gap_1k.cpp` (Phase 6b)
- `test/stealth/test_tls_corpus_android_chromium_alps_1k.cpp` (Phase 6c)
- `test/stealth/test_tls_corpus_android_chromium_no_alps_1k.cpp` (Phase 6d)
- `test/stealth/test_tls_corpus_ja3_ja4_stability_1k.cpp`
- `test/stealth/test_tls_corpus_alps_type_consistency_1k.cpp`
- `test/stealth/test_tls_corpus_wire_size_distribution_1k.cpp`
- `test/stealth/test_tls_corpus_adversarial_dpi_1k.cpp`
- `test/stealth/test_tls_corpus_grease_slot_independence_1k.cpp`
- `test/stealth/test_tls_corpus_cross_platform_contamination_1k.cpp` (Phase 12a)

---

**Relevant files**

- `test/stealth/test_tls_capture_chrome144_differential.cpp` — pattern reference (per-seed structural checks)
- `test/stealth/test_tls_ja3_ja4_cross_validation.cpp` — JA3/JA4 computation helpers (migrate/share)
- `test/stealth/ReviewedClientHelloFixtures.h` — ground truth constants per browser+platform (cipher suites, key shares, ECH expectations, extension vectors)
- `test/stealth/FingerprintFixtures.h` — `kKnownTelegramJa3`, ALPS constants
- `test/stealth/MockRng.h`, `test/stealth/TlsHelloParsers.h`, `test/stealth/TestHelpers.h` — test infra
- `test/analysis/profiles_validation.json` — complete fixture corpus with `non_grease_extensions_without_padding` per platform+fixture
- `test/analysis/fixtures/clienthello/linux_desktop/` — Linux Desktop fixture JSONs
- `test/analysis/fixtures/clienthello/macos/` — macOS fixture JSONs (single entry: `firefox149_macos26_3`)
- `test/analysis/fixtures/clienthello/ios/` — iOS fixture JSONs (Apple TLS family + Chromium iOS)
- `test/analysis/fixtures/clienthello/android/` — Android fixture JSONs (Brave/Yandex-Samsung = ALPS; Chrome/Samsung/Yandex-OnePlus = no-ALPS)
- `test/analysis/fixtures/clienthello/windows_desktop/` — future Windows Desktop fixture JSONs; keep separate from Linux and macOS once added
- `test/stealth/test_tls_corpus_windows_chrome_1k.cpp` — reserved future Windows Chrome corpus file
- `test/stealth/test_tls_corpus_windows_firefox_1k.cpp` — reserved future Windows Firefox corpus file
- `test/stealth/test_tls_corpus_windows_edge_1k.cpp` — reserved future Windows Edge corpus file
- `test/stealth/test_tls_corpus_windows_cross_browser_1k.cpp` — reserved future Windows intra-platform contamination file
- `td/mtproto/stealth/TlsHelloBuilder.cpp` — `sample_ech_payload_length()`, `sample_padding_entropy_length()`, `init_grease()`, `shuffle_chrome_anchored_extensions()`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` — PROFILE_SPECS array (ALPS type, ECH flag, PQ flag, extension policy per profile)
- `test/CMakeLists.txt` — add all 16 new files

---

**Verification**

1. All 16 files compile: `cmake --build build --target run_all_tests --parallel 4`
2. Run tests: `ctest --test-dir build --output-on-failure -R "LinuxDesktop|Macos|IosApple|IosChromium|AndroidChromium|CrossPlatform|Uniformity1k|Invariance1k|Stability1k|Consistency1k|Distribution1k|Independence1k|AdversarialDpi"`
3. **Intentional failure scan**: run each test file in isolation first. Tests designed to find bugs should FAIL if the generation is incorrect — do NOT relax thresholds when tests fail. Instead, diagnose the underlying generator bug.
4. Manually inspect chi-squared / autocorrelation tests to confirm they catch a synthetic broken PRNG (seed with a mock RNG that always returns 0, should break tests 4/6/8 of Phase 2)
5. After Phase 6b and 6c/6d tests pass, verify that the documented **generator gaps** (iOS Chromium profile missing, Android Chromium-ALPS profile using Linux Chrome as proxy) are noted in a follow-up issue.

---

**Decisions / Scope boundaries**

- **Included**: C++ tests in `test/stealth/` only. No Python tests.
- **Excluded**: Tests for Session/Transport-layer behavior (those are in DRS/IPT test files), HMAC correctness (covered in `test_tls_hello_hmac_regression.cpp`), server-side analysis.
- **Ground truth source**: `ReviewedClientHelloFixtures.h` (from captured pcaps) is canonical per platform. `profiles_validation.json` fixtures section is cross-reference. When they disagree on SET, fixture JSON wins per the contamination_guard policy.
- **N=1024**: chosen as power-of-2 close to 1000, giving clean expected values for uniformity tests. Some individual sub-tests use N=100 or N=20 where noted.
- **PSK extension 0x0029**: Fresh-connection generator tests must assert 0x0029 is absent — documented in the Platform-OS Corpus Matrix section. `0x0029` in platform captures always indicates TLS session resumption, not a fresh connection.
- **DO NOT** treat failing tests as test bugs without first ruling out a generator bug. The tests are designed adversarially to expose real issues.
- **Generator gap policy**: Phases 6b and 6c document families where the current runtime profile set is incomplete (no iOS Chromium profile, no Android-Brave-family profile). Tests in those phases use the closest available profile or use fixture constants directly. The gap itself must be documented with a follow-up task, not silently accepted.
- **Same browser, different platform** is NOT the same fingerprint family. Yandex on Samsung ≠ Yandex on OnePlus. Chrome on iOS 26.1 ≠ Chrome on iOS 26.4. macOS Firefox ≠ Linux Firefox. Every such divergence is captured in Phase 12a.
- **Future Windows policy**: When Windows Desktop dumps are added, create Windows-only corpus phases and Windows-only ground-truth constants. Do not broaden any existing Linux/macOS tests into a generic `desktop` bucket.
- **Reserved Windows phase names**: Use `Phase W1`/`W2`/`W3`/`W4` for Windows Chrome / Firefox / Edge / intra-Windows contamination respectively. Do not renumber existing Linux/macOS/iOS/Android phases to make room for Windows later.
- **Reserved Windows fixture IDs**: Default to `chrome<major>_windows_desktop`, `firefox<major>_windows_desktop`, and `edge<major>_windows_desktop`. Only append OS-build suffixes when the corpus demonstrates a real family split that must stay separately testable.

---

**Further Considerations**

1. **Phase 0 header reuse vs. duplication**: `test_tls_ja3_ja4_cross_validation.cpp` already has inline JA3/JA4 helpers. The plan creates `CorpusStatHelpers.h` to consolidate them. The implementer should decide whether to move existing helpers there or duplicate — moving is cleaner but mechanical; duplicating risks divergence. Recommendation: create new header, reuse the functions by reference.

2. **Chi-squared test implementation**: the `GreaseSlotIndependence1k` chi-squared test requires a simple statistics implementation in the helper header. Keep it simple — compute observed/expected ratio per cell rather than a full Pearson χ² — this avoids a math library dependency while still detecting gross dependence.

3. **Android/iOS profile gap**: `BrowserProfile::IOS14` and `BrowserProfile::Android11_OkHttp_Advisory` are marked `Advisory` trust tier (no independent network provenance). Tests in Phase 6 are structural (extension set, no-ECH, no-ALP) rather than exact-order matches, because there are no verified network captures for these profiles yet. When network captures for these profiles are collected and promoted to `Verified`, these tests should be hardened to exact-order checks. 

Updated memory file [](file:///home/david_osipov/.vscode-server-insiders/data/User/workspaceStorage/3e2b1a506ef424385226d9763619d8d8/GitHub.copilot-chat/memory-tool/memories/NjNjM2JlYmItY2NlMC00YzMzLTg4MmUtZTgwNmNiOTY0OGI0/plan.md)

The plan is complete. Here's a brief summary of what was planned:

The plan creates **11 new C++ test files** + 1 shared helper header, all in stealth, running **1024 iterations each** (seeds 0–1023 via `MockRng`) to statistically validate fingerprint generator correctness against real browser captures. The tests are organized into phases designed to **expose generator bugs**, not confirm correctness:

- **Phase 0** — `CorpusStatHelpers.h`: shared frequency counters, distribution uniformity checks, JA3/JA4 helpers, canonical extension-set constants from fixture ground truth
- **Phases 1–4** — Chrome extension set fidelity, GREASE value uniformity (chi-square level), ECH payload length distribution (4 values × ~256 each), and permutation position coverage at 1024 seeds
- **Phase 5** — Firefox: extension ORDER invariance must hold in **every one** of 1024 runs (any permutation = bug)
- **Phase 6** — Safari/iOS/Android corpus: structural invariants for Advisory-tier profiles (no ECH, no ALPS, correct PQ presence)
- **Phase 7** — JA3/JA4 hash stability across 1024 runs per profile + cross-profile divergence + anti-Telegram-hash guard
- **Phase 8** — ALPS type consistency: Chrome133 ↔ `0x44CD`, Chrome131 ↔ `0x4469`, never cross-contaminated
- **Phase 9** — Wire size distribution: Chrome ECH produces exactly 4 distinct sizes, ECH-disabled produces ≥7, Firefox produces exactly 1
- **Phase 10** — DPI adversarial corpus: session ID randomness, client_random entropy, route-aware ECH on/off at scale, proxy ALPN discipline, autocorrelation checks
- **Phase 11** — GREASE slot statistical independence: pairwise GREASE values must not be correlated across slots

The key principle: **tests that fail reveal real bugs** in the generator. The PSK/`0x0029` boundary, the `RenegotiationInfo` tail-anchor regression (V7), ECH route-aware blocking, and ALPS type correctness are all adversarially covered at N=1024. 

