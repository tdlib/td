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

No finite corpus can mathematically prove future traffic for all time. The practical bar in this plan is stronger and operationally release-grade: every active fingerprint family must be backed by independent real dumps, reviewed statistical baselines, paired-handshake corroboration, and adversarial regression gates that fail closed.

---

## 1. Objective

Turn the current fingerprint validation system into a **release-blocking, corpus-driven statistical verification pipeline** that answers one question for every active browser and OS fingerprint family:

> Do we have enough independent real traffic evidence to assert that the generated TLS fingerprint is indistinguishable from the intended browser/OS family and not a cross-family hybrid that would expose Telegram?

This plan is successful only if the answer becomes **yes** for every active family and **not release-grade yet** for every family that still lacks sufficient evidence.

### Statistical methodology contract

No finite corpus proves future traffic for all time. Wave 2 therefore makes bounded, falsifiable claims rather than absolute proof claims.

Every release gate must declare which of the following claim classes it is using:

| Claim class | Null hypothesis or rule under test | Failure condition | Gate type |
|---|---|---|---|
| Exact invariant | The generated output never violates invariant `I` for `(family_id, route_lane)` | A single counterexample exists | Hard fail |
| Deterministic rule | Given the same inputs and pinned upstream rule specification, the generated field is legal | A single impossible output exists | Hard fail |
| Distributional fidelity | Real captures and generated outputs for the same `(family_id, route_lane, browser_state)` are drawn from the same audited joint distribution | The chosen corrected two-sample test rejects at configured `alpha` | Nightly and promotion only |
| Cross-family contamination | Generated outputs for family `X` stay inside the reviewed envelope for `X` and outside disjoint invariants for family `Y` | Distance gate, disjoint invariant gate, or classifier threshold is violated | Nightly and promotion only |

Required metadata in every distributional or classifier-style test file docstring:

1. Null hypothesis `H0`
2. Alternative hypothesis `H1`
3. Significance level `alpha`
4. Target power `1 - beta`
5. DPI-relevant effect size being detected
6. Minimum sample size required for the decision to be release-gating
7. Multiple-testing correction policy

Exact-invariant and deterministic-rule suites are not treated as null-hypothesis significance tests. They are legality checks and fail on the first counterexample.

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

### Gap C — reviewed statistical baselines are missing

`test/stealth/ReviewedClientHelloFixtures.h` is useful, but it is still a reviewed summary rather than a dedicated statistical baseline artifact built from many independent dumps per family.

### Gap D — handshake acceptance and ServerHello corroboration are not yet formalized separately

The repository already has ServerHello extraction and smoke validation, but the next bar is stronger and must be split cleanly:

1. **Handshake acceptance** — generated ClientHellos must successfully complete the expected handshake path. This is correctness.
2. **ServerHello and first-flight corroboration** — the resulting server-side reply and first-flight layout should remain consistent with the reviewed corpus for that lane. This is secondary evidence, not primary proof of ClientHello fidelity.

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
5. ISP, AS number, or materially different network path

Capture date alone never creates independence.

### Evidence tiers

To avoid overclaiming confidence, every family must be tagged by evidence strength.

| Tier | Meaning | Minimum evidence | Allowed use |
|---|---|---|---|
| Tier 0 | Provisional | 1 to 4 authoritative captures or only pseudo-replicated sessions from one source | Test research only, never release-grade |
| Tier 1 | Corroborated exact-invariant evidence | At least 5 authoritative captures from at least 2 independent sources and at least 2 distinct sessions | Exact-invariant and deterministic-rule tests only; no distributional promotion |
| Tier 2 | Operational release-grade | At least 200 authoritative captures per `(family_id, route_lane)`, at least 3 independent sources, at least 2 browser minor versions unless a reviewed no-drift justification is recorded, at least 2 distinct capture days or environments, handshake acceptance evidence, and ServerHello corroboration | Exact, deterministic, and corrected distributional gates allowed with documented uncertainty |
| Tier 3 | High-confidence release-grade | At least 385 authoritative captures per `(family_id, route_lane)` under the same independence and version-spanning rules as Tier 2 | Preferred bar for stable distributional promotion and long-term release confidence |

Rules:

1. If a route lane has fewer than 200 authoritative captures, distributional fields for that lane are diagnostic only and cannot promote it.
2. If a family lacks multi-version evidence, it may not claim release-grade coverage across browser auto-update behavior unless a reviewed no-drift justification is attached.
3. Promotion is per route lane, not global per family.

### Family and lane clustering policy

New captures are clustered against existing reviewed `(family_id, route_lane)` groups using a composite distance on mixed feature types.

Primary distance components:

1. Jaccard distance on extension-presence sets
2. Kendall tau distance on ordered non-GREASE extension sequences when order is meaningful
3. Normalized absolute difference on continuous length fields using robust scale estimates
4. Hamming distance on deterministic state flags such as ECH lane, ALPS type family, resumption markers, and key-share structure class

Secondary multivariate outlier rule:

1. Use Mahalanobis distance only on continuous co-varying fields and only when the reviewed family-lane has enough samples for a stable covariance estimate
2. Otherwise use a robust median absolute deviation normalized distance

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

Distributional assertions become release-gating only for Tier 2 or Tier 3 route lanes. For lower tiers, the same suites may run in diagnostic mode but cannot promote a family.

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

- `browser_state` catalogs that keep co-varying fields together instead of treating them as independent marginals
- Joint tuples for `(extension_order_template, ALPS type family, GREASE legality template)`
- Joint tuples for `(ECH presence, ECH payload or padded-length bucket, inner extension signature, key-share signature)`
- Joint tuples for `(wire-length bucket, extension count, ALPN signature, resumption state)`
- Joint tuples for first-flight and ServerHello reply patterns where the corpus supports them

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

### Statistical acceptance philosophy

1. Exact invariants must remain exact.
2. Deterministic browser-derived fields must first pass pinned rule verifiers before any corpus comparison is considered meaningful.
3. Stochastic fields must be validated against reviewed joint state catalogs and corrected two-sample tests, not against independently sampled marginals.
4. If a family-lane does not yet have enough reviewed independent data for a statistically powered distributional decision, mark the baseline provisional and keep that lane out of release-grade distributional promotion.
5. The goal is not to force uniformity where real browsers are not uniform; the goal is to reflect real observed joint state behavior without allowing obvious synthetic collapse or impossible combinations.

### Named statistical tests and correction policy

| Data shape | Primary test | Release notes |
|---|---|---|
| Binary presence or absence | Fisher exact test for small counts, otherwise two-proportion test | Target detection of at least 10 percentage point shifts when sample size permits |
| Low-cardinality categorical fields | Exact multinomial test when feasible, otherwise chi-squared or G-test with reviewed minimum cell counts | If counts are too sparse, keep the decision diagnostic-only |
| Ordered extension-template frequencies | Permutation test over template-frequency vectors and Kendall tau distance as a diagnostic | Template membership is primary; order marginals alone are not |
| Continuous length fields | Two-sample Anderson-Darling test with bootstrap confidence intervals on effect size | Tail sensitivity matters because DPI classifiers often target tails |
| Randomness quality of generator-only fields | NIST SP 800-22 subset: monobit and runs tests on Monte Carlo outputs | This tests generator quality, not corpus alignment |

Default policy:

1. Exact-invariant and deterministic-rule tests are hard-fail legality checks and do not participate in multiple-testing correction.
2. Distributional tests within one `(family_id, route_lane)` battery use Benjamini-Hochberg with target `FDR <= 0.05`.
3. Cross-family contamination and promotion tests use a family-wise correction, defaulting to Holm-Bonferroni with `FWER <= 0.01`.
4. Target power for release-gating distributional tests is at least `0.8` against a DPI-relevant effect size.
5. If the available sample size is too small to achieve the declared power, the result is diagnostic only and cannot promote a lane.

Default DPI-relevant effect sizes:

1. Binary or categorical presence fields: detect at least a 10 percentage point shift when sample size permits.
2. Continuous structural length fields: detect at least a 16-byte shift or `0.5` pooled standard deviations, whichever is smaller.
3. Classifier-style distinguishability checks: fail if a reviewed simple classifier exceeds the declared held-out AUC threshold.

### JA3 and JA4 policy

1. JA3 and JA4 are necessary but insufficient gates.
2. A route-lane is JA3-stable only if generated outputs stay within the reviewed JA3 set for that lane.
3. A route-lane is JA4-stable only if generated outputs stay within the reviewed JA4 set for that lane.
4. Distinct release families should have disjoint JA3 or JA4 sets whenever the reviewed real corpus shows that disjointness.
5. Passing JA3 or JA4 does not prove safety because they do not fully encode extension order, wire length, key-share payload details, or first-flight layout.

---

## 10. Workstream C — family-by-family statistical runtime suites

The current 1k suites are a strong start. Wave 2 adds **multi-dump exact, deterministic, and distributional comparison suites** that compare generated output against reviewed baselines, not just one reviewed capture.

### New per-family test files to add

1. `test/stealth/test_tls_multi_dump_linux_chrome_stats.cpp`
2. `test/stealth/test_tls_multi_dump_linux_firefox_stats.cpp`
3. `test/stealth/test_tls_multi_dump_macos_firefox_stats.cpp`
4. `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
5. `test/stealth/test_tls_multi_dump_ios_chromium_stats.cpp`
6. `test/stealth/test_tls_multi_dump_android_chromium_alps_stats.cpp`
7. `test/stealth/test_tls_multi_dump_android_chromium_no_alps_stats.cpp`
8. `test/stealth/test_tls_multi_dump_tdesktop_linux_stats.cpp`

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
3. Its null hypothesis and alternative hypothesis
4. The declared `alpha`, power target, effect size, and correction policy
5. Whether it depends on Tier 2 or Tier 3 evidence

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
2. Use simple audited baselines such as logistic regression and shallow trees before trying more expressive models.
3. Fail the gate if any reviewed classifier achieves a held-out AUC whose lower 95% bootstrap confidence bound exceeds `0.60` for distinguishing real vs generated outputs inside the same `(family_id, route_lane)`.

---

## 13. Workstream F — nightly fuzz, Monte Carlo, and boundary falsification

The repository already contains light fuzz and stress suites. Wave 2 adds long-run statistical validation where the smaller PR suite is not enough.

### New tests to add

1. `test/analysis/test_build_fingerprint_stat_baselines_stress.py`
2. `test/stealth/test_tls_nightly_generator_consistency_monte_carlo.cpp`
3. `test/stealth/test_tls_nightly_boundary_falsification.cpp`
4. `test/stealth/test_tls_nightly_cross_family_distance.cpp`

### Required long-run coverage

1. **Generator consistency Monte Carlo**: 10k-seed per-family-lane runs verify that the generator remains inside the reviewed baseline and deterministic legality envelope under many seeds.
2. **Boundary falsification Monte Carlo**: 10k-seed per-family-lane runs verify that no sampled output violates deterministic rule verifiers or legal joint state catalogs.
3. **Escalation runs**: 100k focused runs for the most sensitive families or fields when a regression is suspected.
4. Large synthetic corpus stress for the analysis pipeline to ensure fail-closed behavior still holds under corpus growth.
5. Randomness quality checks on generated client-random and session-id fields use monobit and runs tests on Monte Carlo outputs, not entropy estimates derived from tiny real corpora.

Monte Carlo is not allowed to stand in for corpus truth. It validates generator behavior against the reviewed model and legality rules; it does not prove the reviewed model is correct.

Performance requirement:

1. The PR gate must stay reasonably narrow.
2. The nightly gate can be expensive.
3. Expensive statistics must be precomputed offline where possible.
4. C++ runtime tests should consume compact generated baselines instead of reparsing large JSON corpora on every run.

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

Fast, deterministic, fail-closed:

1. Corpus smoke validation.
2. Exact invariant suites.
3. Deterministic rule verifier suites.
4. Analysis pipeline fail-closed tests.
5. Optional touched-family statistical smoke suites may run for developer feedback, but they are diagnostic-only and cannot promote or demote release status.

### Nightly gate

Broader evidence:

1. Full multi-dump baseline regeneration check.
2. Corrected distributional battery for Tier 2 and Tier 3 route lanes.
3. Generator consistency Monte Carlo.
4. Boundary falsification Monte Carlo.
5. Handshake acceptance checks.
6. ServerHello and first-flight corroboration checks.
7. Long-run fuzz, stress, and classifier-style adversarial suites.

### Promotion gate for a new family

Before a new family can back release behavior:

1. Tier 2 evidence is present for every emitted route lane, with Tier 3 preferred.
2. Reviewed route-lane baseline exists, including exact invariants, deterministic rule manifests, and joint state catalogs.
3. Positive, negative, edge, adversarial, integration, light fuzz, and stress tests all exist in separate files.
4. Exact invariants and deterministic rule verifiers are green.
5. Corrected distributional tests are green for Tier 2 and Tier 3 lanes.
6. Handshake acceptance is green.
7. ServerHello corroboration is green, but is not treated as sole fidelity proof.
8. Cross-family contamination and classifier-style adversarial tests are green.

---

## 16. Completion criteria

Wave 2 is complete only when all of the following are true.

1. Every active fingerprint family has Tier 2 evidence for every emitted route lane or is explicitly marked non-release-grade in the missing lanes.
2. Every active fingerprint family has a reviewed route-lane baseline generated from real dumps.
3. Every active fingerprint family has separate positive, negative, edge, adversarial, integration, light fuzz, and stress suites.
4. No active release family remains advisory-backed.
5. Windows desktop either has its own reviewed families and tests or remains explicitly out of release scope.
6. Distributional promotion is only granted to Tier 2 or Tier 3 route lanes; lower tiers remain exact-invariant or deterministic-only.
7. Handshake acceptance is a correctness gate and ServerHello corroboration is a secondary release gate, never the sole fidelity proof.
8. Cross-family contamination tests fail on any attempted silent widening of a family.
9. The analysis pipeline treats community dumps as untrusted input and fails closed under malformed or malicious artifacts.
10. RU-route and blocked-ECH behavior remain consistent with route policy and reviewed corpus expectations.
11. Baseline generation, corpus smoke, deterministic verifier suites, and runtime differential suites are all green.

---

## 17. Required documentation update after implementation

If and only if this entire plan is implemented and the validation gates are green, update:

- `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`

That update must include:

1. The exact commands run.
2. The exact green test outputs or summarized pass counts.
3. Which families and route lanes are Tier 2 release-grade.
4. Which families and route lanes are Tier 3 release-grade.
5. Which families or route lanes remain provisional and why.
6. Any residual risk that still blocks promotion.

Do not update the Russian implementation doc early with aspirational claims.

---

## 18. Suggested implementation order

1. Freeze the statistical methodology contract, route-lane schema, and evidence-tier rules.
2. Harden corpus intake and provenance tests.
3. Add deterministic rule verifiers with pinned upstream-rule references.
4. Build the route-lane baseline generator and generated baseline artifacts.
5. Add handshake acceptance and ServerHello corroboration suites.
6. Add multi-dump runtime suites for already-supported non-Windows families.
7. Promote or demote advisory-backed active profiles based on real evidence.
8. Add Windows families only when the reviewed corpus exists.
9. Run nightly-scale Monte Carlo, fuzz, and stress.
10. Update `docs/Plans/STEALTH_IMPLEMENTATION_RU.md` only after all gates are green.

---

## 19. Final rule

If a new community dump contradicts the current generator, assume the generator may be wrong until the discrepancy is explained. The correct action is not to loosen the test. The correct action is to classify the discrepancy, determine whether it represents a new family, a real bug, a route-specific variant, a resumption artifact, or a corpus defect, and only then decide how to change code or expectations.

That is the standard required for nation-state DPI resistance.