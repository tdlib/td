<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Corpus Statistical Validation Plan (Wave 2)

**Date:** 2026-04-11  
**Extends:** `docs/Plans/fingerprints_hardcore_tests.md`  
**Primary scope:** TLS fingerprint generation, runtime profile selection, reviewed fixture ingestion, ClientHello/ServerHello corroboration, and release-gating evidence for browser and OS impersonation  
**Method:** **TDD only** — write red tests first, fix code second, never relax a failing test until a corpus defect is proven  
**Threat model:** high-budget state DPI doing multi-connection, multi-feature, cross-handshake correlation under ECH blocking and QUIC blocking constraints

---

## 0. Why this second plan exists

The first hardcore plan was the correct first wave and most of that surface is now present in the repository: 1k corpus suites, adversarial tests, light fuzz, stress tests, cross-platform contamination tests, and ServerHello smoke validation are already in place.

That is not yet the same thing as release-grade proof that every active fingerprint family is fully aligned with real browser traffic across OSs, browser versions, devices, routes, and handshake counterparts. The remaining risk is more specific:

1. **Corpus breadth risk** — many assertions still derive from a small number of reviewed captures per family.
2. **Independence risk** — some families still need more independent corroboration from different contributors, sessions, networks, and dates.
3. **Trust-tier risk** — in `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`, some active profiles still carry advisory fixture metadata (`Safari26_3`, `IOS14`, `Android11_OkHttp_Advisory`) instead of fully network-derived release-grade backing.
4. **Platform gap risk** — `test/analysis/profiles_validation.json` currently covers Android, iOS, Linux desktop, and macOS, but not Windows desktop.
5. **Statistical proof gap** — `test/stealth/ReviewedClientHelloFixtures.h` is a generated reviewed summary, but there is not yet a dedicated reviewed multi-dump statistical baseline artifact for every family.
6. **Full-handshake correlation risk** — the repository already has ServerHello smoke checks, but the next bar is stronger: pair the generated ClientHello families with observed ServerHello families and first-flight layouts as a release gate.
7. **CI efficiency risk** — the existing 1k corpus suites run 228 tests at 1024 iterations each, taking ~2 minutes of pure CPU. Many of those iterations test deterministic properties that are identical on every seed, wasting CI budget that could be spent on genuinely statistical validation against real dump fixtures.
8. **Fixture grounding gap** — the existing 1k tests verify the generator's internal consistency (extension sets, GREASE uniformity, wire size distribution), but the critical question — "does the generated output match what real browsers actually produce on the wire?" — is answered only via hand-picked reference values in `test/stealth/CorpusStatHelpers.h` and `test/stealth/FingerprintFixtures.h`, not via systematic statistical comparison against the full reviewed fixture corpus extracted from `docs/Samples/Traffic dumps/`.

No finite corpus can mathematically prove future traffic for all time. The practical bar in this plan is stronger and operationally release-grade: every active fingerprint family must be backed by independent real dumps, reviewed structural baselines, generator-envelope containment, and adversarial regression gates that fail closed. Distributional fidelity gates activate progressively as the corpus grows.

### 0.0.1 Current corpus inventory and tier projections

**Corpus snapshot (2026-04-11):** 111 ClientHello fixtures, 106 ServerHello fixtures, across 5 platforms.

| Platform | Fixtures | Families (approx) | Richest family (n) | Projected max tier with current data |
|---|---|---|---|---|
| Android | 29 | ~12 | 4–5 (Chrome 146/147) | Tier 2 for Chrome; Tier 1 for others |
| iOS | 21 | ~10 | 3–4 (Safari 26.x) | Tier 2 for Safari; Tier 1 for others |
| Linux desktop | 16 | ~5 | 4–5 (Chrome 144–147) | Tier 2 for Chrome; Tier 2 for Firefox |
| macOS | 14 | ~6 | 4 (Firefox 149) | Tier 2 for Firefox; Tier 1–2 for others |
| Windows | 31 | ~15 | 6 (Chrome 146) | Tier 2 for Chrome; Tier 1 for Firefox (not yet in runtime profiles) |

**No family currently reaches Tier 3 (n ≥ 15 per family-lane).** Distributional tests are therefore diagnostic-only today. The plan is designed so that Tier 2 structural gates provide meaningful release evidence now, and Tier 3+ gates activate automatically as contributions arrive.

**Corpus growth model:** This plan does not include a data collection campaign. Contributors are welcome to add captures per the admission protocol in Section 6. When new captures arrive:
1. The baseline generator re-evaluates tier assignments automatically.
2. Suites that were diagnostic-only may become release-gating.
3. No code changes are needed to enable higher tiers — the generated baselines header carries tier metadata, and test suites conditionally enable tier-gated assertions.

### 0.0.2 What is actionable today vs what waits for more data

| Component | Actionable now | Waits for more data |
|---|---|---|
| Iteration tier refactoring (Section 0.1) | Yes — purely mechanical | — |
| Corpus intake hardening (Workstream A) | Yes — pipeline security | — |
| Deterministic rule verifiers (Workstream B) | Yes — upstream-rule-pinned, zero corpus dependency | — |
| Generator-envelope containment (Tier 1+) | Yes — works at n = 1 | — |
| Structural multi-dump suites (Tier 2 gates) | Yes — most families qualify for Tier 2 now | — |
| Joint state set-membership catalogs | Yes — set-membership works at any n | — |
| Cross-family disjointness | Yes — invariant-based, works at n ≥ 1 per family | — |
| Monte Carlo consistency and boundary falsification | Yes — tests generator, not corpus | — |
| Handshake acceptance and ServerHello corroboration | Yes — correctness checks, not statistical | — |
| One-sample distributional tests (Tier 3) | Diagnostic-only now | n ≥ 15 per family-lane |
| Classifier-style adversarial gates (Tier 3) | Diagnostic-only now | n ≥ 15 per family-lane |
| TOST equivalence framework (Tier 4) | Not yet | n ≥ 200 per family-lane |

### 0.1 Audit of existing 1k corpus test suite

The existing Wave 1 test surface consists of **22 files, 228 TEST() macros, ~3600 lines** under `test/stealth/test_tls_corpus_*_1k.cpp`. Every test iterates 1024 seeds. Measured runtime is ~2 minutes total at ~300ms per test.

#### Reference value provenance

The reference constants used by the 1k tests actually do trace to real traffic captures through a two-stage pipeline:

1. `test/stealth/FingerprintFixtures.h` — hand-curated structural constants (extension type codes, group IDs, key lengths). These are protocol-level facts, not capture-derived.
2. `test/stealth/CorpusStatHelpers.h` — defines comparison sets like `kChrome133EchExtensionSet` and `kFirefox148ExtensionOrder`. These are derived from `ReviewedClientHelloFixtures.h` constants (e.g., `chrome146_75_linux_desktopNonGreaseExtensionsWithoutPadding`, `firefox148_linux_desktopNonGreaseExtensionsWithoutPadding`).
3. `test/stealth/ReviewedClientHelloFixtures.h` — generated by `test/analysis/merge_client_hello_fixture_summary.py` from the reviewed JSON fixture files in `test/analysis/fixtures/clienthello/`.

So the provenance chain is intact: real PCAPs → JSON fixtures → generated C++ header → comparison constants. However, each comparison constant is derived from **one specific reviewed fixture** (e.g., `chrome146_75_linux_desktop`), not from the full set of reviewed fixtures for that family. If a family has 18 captured fixtures (Linux Chrome), the tests compare against one of them and ignore the other 17. This is the key limitation Wave 2 must close.

#### Per-file value assessment

| File | Tests | Value verdict | Reasoning |
|---|---|---|---|
| `firefox_invariance_1k` | 15 | **Rewrite as quick-tier** | Tests deterministic properties (extension order, cipher suite order, no-GREASE, ECH position, delegated credentials presence) that are identical for every seed. 1024 iterations confirm the same fact 1024 times. 3 seeds catch any regression. But the invariants themselves are valuable — keep assertions, drop iteration count. |
| `firefox_macos_1k` | 5 | **Rewrite as quick-tier** | Same pattern as firefox_invariance: deterministic extension order and cipher suite checks. Small file, low complexity. Keep assertions, drop iterations. |
| `safari26_3_invariance_1k` | 12 | **Rewrite as quick-tier** | Deterministic: cipher suites, extension set, supported groups, key shares, ALPS absence, compress certificate. Compared against `ReviewedClientHelloFixtures.h` constants from real Safari captures. Assertions are well-chosen. Drop iterations. |
| `ios_apple_tls_1k` | 12 | **Rewrite as quick-tier** | Deterministic: extension set, cipher suites, supported groups against real iOS fixture. Also tests ECH absence and ALPS absence for Apple TLS. Good assertions. Drop iterations. |
| `ios_chromium_gap_1k` | 4 | **Rewrite as quick-tier** | Deterministic: extension set differences between iOS Chromium and Apple TLS. Good gap documentation. Drop iterations. |
| `fixed_mobile_profile_invariance_1k` | 12 | **Rewrite as quick-tier** | Deterministic: tests Android and Yandex profiles against reviewed fixture constants. Good coverage. Drop iterations. |
| `alps_type_consistency_1k` | 12 | **Rewrite as quick-tier** | Deterministic: ALPS type presence/absence by profile family. Correct but iterating 1024 times proves nothing extra. |
| `chrome_extension_set_1k` | 11 | **Rewrite as quick-tier** | Deterministic: extension set membership, ECH extension presence/absence. Set comparison is seed-independent. |
| `cross_platform_contamination_1k` | 7 | **Keep as-is** | Already fast (~0ms per test). No builder calls — pure fixture-array comparisons. Good value: catches accidental cross-family fixture corruption. No change needed. |
| `cross_platform_contamination_extended_1k` | 13 | **Keep as-is** | Same as above. Fixture-only comparisons. Already fast. |
| `android_chromium_alps_1k` | 7 | **Rewrite as quick-tier** | Deterministic: extension set and ALPS type for Android Chrome with ALPS. Drop iterations. |
| `android_chromium_no_alps_1k` | 7 | **Rewrite as quick-tier** | Deterministic: extension set and ALPS type absence for Android Chrome without ALPS. Drop iterations. |
| `chrome_grease_uniformity_1k` | 10 | **Keep, valuable** | Statistical: GREASE value distribution uniformity, dominance thresholds. 1024 iterations genuinely needed — 10 tests × 16 possible GREASE values means 64 iterations is borderline for detecting a 30% dominance bias. Could reduce to 256 for PR gate but 1024 is proper for nightly. |
| `grease_slot_independence_1k` | 7 | **Keep, valuable** | Statistical: GREASE slot cross-correlation (<25% collision rate), pairwise coverage (≥64 distinct pairs). 1024 iterations needed for the chi-squared-like assertions to be reliable. |
| `grease_autocorrelation_adversarial_1k` | 8 | **Keep, nightly only** | Autocorrelation at lags 1–512 and Pearson correlation of GREASE with seed index. Requires full 1024-length series — cutting iterations destroys the lag analysis. Genuinely valuable but nightly-only. |
| `chrome_ech_payload_uniformity_1k` | 13 | **Keep, valuable** | Statistical: 4-value ECH payload length distribution, uniformity, boundary checks. 1024 iterations needed for reliable uniformity assessment across 4 discrete values. |
| `chrome_permutation_position_1k` | 11 | **Keep, valuable** | Statistical: extension position diversity across seeds. Tests that ≥1000 of 1024 hellos have distinct permutations, and that individual extensions appear in ≥8 positions. Genuine permutation diversity check. |
| `wire_size_distribution_1k` | 10 | **Keep, valuable** | Statistical: wire size distribution bounds, distinct size counts, ECH vs non-ECH separation. 1024 iterations needed for reliable tail bounds. |
| `ja3_ja4_stability_1k` | 18 | **Mixed** | 7 tests check deterministic JA3/JA4 identity for Firefox/Safari (quick-tier). 11 tests check JA3 diversity ≥256 for Chrome, non-overlap across profiles, and stability for Telegram's known JA3 (statistical, needs ≥256 seeds). Split by assertion type. |
| `adversarial_dpi_1k` | 11 | **Mixed** | 4 tests check deterministic properties (session ID size, legacy version) across all profiles — quick-tier. 5 tests check uniqueness of session ID, client random, X25519, PQ, ECH enc across 1024 seeds — genuine entropy/uniqueness tests, nightly-only. 2 tests check ECH route policy at scale (deterministic per seed, quick-tier). |
| `hmac_timestamp_adversarial_1k` | 17 | **Mixed** | 5 tests check deterministic HMAC properties (determinism, timestamp embedding, bit-flip sensitivity) — quick-tier. 7 tests check entropy and uniqueness across seeds — nightly-only. 5 tests check boundary behavior (already use ≤64 iterations) — keep as-is. |
| `structural_key_material_stress_1k` | 25 | **Mixed, mostly rewrite** | 11 tests check deterministic structural TLS fields (record type, version, compression, etc.) across all profiles × 1024 seeds — pure waste, quick-tier. 4 tests check key material validity (X25519, ML-KEM coefficients) — fuzz-like value at ~64 seeds, overkill at 1024. 4 tests check key uniqueness — genuine entropy tests, nightly-only. 3 tests exercise pathological RNG — already use ≤64 seeds, keep as-is. 3 tests check ECH structural fields — quick-tier. |

#### Summary by verdict

| Verdict | Files | Tests | Action |
|---|---|---|---|
| **Rewrite as quick-tier** (3 seeds) | 12 files | ~93 tests | Drop `kCorpusIterations` to 3. Keep all assertions unchanged. |
| **Keep, statistically valuable** (1024 seeds) | 5 files | ~49 tests | Keep at full iteration count. Move to nightly gate. Run at `kSpotIterations` (= 64) in PR gate. |
| **Keep as-is** (already fast) | 2 files | ~20 tests | No iteration change needed — fixture-only comparisons. |
| **Mixed** (split by assertion type within file) | 4 files | ~71 tests | Split each test's iteration count by assertion type: quick-tier for deterministic, spot-tier for statistical, full-tier for entropy/autocorrelation. |
| **Keep, nightly only** | 1 file | ~8 tests | Autocorrelation analysis requires full series. Move entirely to nightly gate. |
| **Delete** | 0 files | 0 tests | — |

#### Key finding: no files should be deleted

None of the 22 files test something that is actually useless. Every file tests a property that matters for fingerprint fidelity. The waste is purely in **iteration count**: ~93 tests re-verify deterministic properties 1024 times when 3 seeds suffice. The assertions themselves are correct and trace to real capture fixtures through the `ReviewedClientHelloFixtures.h` pipeline.

The correct action is to right-size iterations per assertion type, not to delete tests. The new multi-dump comparison suites (Workstream C) will add the missing *breadth* dimension — comparing against all fixtures in a family rather than one singleton — but the existing depth tests (GREASE uniformity, permutation diversity, autocorrelation, entropy) have no equivalent in the planned multi-dump suites and must be preserved.

#### Iteration tier strategy for Wave 2

Rather than running all tests at 1024 iterations uniformly, Wave 2 introduces three iteration tiers gated by assertion type:

| Tier | Iterations | When to use | Gate |
|---|---|---|---|
| **Quick** | 3 (seeds 0, 1, `UINT64_MAX`) | Exact invariants, deterministic rule checks, fixture-only comparisons | PR gate |
| **Spot** | 64 | Statistical spot-checks where 64 samples already provide >99% probability of detecting a 10+ percentage point shift in a 4-value distribution | PR gate |
| **Full** | 1024+ | Autocorrelation analysis, entropy/uniqueness validation, generator consistency Monte Carlo, distributional promotion batteries | Nightly gate only |

Implementation mechanism:

1. Define `kQuickIterations = 3`, `kSpotIterations = 64`, `kFullIterations = 1024` as constants in `test/stealth/CorpusIterationTiers.h`.
2. Each test uses the tier appropriate to its assertion type per the per-file table above.
3. The nightly gate sets `TD_NIGHTLY_CORPUS=1` as an environment variable; tests that want the full tier check this at runtime and fall back to the spot tier when absent.
4. Existing 1k test files are updated in place — no new files needed for the tier split itself.
5. For the 4 mixed files (`ja3_ja4_stability`, `adversarial_dpi`, `hmac_timestamp_adversarial`, `structural_key_material_stress`), individual tests within the same file use different tier constants based on their assertion type.

Expected PR gate impact: **~12 seconds instead of ~120 seconds** for the corpus test slice, with zero loss of detection power for structural and invariant assertions.

The full-tier tests remain mandatory in the nightly gate. Skipping them in PR is acceptable because:
- Exact invariants catch structural regressions at any iteration count.
- Statistical regressions affect distributional properties that change slowly (code changes, not data changes) and are caught within 24 hours by the nightly run.
- A developer who wants immediate feedback can set `TD_NIGHTLY_CORPUS=1` locally.

---

## 1. Objective

Turn the current fingerprint validation system into a **release-blocking, corpus-driven statistical verification pipeline** that answers one question for every active browser and OS fingerprint family:

> Do we have enough independent real traffic evidence to assert that the generated TLS fingerprint is indistinguishable from the intended browser/OS family and not a cross-family hybrid that would expose Telegram?

The evidence chain is: real traffic captures (`docs/Samples/Traffic dumps/`) → extracted and reviewed JSON fixtures (`test/analysis/fixtures/clienthello/`, `test/analysis/fixtures/serverhello/`) → reviewed C++ fixture constants (`test/stealth/ReviewedClientHelloFixtures.h`) → runtime comparison tests that generate ClientHellos via the builder and assert statistical alignment against the full reviewed fixture set per `(family_id, route_lane)`.

This plan is successful only if the answer becomes **yes** for every active family and **not release-grade yet** for every family that still lacks sufficient evidence.

### Statistical methodology contract

No finite corpus proves future traffic for all time. Wave 2 therefore makes bounded, falsifiable claims rather than absolute proof claims.

**Fundamental asymmetry:** The generator can be sampled to arbitrary precision at near-zero cost, so its distribution is effectively known. The real browser population is observed only through the limited capture corpus (currently 111 fixtures, 1–5 per typical family-lane). All distributional comparisons must respect this asymmetry: they are one-sample problems (testing whether the small real sample is consistent with the known generator distribution), not symmetric two-sample problems.

Every release gate must declare which of the following claim classes it is using:

| Claim class | Null hypothesis or rule under test | Failure condition | Corpus requirement | Gate type |
|---|---|---|---|---|
| Exact invariant | The generated output never violates invariant `I` for `(family_id, route_lane)` | A single counterexample exists | Tier 1+ (n ≥ 1) | Hard fail |
| Deterministic rule | Given the same inputs and pinned upstream rule specification, the generated field is legal | A single impossible output exists | Tier 1+ (n ≥ 1) | Hard fail |
| Generator-envelope containment | Every observed real capture value falls within the generator's output space (across N seeds) | A real capture value that the generator never produces | Tier 1+ (n ≥ 1) | Hard fail |
| Empirical-envelope coverage | Generated outputs stay within the min/max/set-of-observed values across all reviewed captures for the family-lane | A generated value outside the reviewed empirical envelope | Tier 2+ (n ≥ 3) | Hard fail |
| Set-membership coverage | The generator covers every distinct observed value in the reviewed corpus (e.g., every observed extension set, every observed wire-length bucket) | A reviewed real value with zero coverage probability in the generator | Tier 2+ (n ≥ 3) | Nightly and promotion |
| Cross-family contamination | Generated outputs for family X stay inside the reviewed invariant set for X and outside disjoint invariants for family Y | Disjoint invariant gate or distance gate is violated | Tier 2+ (n ≥ 3) | Nightly and promotion |
| Distributional fidelity | Real captures are consistent with being drawn from the generator's known distribution | One-sample exact permutation test or bootstrap CI rejects at configured alpha | Tier 3+ (n ≥ 15) | Nightly and promotion only |
| Classifier-style distinguishability | A simple interpretable classifier cannot separate real from generated at the per-connection level | AUC lower 95% bootstrap CI exceeds threshold | Tier 3+ (n ≥ 15) | Nightly and promotion only |

Required metadata in every distributional or classifier-style test file docstring:

1. Null hypothesis `H0`
2. Alternative hypothesis `H1`
3. Significance level `alpha`
4. Achieved power estimate given the actual corpus size, or "power not estimated — diagnostic only" if below Tier 3
5. DPI-relevant effect size being detected
6. Actual sample size used and the tier it qualifies for
7. Multiple-testing correction policy

Exact-invariant, deterministic-rule, and containment/envelope suites are not treated as null-hypothesis significance tests. They are legality checks and fail on the first counterexample.

---

## 2. In-scope code paths

This plan covers all fingerprint-affecting code paths, not the entire repository.

### Runtime fingerprint generation and selection

- `td/mtproto/stealth/TlsHelloBuilder.cpp`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
- Supporting runtime profile selection, route/ECH decision, and RNG seams used by stealth fingerprint generation

### Reviewed corpus and analysis pipeline

- `test/analysis/extract_client_hello_fixtures.py`
- `test/analysis/merge_client_hello_fixture_summary.py`
- `test/analysis/extract_server_hello_fixtures.py`
- `test/analysis/generate_server_hello_fixture_corpus.py`
- `test/analysis/run_corpus_smoke.py`
- `test/analysis/check_server_hello_matrix.py`
- `test/analysis/profiles_validation.json`
- `test/analysis/fixtures/clienthello/**`
- `test/analysis/fixtures/serverhello/**`

### Reviewed generated test data

- `test/stealth/ReviewedClientHelloFixtures.h`
- New reviewed statistical baseline artifacts introduced by this plan

### Runtime differential and regression suites

- Existing `test/stealth/test_tls_corpus_*`
- New per-family statistical comparison suites required by this plan

---

## 3. Non-negotiable rules

1. **TDD only.** First create failing tests, then fix code.
2. **Red tests are evidence, not noise.** Do not weaken them unless the fixture or corpus is proven wrong.
3. **All tests must live in separate files.** No inline one-off tests inside unrelated files.
4. **Fail closed.** Unknown family, unknown route, unknown metadata, mixed trust tiers, mixed source hashes, or mixed client/server pairings must block promotion.
5. **No runtime JSON parsing for release behavior.** Runtime code stays separate from offline analysis. Analysis outputs reviewed generated artifacts; runtime consumes code, not raw corpus files.
6. **Deterministic structure over convenience.** Keep builder logic, registry logic, corpus logic, and comparison logic in separate seams.
7. **No manual edits to generated review artifacts.** Regenerate from checked-in reviewed inputs only.
8. **Do not silently merge surprising new traffic into an existing family.** New cluster means new provisional family until proven equivalent.

---

## 4. ASVS L2 and secure corpus handling requirements

This work must align with OWASP ASVS L2 where it applies to offline corpus intake, runtime routing, and release gating.

### 4.1 Untrusted input handling

Treat PCAPs, extracted JSON, scenario metadata, and contributor-provided dumps as untrusted input.

Required behavior:

1. Reject path traversal, absolute path abuse outside approved roots, and poisoned relative paths.
2. Reject malformed JSON, duplicate fixture IDs, duplicate scenario IDs, inconsistent family IDs, invalid enum values, oversized fields, truncated handshakes, impossible lengths, and mixed parser versions.
3. Reject source hash mismatches, route mode mismatches, mixed authoritative and advisory artifacts in the same release-grade family, and mixed client/server families for the same capture source.
4. Reject unknown fixture families in release mode.
5. Reject promotion of any family that is backed only by advisory code samples or snapshots.

### 4.2 Integrity and provenance

Required behavior:

1. Every reviewed artifact must retain `source_path`, `source_sha256`, route mode, parser version, and family ID.
2. Promotion from provisional to release-grade requires independent corroboration, not repeated copies of the same capture.
3. Generated reviewed headers must be deterministic and must not allow string-content injection into C++ code generation.

### 4.3 Data minimization

Required behavior:

1. Store and compare handshake-only data needed for validation.
2. Avoid retaining unrelated user payloads, cookies, or application data from community captures.
3. Sanitize metadata fields before emission into generated headers and reports.

### 4.4 Fail-closed runtime decisions

Required behavior:

1. ECH decisions must remain route-aware and fail closed when the route is RU or otherwise blocked by policy.
2. Unknown platform/browser family selection must not silently fall through to a superficially similar release fingerprint.
3. Any profile that lacks release-grade corpus backing stays provisional and must not be treated as fully verified.

---

## 5. Current verified gaps to prioritize first

These are concrete gaps already visible from the current codebase and corpus layout.

### Gap A — active advisory-backed profiles

`td/mtproto/stealth/TlsHelloProfileRegistry.cpp` still marks these fixture sources as advisory:

- `Safari26_3`
- `IOS14`
- `Android11_OkHttp_Advisory`

Wave 2 must either:

1. Promote them to network-derived, reviewed release-grade families with sufficient real captures, or
2. Replace them with new verified profiles, or
3. Explicitly keep them non-release-grade until sufficient evidence exists

The final state must not pretend an advisory family is fully verified.

### Gap B — no Windows desktop corpus yet

`test/analysis/profiles_validation.json` currently sources Android, iOS, Linux desktop, and macOS captures. Windows desktop must not be inferred from Linux or macOS assumptions.

Note: `test/analysis/fixtures/clienthello/windows/` already contains 31 extracted ClientHello fixtures. These captures exist but have not yet been wired into `profiles_validation.json` families or backed by runtime profiles. Wave 2 must either formalize Windows families or document that they remain out of release scope.

### Gap C — reviewed statistical baselines are missing

`test/stealth/ReviewedClientHelloFixtures.h` is useful, but it is still a reviewed summary rather than a dedicated statistical baseline artifact built from many independent dumps per family.

### Gap D — handshake acceptance and ServerHello corroboration are not yet formalized separately

The repository already has ServerHello extraction and smoke validation, but the next bar is stronger and must be split cleanly:

1. **Handshake acceptance** — generated ClientHellos must successfully complete the expected handshake path. This is correctness.
2. **ServerHello and first-flight corroboration** — the resulting server-side reply and first-flight layout should remain consistent with the reviewed corpus for that lane. This is secondary evidence, not primary proof of ClientHello fidelity.

### Gap E — existing 1k test suite compares against singleton fixtures, not family-level corpus

The 228 existing 1k tests have valid provenance: reference constants trace through `CorpusStatHelpers.h` → `ReviewedClientHelloFixtures.h` → real dump JSON fixtures. However, each comparison constant is derived from **one specific reviewed fixture** (e.g., `chrome146_75_linux_desktop` for the Chrome extension set), not from the full set of reviewed fixtures for that family. If Linux Chrome has 18 captured fixtures across Chrome 144–147 and Chromium 146, the tests compare against one of them and ignore the other 17.

Additionally, as audited in Section 0.1:

- ~93 tests iterate 1024 times to confirm deterministic properties that are identical for every seed — wasting CI budget without adding detection power.
- ~49 tests are genuinely statistical and benefit from high iteration counts, but should be gated as nightly-only in the PR workflow.
- No tests should be deleted — all 228 test valid properties, but iterations must be right-sized.

Closing this gap requires:
1. Right-sizing existing 1k test iterations per the tier strategy in Section 0.1 (no files deleted, all assertions preserved).
2. Building the new multi-dump structural and statistical comparison suites (Workstream C) that compare generated output against **all reviewed fixtures per family-lane**, using tier-gated assertion strategies: structural checks (Tier 1+, all families), envelope and coverage checks (Tier 2+, families with ≥ 3 captures), and distributional tests (Tier 3+, families with ≥ 15 captures).
3. The multi-dump suites are the new primary release evidence; the existing 1k tests become regression guards for generator internals (GREASE quality, permutation diversity, entropy, structural validity).

---

## 6. Corpus admission protocol for new community dumps

Every new dump must follow the same pipeline. No exceptions.

1. Extract ClientHello artifacts into `test/analysis/fixtures/clienthello/**`.
2. Regenerate matching ServerHello artifacts into `test/analysis/fixtures/serverhello/**`.
3. Run smoke validation against `test/analysis/profiles_validation.json`.
4. Cluster the new artifacts against reviewed families.
5. If the new artifact falls outside the reviewed family envelope, create a new provisional family instead of widening the old one silently.
6. Review the diff in generated summaries and statistical baselines.
7. Only then promote or refine runtime expectations.

### Route-lane stratification

All corpus evidence, baselines, and promotion decisions are tracked per `(family_id, route_lane)`, not per family in the abstract.

### Temporal staleness policy

Browser fingerprints drift with auto-updates. A family-lane whose most recent authoritative capture is older than 90 days is flagged as **potentially stale** in nightly reports. A family-lane whose most recent capture is older than 180 days is automatically downgraded to the next lower evidence tier for distributional promotion purposes (Tier 3 → Tier 2 behavior, Tier 2 → structural-only). Exact-invariant and deterministic-rule gates are not affected by staleness because they test structural legality, not distributional currency.

Staleness downgrade is lifted immediately when a new authoritative capture within the 90-day window is admitted.

**Rapid-response protocol for major TLS stack changes:** When a major browser TLS stack change is detected (new extension added or removed, new named group, ALPS version change, ECH behavior change, PQ algorithm change), all affected families are **immediately downgraded to Tier 0 (Advisory)** until new captures confirm the generator has been updated to match. This is a circuit breaker: it prevents the system from claiming release-grade fidelity for a family whose real-world behavior has changed in ways the corpus hasn't yet captured. The downgrade is lifted per-family as new captures for the updated browser version are admitted and the family re-qualifies for Tier 1+.

### Capture-environment declaration

Contributors must declare the capture environment alongside provenance metadata. Required fields:

1. Whether the capture was made through a VPN, corporate proxy, TLS-intercepting middlebox, or other network intermediary
2. Whether the device had any system-wide certificate overrides or custom root CAs installed

Captures made through known TLS-modifying intermediaries (corporate TLS inspection proxies, debugging proxies like mitmproxy or Fiddler with TLS interception enabled) are excluded from the authoritative corpus. VPN captures are accepted only if the VPN is documented not to modify TLS ClientHello content; otherwise they are flagged as advisory.

This prevents non-representative fingerprints from entering the reviewed corpus as authoritative evidence.

Minimum route lanes:

1. `RU_BLOCKED_OR_FAIL_CLOSED`
2. `NON_RU_ECH_ENABLED`
3. `NON_RU_ECH_DISABLED`
4. `UNKNOWN_FAIL_CLOSED` when runtime policy can emit a distinct unknown-route fingerprint form

Rules:

1. Never merge RU and non-RU captures into one statistical baseline.
2. Never merge ECH-enabled and ECH-disabled captures into one statistical baseline.
3. A family may be release-grade in one route lane and provisional in another.

### Operational definition of independence

Different capture days on the same contributor, device, browser build, OS build, and network count as distinct sessions, not independent sources.

Two sources count as independent only if they differ on at least three of the following dimensions:

1. Contributor identity
2. Device hardware platform
3. OS minor version or build
4. Browser minor version or build
5. AS number or country of network egress

"Materially different network path" is not an accepted substitute: two ISPs in the same AS and country do not create independence on this dimension.

Capture date alone never creates independence.

### Evidence tiers

To avoid overclaiming confidence, every family must be tagged by evidence strength.

**Current corpus reality (2026-04-11):** The repository has **111 total ClientHello fixtures** across 5 platforms. Most browser families per platform have 1–5 captures, with the richest single family having approximately 11–16 fixtures. No family-lane has 200+ captures. The tier system below is designed to be **actionable with the corpus as it exists today** while progressively unlocking stronger evidence classes as community contributions grow.

| Tier | Meaning | Minimum evidence | Allowed gates |
|---|---|---|---|
| Tier 0 | Advisory-only | Zero authoritative network-derived captures; backed only by code samples (uTLS snapshots, advisory specifications) | Advisory test targets only. Cannot gate any release behavior. |
| Tier 1 | Anchored | 1–2 authoritative captures from at least 1 network-derived source | Exact-invariant gates, deterministic-rule gates, generator-envelope containment (Section 9.1). No distributional claims. This is where most families start. |
| Tier 2 | Corroborated | At least 3 authoritative captures per `(family_id, route_lane)` from at least 2 independent sources, at least 2 distinct sessions | All Tier 1 gates plus: empirical-envelope gates (min/max/set-of-observed), set-membership coverage checks, cross-family disjointness checks, one-sample containment tests (Section 9.2). Operational release-grade for structural fidelity. |
| Tier 3 | Distributional | At least 15 authoritative captures per `(family_id, route_lane)` from at least 3 independent sources, at least 2 browser minor versions unless a reviewed no-drift justification is recorded | All Tier 2 gates plus: permutation-based exact distributional tests, bootstrap confidence intervals on effect sizes, classifier-style adversarial gates with documented power limitations (Section 9.3). Release-grade with quantified uncertainty. |
| Tier 4 | High-confidence (future) | At least 200 authoritative captures per `(family_id, route_lane)` under the same independence and version-spanning rules as Tier 3 | Full asymptotic two-sample distributional battery with Benjamini-Hochberg correction and power ≥ 0.8 against DPI-relevant effect sizes. Preferred bar for long-term stable distributional promotion. |

**Threshold derivation.**

- **Tier 2 (n ≥ 3):** The minimum at which an empirical min/max envelope is non-degenerate (not defined by a single point) and a pairwise distance matrix has at least 3 entries for centroid estimation. Three independent captures from 2 sources also provide the minimum meaningful corroboration.
- **Tier 3 (n ≥ 15):** The minimum at which permutation-based exact tests have non-trivial power. For a two-sided permutation test comparing n_real = 15 real captures against n_gen = 1000 generated samples, the exact attainable significance level is approximately $\binom{1015}{15}^{-1}$, far below 0.05. The practical constraint is detection power: Monte Carlo simulation shows that with n_real = 15, a permutation test achieves power ≥ 0.70 against a 20 percentage-point shift in a binary feature — coarser than the Tier 4 target but sufficient to catch gross generator defects. For continuous features, bootstrap CIs on the Kolmogorov-Smirnov statistic at n = 15 have half-widths of approximately ±0.25, adequate for detecting large distributional mismatches (>16 bytes in wire length) but not subtle ones.
- **Tier 4 (n ≥ 200):** The original asymptotic power calculation: detecting a 10pp shift at alpha = 0.05 / power = 0.80 under a one-sample proportion test against the generator's known distribution requires approximately 197 samples. (Note: this is a one-sample test, not two-sample, because the generator distribution is fully known and can be sampled to arbitrary precision.)

**Key reframing: the generator distribution is known.** Unlike the original plan's two-sample framing, the generated-vs-real comparison is fundamentally asymmetric. The generator can be sampled millions of times at zero cost, so its distribution is effectively known. The only unknown is the real browser's distribution, observed through limited captures. All distributional tests should therefore be one-sample tests: "Is the observed real sample consistent with the generator's distribution?" or equivalently "Does the generator's output space cover the real browser's behavior?" This halves sample-size requirements compared to symmetric two-sample tests.

Rules:

1. Families at Tier 0 have no release gates and must not back any active fingerprint selection.
2. Families at Tier 1 are release-eligible for exact-invariant and structural gates only. This is the minimum bar for a new family to enter active rotation.
3. Families at Tier 2 are release-grade for structural fidelity. Most currently-active families should reach this tier with the existing corpus.
4. Families at Tier 3 unlock distributional evidence. This is achievable for the richest families (Linux Chrome, Android Chrome) with targeted contributor coordination.
5. Tier 4 is a future aspiration. No family is expected to reach it in the near term. When community contributions eventually grow the corpus, Tier 4 gates activate automatically.
6. Promotion is per route lane, not global per family.
7. If a family lacks multi-version evidence, it may not claim version-spanning coverage unless a reviewed no-drift justification is attached.

### Family and lane clustering policy

New captures are clustered against existing reviewed `(family_id, route_lane)` groups using a composite distance on mixed feature types.

Primary distance components:

1. Jaccard distance on extension-presence sets
2. Kendall tau distance on ordered non-GREASE extension sequences when order is meaningful
3. Normalized absolute difference on continuous length fields using robust scale estimates
4. Hamming distance on deterministic state flags such as ECH lane, ALPS type family, resumption markers, and key-share structure class

**Composite distance composition:** The four components are combined as a weighted sum with family-specific weights derived from the reviewed corpus:

1. Default weights when no family-specific calibration exists: Jaccard = 0.35, Kendall tau = 0.25, normalized length = 0.15, Hamming = 0.25.
2. Weights are re-estimated per reviewed family-lane by measuring within-family variance on each component and normalizing so that each component contributes proportional to its reciprocal within-family standard deviation. This prevents high-variance components from dominating.
3. The resulting scalar composite distance is used for admission decisions. When a family has fewer than 30 captures, the default weights are used without re-estimation.

**In-family envelope definition:** The reviewed empirical in-family envelope is the 97.5th percentile of pairwise composite distances among all reviewed captures within the family-lane. A new capture must have a composite distance to the family-lane centroid (median on each component) that does not exceed this threshold. The 97.5th percentile rather than the maximum is used to prevent a single outlying reviewed capture from inflating the envelope.

Secondary multivariate outlier rule:

1. Use Mahalanobis distance only on continuous co-varying fields and only when the reviewed family-lane has enough samples for a stable covariance estimate
2. Covariance stability criterion: the estimate is considered stable when the sample count N is at least 5p (where p is the number of continuous fields in the distance) **and** the condition number of the Pearson correlation matrix is below 100. If either condition is not met, the covariance is ill-conditioned and must not be used.
3. When the covariance stability criterion is not met, use a robust median absolute deviation (MAD) normalized distance on each continuous field independently

A new capture may join an existing reviewed family-lane only if:

1. It passes all exact invariants for that family-lane
2. It passes all deterministic rule verifiers applicable to that lane
3. Its composite distance is within the reviewed empirical in-family envelope
4. It is not flagged as a multivariate outlier on the continuous subset

If any of those checks fail, the capture becomes a new provisional family-lane candidate instead of silently widening the old family.

---

## 7. Architectural shape of the solution

Wave 2 must keep the code modular and testable.

### 7.1 Offline analysis layer

New logic for multi-dump statistics must live in the analysis pipeline, not in runtime code.

Recommended additions:

- `test/analysis/build_fingerprint_stat_baselines.py`
- `test/analysis/test_build_fingerprint_stat_baselines.py`
- `test/analysis/test_build_fingerprint_stat_baselines_fuzz.py`
- `test/stealth/CorpusIterationTiers.h` — shared iteration tier constants and `TD_NIGHTLY_CORPUS` gating logic
- `test/stealth/DeterministicTlsRuleVerifiers.h`
- `test/stealth/DeterministicTlsRuleVerifiers.cpp`
- `test/stealth/test_tls_deterministic_rule_verifiers.cpp`

Responsibilities:

1. Read reviewed ClientHello and ServerHello artifacts.
2. Group by reviewed `(family_id, route_lane)`.
3. Compute exact invariants, deterministic rule manifests, joint stochastic state catalogs, and diagnostic marginals.
4. Emit reviewed baseline artifacts.

Deterministic rule verifiers must be derived from pinned upstream browser-source logic or a reviewed extracted specification of that logic. They must not require compiling full BoringSSL, NSS, CFNetwork, or browser trees in CI.

### 7.2 Generated review artifacts

Generated artifacts should be data-only.

Recommended outputs:

- `test/analysis/fingerprint_stat_baselines.json`
- `test/stealth/ReviewedFingerprintStatBaselines.h`

Rules:

1. The generated header should contain constants only.
2. Comparison logic belongs in `.cpp` and `.h` test support files, not in giant header-only code.
3. Regeneration must be deterministic from the reviewed corpus.

### 7.3 Runtime differential layer

Existing and new `test/stealth/test_tls_corpus_*` files remain the place where runtime-generated ClientHello bytes are compared against reviewed exact invariants and statistical envelopes.

Existing 1k test files are refactored to use the iteration tier constants from Section 0.1. New multi-dump comparison files (Workstream C) are added to compare generated output against the full reviewed fixture corpus rather than singleton reference values.

Distributional assertions become release-gating only for Tier 3 or Tier 4 route lanes. For lower tiers, the same suites run structural and envelope checks (which are already release-gating at Tier 1–2) but distributional comparisons are diagnostic-only.

The iteration tier constants (`kQuickIterations`, `kSpotIterations`, `kFullIterations`) and the `TD_NIGHTLY_CORPUS` environment variable gating are defined in a shared header, not duplicated per file.

**Progressive unlocking:** Each multi-dump suite file detects the evidence tier of its family-lane at compile time (via constants in the generated baselines header) and enables the appropriate assertion tiers. When a contributor adds new captures and the family reaches Tier 3, the distributional gates activate automatically on the next baseline regeneration — no new test files are needed.

---

## 8. Workstream A — harden corpus intake and provenance

Before widening runtime assertions, harden the intake path for new community dumps.

### New analysis tests to add first

1. `test/analysis/test_fixture_intake_fail_closed.py`
2. `test/analysis/test_fixture_path_sanitization.py`
3. `test/analysis/test_fixture_metadata_collision.py`
4. `test/analysis/test_fixture_large_corpus_stress.py`
5. `test/analysis/test_clienthello_serverhello_pairing_fail_closed.py`
6. `test/analysis/test_generated_header_escaping.py`

### Required test categories

#### Positive

- Valid reviewed artifacts are accepted and merged deterministically.
- Matching ClientHello and ServerHello artifacts from the same source are accepted.

#### Negative

- Unknown family IDs are rejected.
- Duplicate fixture IDs, duplicate scenario IDs, or mixed source hashes are rejected.
- Advisory-only artifacts for a release family are rejected.

#### Edge cases

- Empty artifact sets.
- Single-sample families.
- Maximum accepted metadata lengths.
- Boundary handshake lengths and truncated records.

#### Adversarial / black-hat

- Path traversal in source paths.
- Malicious strings trying to break generated C++ headers.
- Mixed-family poisoning where one rogue sample tries to widen an accepted family.
- Parser-version mixing in the same batch.

#### Integration

- End-to-end extractor -> registry -> merge -> smoke path with temporary corpora.

#### Light fuzz

- Randomized malformed JSON fields, random family names, random lengths, randomized duplicate patterns.

#### Stress

- Thousands of synthetic artifacts to ensure no pathological quadratic behavior or silent acceptance under load.

---

## 9. Workstream B — build reviewed statistical baselines from real dumps

The next artifact after the reviewed summary header must be a reviewed **statistical baseline** per family.

### New artifacts to build

1. `test/analysis/build_fingerprint_stat_baselines.py`
2. `test/analysis/upstream_tls_rule_manifest.json`
3. `test/analysis/fingerprint_stat_baselines.json`
4. `test/stealth/ReviewedFingerprintStatBaselines.h`
5. `test/stealth/FingerprintStatBaselineMatchers.h`
6. `test/stealth/FingerprintStatBaselineMatchers.cpp`

### Required baseline contents per `(family_id, route_lane)`

#### Exact invariants

- TLS record version
- ClientHello legacy version
- Cipher suite set and order rules
- Required and forbidden extensions
- Supported groups set and order rules
- Key share group set and key length rules
- ALPN set and order rules
- ALPS type rules
- ECH presence rules by route and family
- Fresh-connection vs resumption markers (`0x0029`) rules
- ServerHello allowed tuple set and layout signatures

#### Deterministic rule manifests

- Pinned upstream source or reviewed specification reference for each deterministic field
- Allowed ECH legality and padding rules conditioned on inputs and route lane
- Allowed extension-order template set per browser-state cluster
- Allowed key-share structure and length rules conditioned on browser-state cluster
- Allowed ALPN, ALPS, and resumption legality rules conditioned on lane and browser-state cluster

#### Joint stochastic state catalogs

Joint state catalogs prevent the generator from producing impossible field combinations that could be detected by DPI correlation. However, with a small corpus (n = 1–15 per family-lane), the joint catalog approach must be adapted:

**Small-corpus rule (n < 15):** Joint catalogs at Tier 1 and Tier 2 are **set-membership catalogs**, not distributional catalogs. They enumerate the set of observed joint tuples and assert that:
1. Every generated joint tuple must be a member of the generator's declared legal-combination set (a superset of the corpus-observed set, derived from pinned upstream browser rules).
2. Every corpus-observed joint tuple must be producible by the generator.
3. No generated joint tuple matches a declared-impossible combination.

This is a legality check, not a frequency test. It avoids the combinatorial explosion problem because it does not require cell counts — it only requires set membership.

**Larger-corpus rule (n ≥ 15, Tier 3+):** When enough data exists, frequencies of joint tuples can be compared using exact multinomial tests. If any cell in the full joint table has expected count < 1, reduce the joint to pairwise interactions: test all $\binom{k}{2}$ pairs of fields as 2-way contingency tables instead of the full k-way joint. This avoids sparse-cell bias while still detecting the most actionable DPI features (pairwise correlations between extension presence and ECH state, ALPS type and key-share structure, etc.).

Joint tuple sets to track:

- `(extension_order_template, ALPS type family, GREASE legality template)` — tracks whether the generator produces only combinations actually seen in the wild
- `(ECH presence, ECH payload or padded-length bucket, inner extension signature, key-share signature)` — tracks ECH-related field consistency
- `(wire-length bucket, extension count, ALPN signature, resumption state)` — tracks size-related field consistency
- `(first-flight layout, ServerHello cipher, ServerHello extension set)` — tracks full-handshake consistency where the corpus supports it

#### Diagnostic marginals only

- Extension order histograms for review dashboards only
- Extension position histograms for review dashboards only
- GREASE value histograms by slot for sanity review only
- ECH payload length histograms for sanity review only
- ClientHello total wire length histograms for sanity review only
- First-flight record size histograms where available for sanity review only
- JA3 / JA4 family-level stability and separation metrics
- Session ID uniqueness summaries
- Client random uniqueness summaries

Marginal histograms are not allowed to be sampled independently to synthesize a fingerprint. They are diagnostics for human review, not the primary release gate.

**Anchoring safeguard:** All family acceptance and promotion decisions must be logged against the joint catalog check result, not against reviewer visual inspection of marginal plots. If a reviewer overrides a joint catalog rejection based on marginal appearance, it must be recorded as an explicit exception with written justification. Marginals may inform where to investigate, but they do not constitute evidence for promotion.

### Statistical acceptance philosophy

1. Exact invariants must remain exact.
2. Deterministic browser-derived fields must first pass pinned rule verifiers before any corpus comparison is considered meaningful.
3. **Primary gate at all tiers:** Generator-envelope containment and empirical-envelope coverage. Every real capture's observed value must fall within the generator's output space, and every generated value must stay within the empirical envelope derived from all reviewed captures. These work from n = 1 and are the backbone of Wave 2.
4. **Tier 2+ gate:** Set-membership coverage. The generator must cover every distinct observed value in the reviewed corpus (every extension-set variant, every wire-length bucket, every ALPS type). These require n ≥ 3 to be non-trivially constraining.
5. **Tier 3+ gate:** One-sample distributional tests against the generator's known distribution. These use permutation-based exact tests or bootstrap CIs rather than asymptotic tests, because n < 200. Results carry documented power limitations.
6. If a family-lane does not yet have enough reviewed independent data for a statistically powered distributional decision, mark the baseline provisional and keep that lane on structural gates only.
7. The goal is not to force uniformity where real browsers are not uniform; the goal is to reflect real observed joint state behavior without allowing obvious synthetic collapse or impossible combinations.

### Tier-stratified test strategy

The tests that apply depend on the evidence tier of the family-lane. Higher tiers unlock additional gates without relaxing lower-tier gates.

#### 9.1 Tier 1+ gates (n ≥ 1 real capture)

These are structural legality checks. They work even with a single authoritative capture.

| Check | Method | Failure mode |
|---|---|---|
| Exact invariant match | Generated value == reviewed value for every invariant field across all reviewed captures | Any mismatch is a hard fail |
| Deterministic rule legality | Generated value passes pinned upstream browser-logic rule verifier | Any illegal output is a hard fail |
| Generator-envelope containment | Sample generator at 10,000 seeds; every observed real capture value must appear at least once in the generated set | A real value that the generator never produces indicates a generator defect |
| JA3/JA4 set membership | Generated JA3/JA4 must fall within the reviewed set; reviewed JA3/JA4 must fall within the generator's JA3/JA4 output set | Any out-of-set value is a hard fail |

These gates are sufficient for **Tier 1 release eligibility**. A family at Tier 1 can back active fingerprint selection based on structural correctness alone.

#### 9.2 Tier 2+ gates (n ≥ 3 real captures)

These add empirical-envelope checks that become meaningful when there are at least 3 distinct data points.

| Check | Method | Failure mode |
|---|---|---|
| Empirical min/max envelope | Generated wire lengths, extension counts, ECH payload lengths must fall within [min, max] observed across all reviewed captures for the family-lane, with a 10% tolerance margin on continuous fields | Any generated value outside the relaxed envelope is a nightly fail |
| Observed-value coverage | For each discrete field (extension set, supported group set, ALPN set, ALPS type, key share structure), the generator must produce every distinct variant observed across the reviewed captures | A reviewed variant that the generator never produces is a hard fail |
| Cross-family disjointness | For each pair of families documented as disjoint, the generator for family X must never produce the exact invariant set of family Y | Any overlap is a nightly fail |
| Joint state set-membership | Each observed `(extension_order_template, ALPS_type, ECH_presence)` joint tuple in the real corpus must be producible by the generator | A real joint tuple outside the generator's joint output set is a hard fail |

The 10% tolerance margin on continuous min/max envelopes accounts for the fact that a small corpus may not have observed the full real range. As corpus size grows past n = 15, the tolerance narrows to 5%.

#### 9.3 Tier 3+ gates (n ≥ 15 real captures)

These add distributional comparisons that require enough real samples for non-trivial statistical power.

| Data shape | Primary test | Notes |
|---|---|---|
| Binary presence/absence | One-sample exact binomial test against generator's known probability | Detects ~20pp shifts at n = 15 with power ≈ 0.70; power limitation documented per test |
| Low-cardinality categorical | One-sample exact multinomial goodness-of-fit against generator distribution | If any cell has expected count < 1, collapse to a binary present/absent test |
| Ordered extension-template frequencies | Permutation test: observed template-frequency vector vs generator's template-frequency vector | Template set-membership (Tier 2) is primary; frequency comparison is secondary evidence |
| Continuous length fields | One-sample Kolmogorov-Smirnov test against the generator's empirical CDF (from 100,000 seeds) | At n = 15, KS detects distributional shifts with D > 0.34. Bootstrap 95% CI on D reported alongside the p-value |
| Randomness quality of generator-only fields | NIST SP 800-22 subset: monobit and runs tests on Monte Carlo outputs | Tests generator quality, not corpus alignment |

**Why one-sample, not two-sample.** The generator's CDF can be estimated to arbitrary precision by sampling millions of seeds. Treating generator output as one of two unknown samples wastes half the statistical budget. All tests above compare the small real-capture sample against the generator's known (or precisely estimated) distribution.

**Equivalence framing for continuous fields.** Raw goodness-of-fit tests (K-S, A-D) will eventually reject at any sample size because the generator intentionally produces a wider distribution than any finite capture set. To avoid trivial rejections as the corpus grows toward Tier 4:

1. For Tier 3 (n ∈ [15, 200)): use one-sample KS and report the test statistic and bootstrap CI. Rejection at alpha = 0.05 triggers diagnostic investigation, not automatic demotion. The gate is the magnitude of the KS statistic: D < 0.40 passes; D ∈ [0.40, 0.60) triggers review; D ≥ 0.60 fails.
2. For Tier 4 (n ≥ 200, future): switch to a TOST-style equivalence test. H₀: "the distributions differ by more than ε" with ε derived from what a DPI classifier could exploit (16-byte shift or 0.5 pooled SD, whichever is smaller). This tests whether the generator is *close enough*, not whether it is identical.

Default policy:

1. Exact-invariant, deterministic-rule, and containment/envelope tests are hard-fail legality checks and do not participate in multiple-testing correction.
2. Distributional tests within one `(family_id, route_lane)` battery at Tier 3 use Bonferroni with `FWER <= 0.05` (chosen over BH because the small battery size at n < 200 makes BH's FDR advantage negligible while Bonferroni provides stronger familywise guarantees for a small number of tests).
3. Cross-family contamination and promotion tests use Holm-Bonferroni with `FWER <= 0.01`.
4. Every distributional test must document its achieved power given the actual sample size. If achieved power is below 0.50, the result is diagnostic only and cannot promote or demote a lane.
5. **Per-test power rule:** If the available sample size is too small to achieve power ≥ 0.50 against the declared DPI-relevant effect size, the result is diagnostic only.

Default DPI-relevant effect sizes:

1. Binary or categorical presence fields: detect at least a 20 percentage point shift at Tier 3 (relaxed from 10pp at Tier 4 to match available power).
2. Continuous structural length fields: detect at least a 32-byte shift or `0.8` pooled standard deviations at Tier 3 (relaxed from 16-byte / 0.5 SD at Tier 4 to match available power).
3. Classifier-style distinguishability checks: fail if a reviewed simple classifier exceeds the declared held-out AUC threshold (see Section 12).

### JA3 and JA4 policy

1. JA3 and JA4 are necessary but insufficient gates.
2. A route-lane is JA3-stable only if generated outputs stay within the reviewed JA3 set for that lane.
3. A route-lane is JA4-stable only if generated outputs stay within the reviewed JA4 set for that lane.
4. Distinct release families should have disjoint JA3 or JA4 sets whenever the reviewed real corpus shows that disjointness.
5. Passing JA3 or JA4 does not prove safety because they do not fully encode extension order, wire length, key-share payload details, or first-flight layout.
6. **Promotion hierarchy:** JA3/JA4 stability is a prerequisite for promotion consideration, but promotion itself requires passing the structural and envelope gates (Tier 2), and, where available, distributional and classifier-style adversarial gates (Tier 3+). A lane that is JA3/JA4-stable but fails any of those higher gates cannot promote. The promotion checklist in Section 15 is the authoritative sequence.

---

## 10. Workstream C — family-by-family statistical runtime suites

The current 1k suites verify generator self-consistency but do not systematically compare generated outputs against the reviewed fixture corpus derived from real traffic dumps. Wave 2 closes this gap by building **multi-dump statistical comparison suites** that answer the core question: does the generated output for family X match what real browsers in family X actually produce?

### Evidence pipeline these suites consume

The tests operate on a three-stage evidence chain, all of which must be in place before a family-level statistical suite can be release-gating:

1. **Real traffic captures** in `docs/Samples/Traffic dumps/{Android,iOS,Linux desktop,macOS,Windows}/` — PCAPs/PCAPNGs captured from real devices on real networks.
2. **Extracted reviewed JSON fixtures** in `test/analysis/fixtures/clienthello/{android,ios,linux_desktop,macos,windows}/` — produced by `test/analysis/extract_client_hello_fixtures.py` from the PCAPs. Each JSON fixture contains per-sample cipher suites, extension order, supported groups, key shares, ALPN, ECH, ALPS type, compression algorithms, wire lengths, and provenance metadata.
3. **Reviewed C++ fixture constants** in `test/stealth/ReviewedClientHelloFixtures.h` — produced by `test/analysis/merge_client_hello_fixture_summary.py` from the JSON fixtures. These constants are what C++ runtime tests consume.

The multi-dump suites generate `N` ClientHellos via the builder for each `(family_id, route_lane)` and compare them against the full set of reviewed fixture constants for that family in `ReviewedClientHelloFixtures.h`, not against hand-picked singleton reference values.

### Refactoring existing 1k suites

Before adding new multi-dump suites, apply the iteration tier strategy from Section 0.1 to the existing 22 files. No files are deleted — all 228 tests remain, but with right-sized iterations.

Per-file actions (see Section 0.1 for detailed per-file verdicts):

1. **12 files → quick-tier** (93 tests): `firefox_invariance`, `firefox_macos`, `safari26_3_invariance`, `ios_apple_tls`, `ios_chromium_gap`, `fixed_mobile_profile_invariance`, `alps_type_consistency`, `chrome_extension_set`, `android_chromium_alps`, `android_chromium_no_alps` — replace `kCorpusIterations` with `kQuickIterations` (= 3).
2. **5 files → spot-tier in PR, full-tier nightly** (49 tests): `chrome_grease_uniformity`, `grease_slot_independence`, `chrome_ech_payload_uniformity`, `chrome_permutation_position`, `wire_size_distribution` — use `kSpotIterations` (= 64) by default, `kFullIterations` (= 1024) when `TD_NIGHTLY_CORPUS=1`.
3. **1 file → nightly only** (8 tests): `grease_autocorrelation_adversarial` — always uses `kFullIterations`, skipped in PR gate.
4. **4 mixed files → per-test tier** (71 tests): `ja3_ja4_stability`, `adversarial_dpi`, `hmac_timestamp_adversarial`, `structural_key_material_stress` — each test uses the tier matching its assertion type (deterministic → quick, statistical → spot, entropy/uniqueness → full/nightly).
5. **2 files → no change** (20 tests): `cross_platform_contamination`, `cross_platform_contamination_extended` — already fast, fixture-only comparisons.
6. Gate all full-tier iterations on `TD_NIGHTLY_CORPUS=1` environment variable at runtime.

The existing 1k tests become **regression guards for generator internals** (GREASE quality, permutation diversity, structural validity) rather than the primary fixture fidelity evidence. The new multi-dump suites below are the primary fidelity evidence.

### New per-family multi-dump comparison test files

These suites are the core new artifact of Workstream C. Each one compares generated output against **all reviewed fixtures** for the corresponding family, not just one reference capture.

1. `test/stealth/test_tls_multi_dump_linux_chrome_stats.cpp`
2. `test/stealth/test_tls_multi_dump_linux_firefox_stats.cpp`
3. `test/stealth/test_tls_multi_dump_macos_firefox_stats.cpp`
4. `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
5. `test/stealth/test_tls_multi_dump_ios_chromium_stats.cpp`
6. `test/stealth/test_tls_multi_dump_android_chromium_alps_stats.cpp`
7. `test/stealth/test_tls_multi_dump_android_chromium_no_alps_stats.cpp`
8. `test/stealth/test_tls_multi_dump_tdesktop_linux_stats.cpp`

### What each multi-dump suite must assert

For each `(family_id, route_lane)`, using the full reviewed fixture set from `ReviewedClientHelloFixtures.h`:

**Tier-gated assertion model.** Each suite adapts its assertions based on the evidence tier of the family-lane. Suites are structured as: unconditional structural checks (always run) + tier-gated checks (enabled only when the lane qualifies).

#### Exact invariant checks (Tier 1+, all families — compared against every fixture in the family)

- Non-GREASE cipher suite set and order: generated must match the reviewed set for the family. If different fixtures in the family show the same set (which they do for Chrome, Firefox), every generated output must match.
- Non-GREASE extension set (without padding): generated must produce only extensions seen across the family's reviewed fixtures.
- Non-GREASE supported group set: generated must match.
- ALPN protocol set: generated must match.
- Key share group structure: generated must produce groups and lengths consistent with reviewed fixtures.
- Compress certificate algorithm list: generated must match.
- ECH presence/absence rules: must match the route-lane-specific rules observed in the reviewed fixtures.

#### Deterministic rule checks (compared against pinned upstream browser logic)

- Extension order template legality: generated permutations must belong to the set of legal permutations defined by the browser's shuffling rules. This is verified by deterministic rule verifiers, not by comparing against a corpus histogram.
- ALPS type family legality: conditioned on browser version and route lane.
- ECH payload length and structure legality: conditioned on the profile's ECH configuration.

#### Distributional alignment checks (Tier 3+ only, n ≥ 15 real captures)

These tests unlock **only when the family-lane reaches Tier 3** (≥ 15 authoritative captures from ≥ 3 independent sources). Until then, the structural gates above are the release evidence.

When unlocked:

- Wire length distribution: one-sample KS test of real capture wire lengths against the generator's empirical CDF (sampled at 100,000 seeds). Report D statistic and bootstrap 95% CI.
- ECH payload length distribution (for ECH-enabled lanes): one-sample exact multinomial test of real capture values against the generator's known probability vector.
- Extension count distribution: one-sample KS test against generator CDF.
- Key share total length: one-sample containment test — every real value within the generator's output range.

**Important:** These distributional checks cannot make claims stronger than the sample size supports. At n = 15, power against subtle shifts (< 20pp or < 32 bytes) is limited. The tests document their achieved power per the docstring requirements in Section 1. Results that fail to achieve power ≥ 0.50 are reported as diagnostic and cannot promote or demote a lane.

**Progressive strengthening:** As the corpus grows:
- n ∈ [15, 30): Permutation-based exact tests only. Bonferroni correction. Coarse effect sizes (20pp, 32 bytes).
- n ∈ [30, 200): Permutation tests + bootstrap CIs. BH correction. Medium effect sizes (15pp, 24 bytes).
- n ≥ 200 (Tier 4, future): Switch to TOST equivalence framework with asymptotic tests. BH correction. Fine effect sizes (10pp, 16 bytes).

#### Tier 2 structural alignment checks (n ≥ 3 real captures, current achievable bar)

These are the **primary new tests for Wave 2** because they are achievable with the current corpus:

- For each discrete field (cipher suites, extensions, supported groups, ALPN, ALPS type, key share groups), verify that the set of distinct values across all reviewed captures for the family equals or is a subset of the generator's output set. A generated value not seen in any capture triggers a review; a capture value not producible by the generator is a hard fail.
- For continuous fields (wire length), verify that generated outputs fall within the empirical [min − tolerance, max + tolerance] envelope across all reviewed captures. Tolerance is 10% of the observed range at Tier 2, narrowing to 5% at Tier 3+.
- For joint tuples, verify set-membership as described in Section 9.2.
- For cross-family pairs, verify that the generator for family X never produces the complete invariant-set signature of family Y.

#### Cross-family contamination checks

- A generated output for family X must not match the exact invariant set of family Y when families X and Y are documented as disjoint by the reviewed corpus.
- Example: Linux Chrome generated output must not produce an extension set that matches the iOS Apple TLS reviewed fixtures.

### Reserved future files for Windows desktop

1. `test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp`
2. `test/stealth/test_tls_multi_dump_windows_firefox_stats.cpp`
3. `test/stealth/test_tls_multi_dump_windows_edge_stats.cpp`
4. `test/stealth/test_tls_multi_dump_windows_cross_browser_contamination.cpp`

### Every family suite must include these categories

#### Positive

- Generated output matches exact required invariants for the intended family.
- Generated outputs pass deterministic rule verifiers for that family-lane.
- Generated statistical distributions stay within the reviewed family-lane envelope when the lane is Tier 2 or Tier 3.

#### Negative

- Generated output does not exactly match foreign families.
- Forbidden extensions and forbidden handshake layouts never appear.

#### Edge cases

- Domain length extremes.
- Timestamp extremes.
- ECH enabled and disabled lanes where applicable.
- Fresh connection versus resumption-sensitive markers.

#### Adversarial / black-hat

- Fixed-value RNGs.
- Route flips RU <-> non-RU.
- Same browser, different OS version collisions.
- Cross-device contamination inside the same app family.
- Extension order collapse, GREASE collapse, or deterministic wire length collapse.

#### Integration

- Builder -> parser -> baseline matcher -> reviewed family acceptance.

#### Light fuzz

- Randomized seeds, domains, timestamps, secrets, and route hints.

#### Stress

- 10k and larger Monte Carlo passes in nightly or extended validation mode.

Every statistical or classifier-style runtime suite must declare in its file header:

1. Whether it is release-gating or diagnostic-only
2. The `(family_id, route_lane)` it applies to
3. The evidence tier of the family-lane and how many real captures back it
4. For Tier 1–2: which structural gates it exercises (exact invariant, deterministic rule, envelope, set-membership, containment)
5. For Tier 3+: its null hypothesis, alternative hypothesis, declared alpha, achieved power estimate given actual n, effect size, and correction policy
6. Whether it depends on Tier 3+ evidence for any specific assertion (and degrades gracefully at Tier 2)

---

## 11. Workstream D — handshake acceptance and ServerHello corroboration

Current ServerHello support is already useful, but it does not mean the same thing as generator fidelity. Wave 2 therefore splits this workstream in two.

### Workstream D1 — handshake acceptance validation

This is a correctness workstream. A pass means the generated ClientHello is accepted by the server or harness under the intended lane. A pass does **not** prove fingerprint fidelity.

New tests to add:

1. `test/stealth/test_tls_handshake_acceptance_matrix.cpp`
2. `test/stealth/test_tls_handshake_acceptance_route_lanes.cpp`

Required assertions:

1. Generated ClientHellos complete the expected handshake path for the intended family-lane.
2. RU fail-closed behavior and ECH-disabled fallbacks do not break the handshake path expected for that lane.
3. A failure is treated as strong evidence of a generator or policy defect.

### Workstream D2 — ServerHello and first-flight corroboration

This is a weaker but still useful corroboration workstream. A pass provides secondary evidence; a fail is strong evidence of mismatch.

New tests to add:

1. `test/analysis/test_serverhello_family_pairing_contract.py`
2. `test/analysis/test_serverhello_layout_distribution.py`
3. `test/stealth/test_tls_full_handshake_family_pairing.cpp`
4. `test/stealth/test_tls_first_flight_layout_pairing.cpp`

### Required assertions

1. Each reviewed ClientHello family-lane maps only to reviewed ServerHello tuples and layouts seen for that same family-lane.
2. No ClientHello family-lane is accidentally paired with a ServerHello family from a different OS, browser family, or route lane.
3. First-flight layout signatures remain within the reviewed family-lane envelope.
4. ECH-enabled, ECH-disabled, and RU fail-closed lanes are checked independently where the corpus supports them.
5. ServerHello corroboration may support promotion, but it is never sufficient on its own to prove ClientHello fidelity.

This is important because a censor does not need to classify only the ClientHello. A mismatched full handshake can be enough.

---

## 12. Workstream E — DPI-minded adversarial regression suites

The new suites should explicitly assume a black-hat reviewer or censor looking for the smallest classifier that separates Telegram traffic from real browsers.

### New adversarial files to add

1. `test/stealth/test_tls_fingerprint_classifier_blackhat.cpp`
2. `test/stealth/test_tls_cross_family_one_bit_differentiators.cpp`
3. `test/stealth/test_tls_route_ech_quic_block_matrix.cpp`

### Attacks the tests must model

1. One-bit differences in extension presence.
2. Wrong ALPS type for a browser generation.
3. Wrong ECH presence for route or OS family.
4. Wrong session resumption marker on fresh connections.
5. Cross-platform extension-set contamination.
6. Wrong ALPN set for device-specific families.
7. Fixed total wire length or low-entropy GREASE.
8. Same browser app diverging by device or OS version and the generator collapsing them into one synthetic family.
9. Wrong ServerHello tuple after an otherwise plausible ClientHello.
10. Fallback behavior under ECH blocking or QUIC unavailability drifting into a synthetic fingerprint family.

Every adversarial test must explain exactly what DPI discriminator it is defending against.

Classifier-style adversarial suites must treat real-vs-generated separation as a failure signal, not as an optional curiosity.

Default classifier gate:

1. Train only on reviewed feature vectors that correspond to fields already used elsewhere in the plan.
2. Use simple audited baselines such as logistic regression and shallow trees before trying more expressive models. Simple models are chosen for **interpretability**: a logistic regression with feature weights identifies which specific feature leaks, while a complex ensemble at the same AUC provides no actionable signal. This is a deliberate design choice, not a capability limitation.
3. Fail the gate if any reviewed classifier achieves a held-out AUC whose lower 95% bootstrap confidence bound exceeds `0.60` for distinguishing real vs generated outputs inside the same `(family_id, route_lane)`.
4. **Minimum sample requirement — adapted for small corpus:** The classifier gate has tiered minimum requirements:
   - **Tier 3 (n ≥ 15 real captures):** Run the classifier with leave-one-out cross-validation (LOOCV) instead of held-out split. Generate 1,000 synthetic samples per fold. The bootstrap CI on AUC is wide at n = 15 (approximately ±0.15), so the gate catches only gross leaks (point AUC > 0.80) at this tier. Results are diagnostic for subtler leaks. The LOOCV approach is chosen because a train/test split at n = 15 leaves too few test samples for a stable AUC estimate.
   - **Tier 4 (n ≥ 200, future):** Switch to 80/20 held-out split with 1,000 bootstrap resamples. AUC lower 95% CI > 0.60 is the release-blocking threshold.
   - **Below Tier 3 (n < 15):** No classifier gate — sample is too small for any meaningful classification-based inference. Cross-family contamination is checked via exact invariant disjointness instead.
5. **Adversary capability acknowledgment:** passing the simple-classifier gate is necessary evidence but not sufficient assurance against a state-level adversary with cross-connection correlation, deep ensemble classifiers, or traffic-analysis capabilities. The gate detects gross feature leaks at the per-connection level. Periodic reassessment with more expressive models (gradient-boosted ensembles, neural network probes) is recommended as the corpus grows, but such models are advisory — only interpretable classifier failures are release-blocking.

---

## 13. Workstream F — nightly fuzz, Monte Carlo, and boundary falsification

The repository already contains light fuzz and stress suites. Wave 2 adds long-run statistical validation where the smaller PR suite is not enough.

### New tests to add

1. `test/analysis/test_build_fingerprint_stat_baselines_stress.py`
2. `test/stealth/test_tls_nightly_generator_consistency_monte_carlo.cpp`
3. `test/stealth/test_tls_nightly_boundary_falsification.cpp`
4. `test/stealth/test_tls_nightly_cross_family_distance.cpp`

### Required long-run coverage

1. **Generator consistency Monte Carlo**: 10k-seed per-family-lane runs verify that the generator remains inside the reviewed baseline and deterministic legality envelope under many seeds. This is the **highest-value nightly check** because it requires zero additional captures — it validates the generator against its own declared rules and the reviewed invariants.
2. **Boundary falsification Monte Carlo**: 10k-seed per-family-lane runs verify that no sampled output violates deterministic rule verifiers or legal joint state catalogs (set-membership variant for Tier 1–2, frequency variant for Tier 3+).
3. **Generator-envelope containment at scale**: For Tier 1+ families, verify that every reviewed real capture value is producible by the generator across 100,000 seeds. This is a stronger version of the Tier 1 containment gate and catches generator bugs that only manifest under specific seeding patterns.
4. **Escalation runs**: 100k focused runs for the most sensitive families or fields when a regression is suspected.
5. Large synthetic corpus stress for the analysis pipeline to ensure fail-closed behavior still holds under corpus growth.
6. Randomness quality checks on generated client-random and session-id fields use monobit and runs tests on Monte Carlo outputs, not entropy estimates derived from tiny real corpora.

Monte Carlo validates generator behavior against the reviewed model and legality rules. It does not substitute for corpus evidence — it cannot prove the reviewed model is correct, only that the generator faithfully implements it.

Performance requirement:

1. The PR gate must complete all fingerprint corpus tests within 30 seconds, using the iteration tier strategy from Section 0.1.
2. The nightly gate can be expensive and uses `kFullIterations` (= 1024) or higher.
3. Expensive statistics must be precomputed offline where possible.
4. C++ runtime tests should consume compact generated baselines from `ReviewedClientHelloFixtures.h` and `ReviewedFingerprintStatBaselines.h` instead of reparsing large JSON corpora on every run.

---

## 14. Workstream G — close profile-model gaps surfaced by the corpus

Wave 2 is not only about more tests. It is also about refusing to keep pretending a release family exists if the corpus says it does not.

### Gap 1 — Windows desktop

When Windows dumps arrive from the community:

1. Add Windows families to `test/analysis/profiles_validation.json`.
2. Add reviewed ClientHello and ServerHello artifacts.
3. Add dedicated runtime profiles if needed.
4. Add per-family statistical suites.
5. Add cross-platform contamination suites.

Windows must never be folded into Linux or macOS assumptions.

### Gap 2 — dedicated iOS Chromium family

The corpus already shows that iOS Chromium behavior can diverge by OS version and is not always the same as the Apple TLS family. Wave 2 should make that separation release-grade instead of leaving it as a documented gap.

### Gap 3 — Android advisory profile mismatch

`Android11_OkHttp_Advisory` is not sufficient as the final story for Android browser impersonation. Promote real Android browser families or keep the profile explicitly provisional.

### Gap 4 — release family trust tiers

At the end of Wave 2, no active release family should remain backed only by advisory fixture metadata.

---

## 15. CI and release-gating model

### PR gate

Fast, deterministic, fail-closed. Target: under 30 seconds for the entire fingerprint corpus test slice.

1. Corpus smoke validation.
2. Exact invariant suites at `kQuickIterations` (= 3 seeds).
3. Deterministic rule verifier suites at `kQuickIterations`.
4. Analysis pipeline fail-closed tests.
5. Statistical spot-check suites at `kSpotIterations` (= 64 seeds) for GREASE uniformity, ECH payload distribution, permutation diversity, and wire size distribution.
6. Multi-dump fixture alignment exact invariant checks (new Workstream C suites) at `kQuickIterations`.
7. Optional touched-family statistical smoke suites may run for developer feedback, but they are diagnostic-only and cannot promote or demote release status.

Tests that were previously at `kCorpusIterations` (= 1024) and classified as exact-invariant or structural-validity in Section 0.1 run at `kQuickIterations` in the PR gate. Tests classified as statistical run at `kSpotIterations`. Entropy/uniqueness tests are nightly-only.

### Nightly gate

Broader evidence. All tests run at their full iteration tier (`kFullIterations` = 1024 or higher). Gated by `TD_NIGHTLY_CORPUS=1`.

1. Full multi-dump baseline regeneration check.
2. **Tier 1+ structural gates for all families:** exact invariants, deterministic rules, generator-envelope containment at 100,000 seeds.
3. **Tier 2+ envelope and coverage gates** for qualifying families: empirical envelopes, set-membership, joint state set-membership, cross-family disjointness.
4. **Tier 3+ distributional gates** for qualifying families (currently: families with ≥ 15 captures): one-sample KS, exact multinomial, permutation tests, classifier LOOCV gate.
5. Generator consistency Monte Carlo at 10k seeds per family-lane.
6. Boundary falsification Monte Carlo at 10k seeds per family-lane.
7. Multi-dump fixture structural alignment checks at `kFullIterations`.
8. Entropy and uniqueness tests (`adversarial_dpi`, `hmac_timestamp`, `grease_autocorrelation`) at `kFullIterations`.
9. Handshake acceptance checks.
10. ServerHello and first-flight corroboration checks.
11. Long-run fuzz, stress, and randomness quality suites.
12. **Tier status report:** nightly produces a summary per family-lane: current tier, number of captures, which gates ran, pass/fail status, and what's needed for next tier.

### CTest label strategy

To support the PR/nightly split without duplicating test files:

1. Add a `STEALTH_CORPUS_QUICK` CTest label for PR-gate-eligible corpus tests.
2. Add a `STEALTH_CORPUS_FULL` CTest label for nightly-only tests.
3. The PR CI step runs `ctest -L STEALTH_CORPUS_QUICK`.
4. The nightly CI step runs `ctest` without label filter (all tests) with `TD_NIGHTLY_CORPUS=1`.
5. CTest presets in `CMakePresets.json` are added for both modes.

### Promotion gate for a new family

Before a new family can back release behavior:

1. **Tier 1 minimum (structural release):** At least 1 authoritative network-derived capture exists for every emitted route lane.
2. Reviewed route-lane baseline exists, including exact invariants and deterministic rule manifests.
3. Positive, negative, edge, adversarial, integration, light fuzz, and stress tests all exist in separate files.
4. Exact invariants and deterministic rule verifiers are green.
5. Generator-envelope containment is green (every real capture value is producible by the generator).
6. JA3/JA4 set membership is green.
7. Handshake acceptance is green.

**Tier 2 promotion (structural + envelope release, current target for most families):**

8. All Tier 1 gates pass.
9. At least 3 authoritative captures from at least 2 independent sources per emitted route lane.
10. Empirical-envelope gates are green (generated outputs within reviewed min/max).
11. Set-membership coverage is green (generator covers all observed variants).
12. Joint state set-membership is green.
13. Cross-family disjointness checks are green.
14. ServerHello corroboration is green where corpus supports it, but is not treated as sole fidelity proof.

**Tier 3 promotion (distributional release, target for richest families):**

15. All Tier 2 gates pass.
16. At least 15 authoritative captures from at least 3 independent sources per emitted route lane.
17. One-sample distributional tests pass or are documented as diagnostic-only with achieved power < 0.50.
18. Classifier-style adversarial gate passes (LOOCV, gross-leak detection).

**Tier 4 promotion (high-confidence, future):**

19. All Tier 3 gates pass.
20. At least 200 authoritative captures per emitted route lane.
21. Full asymptotic distributional battery with TOST equivalence framework passes.
22. Classifier gate with 80/20 held-out split passes at AUC lower 95% CI > 0.60.

---

## 16. Completion criteria

Wave 2 is complete only when all of the following are true.

1. Every active fingerprint family has at least **Tier 1 evidence** (at least 1 authoritative network-derived capture) for every emitted route lane, or is explicitly marked non-release-grade in the missing lanes.
2. Every active fingerprint family with ≥ 3 captures has **Tier 2 evidence** (structural + envelope release) for every emitted route lane, or has a documented path to Tier 2 when contributor captures arrive.
3. Every active fingerprint family has a reviewed route-lane baseline generated from real dumps.
4. Every active fingerprint family has separate positive, negative, edge, adversarial, integration, light fuzz, and stress suites.
5. No active release family remains advisory-backed (Tier 0). Families that cannot be promoted to Tier 1 are either demoted to non-release or replaced with promotable alternatives.
6. Windows desktop either has its own reviewed families and tests or remains explicitly out of release scope.
7. Distributional promotion (Tier 3) is only granted to family-lanes with ≥ 15 captures from ≥ 3 independent sources; lower-tier families use structural gates only and document the limitation.
8. Handshake acceptance is a correctness gate and ServerHello corroboration is a secondary release gate, never the sole fidelity proof.
9. Cross-family contamination tests (exact invariant disjointness at Tier 2) fail on any attempted silent widening of a family.
10. The analysis pipeline treats community dumps as untrusted input and fails closed under malformed or malicious artifacts.
11. RU-route and blocked-ECH behavior remain consistent with route policy and reviewed corpus expectations.
12. Baseline generation, corpus smoke, deterministic verifier suites, and runtime differential suites are all green.
13. **Existing 1k test iteration tiers are refactored** per the Section 0.1 per-file verdicts: 12 files at quick-tier, 5 files at spot/full-tier, 1 file nightly-only, 4 mixed files with per-test tiers, 2 files unchanged. No tests deleted. PR gate fingerprint corpus tests complete within 30 seconds.
14. **Multi-dump comparison suites compare against all fixtures per family-lane**, not the singleton fixture reference that the existing 1k tests use. The existing 1k tests' provenance chain (`ReviewedClientHelloFixtures.h` → single fixture) is preserved as generator regression guards, while the new multi-dump suites provide family-level breadth.
15. **Per-family multi-dump comparison suites exist** for every active non-advisory family, comparing generated outputs against the full reviewed fixture set in `ReviewedClientHelloFixtures.h` per `(family_id, route_lane)`.
16. **Tier status is documented per family-lane.** Every active family reports: current tier, number of authoritative captures, number of independent sources, which gates are active, and what would be needed to reach the next tier.
17. **Generator-envelope containment** is verified at 100,000 seeds for every Tier 1+ family: every real capture value is producible by the generator.

---

## 17. Required documentation update after implementation

If and only if this entire plan is implemented and the validation gates are green, update:

- `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`

That update must include:

1. The exact commands run.
2. The exact green test outputs or summarized pass counts.
3. Which families and route lanes are Tier 1 (anchored — structural release only).
4. Which families and route lanes are Tier 2 (corroborated — structural + envelope release).
5. Which families and route lanes are Tier 3 (distributional — with documented power limitations).
6. Which families or route lanes remain Tier 0 (advisory) and why.
7. For each family at Tier 2, what is needed to reach Tier 3 (how many additional captures, from how many additional independent sources).
8. Any residual risk that still blocks promotion.

Do not update the Russian implementation doc early with aspirational claims.

---

## 18. Suggested implementation order

1. Freeze the statistical methodology contract, route-lane schema, and evidence-tier rules.
2. **Refactor existing 1k test iteration tiers.** Apply the `kQuickIterations`/`kSpotIterations`/`kFullIterations` split to the existing 22 files per the Section 0.1 audit. Add `TD_NIGHTLY_CORPUS` runtime gating. Add CTest labels `STEALTH_CORPUS_QUICK` and `STEALTH_CORPUS_FULL`. Verify PR gate stays under 30 seconds.
3. Harden corpus intake and provenance tests.
4. Add deterministic rule verifiers with pinned upstream-rule references.
5. Build the route-lane baseline generator and generated baseline artifacts, **including per-family tier metadata** (number of captures, number of independent sources, current tier, active gates).
6. **Wire the dump→fixture→test evidence chain.** Ensure `ReviewedClientHelloFixtures.h` constants carry explicit provenance back to their source fixture files in `test/analysis/fixtures/clienthello/`. Every constant used as a reference value in a test must trace to a specific `fixture_id` and `source_sha256`.
7. **Build per-family multi-dump comparison suites (Workstream C) with tier-gated assertions.** These suites run structural gates (Tier 1+) for all families and conditionally enable envelope/coverage (Tier 2+) and distributional (Tier 3+) gates based on the baselines header. This is the core artifact that closes Gap E.
8. **Build generator-envelope containment suites.** 100,000-seed runs verifying every real capture value is producible. These are high-value because they require zero additional captures.
9. Add handshake acceptance and ServerHello corroboration suites.
10. Promote or demote advisory-backed active profiles based on real evidence. Safari26_3, IOS14, and Android11_OkHttp_Advisory must reach Tier 1 or be demoted.
11. Add Windows families only when the reviewed corpus is wired into `profiles_validation.json` and runtime profiles exist.
12. Run nightly-scale Monte Carlo, fuzz, and stress.
13. **Distributional and classifier gates (Tier 3+) are diagnostic-only** until community contributions grow specific family-lanes to n ≥ 15. No code changes needed when that happens — gates activate via the baselines header tier metadata.
14. Update `docs/Plans/STEALTH_IMPLEMENTATION_RU.md` only after all structural gates (Tier 1–2) are green for every active family.

---

## 19. Final rule

If a new community dump contradicts the current generator, assume the generator may be wrong until the discrepancy is explained. The correct action is not to loosen the test. The correct action is to classify the discrepancy, determine whether it represents a new family, a real bug, a route-specific variant, a resumption artifact, or a corpus defect, and only then decide how to change code or expectations.

That is the standard required for nation-state DPI resistance.