<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Hardening Master Plan

**Plan ID:** fingerprint-hardening-master-plan-2026-05-24  
**Date:** 2026-05-24  
**Status:** Authoritative  
**File policy:** This is the single active fingerprint hardening plan for this initiative.  
**Supersedes:**

1. `docs/Plans/FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25.md`
2. `docs/Plans/FINGERPRINT_HARDENING_PLAN_FINAL_AUDIT_2026-04-25.md`
3. `docs/Plans/FINGERPRINT_TEST_AND_PIPELINE_HARDENING_PLAN_2026-05-09.md`

**Primary Goal:** Make the fingerprint pipeline release-trustworthy under high-budget adaptive DPI by grounding every release-critical claim in real reviewed captures, deterministic fail-closed validation, adversarial tests, and statistically disciplined reporting.

**Threat Context:** RU censorship hardens continuously, ECH is blocked in RU, RU-to-non-RU QUIC is treated as blocked, and fallback behavior itself is fingerprinted. The pipeline must therefore match real browser behavior where evidence exists, fail closed where evidence is missing, and never let lossy or advisory evidence masquerade as release-grade proof.

**Non-negotiable execution method:** `Contract -> Attack -> Red -> Green -> Survive -> Refactor`. No implementation change ships before failing tests exist for the exact defect or attack class being fixed.

## 0. Non-Negotiable Rules

1. Real fixtures are the source of truth. Agents must derive expectations from `docs/Samples/Traffic dumps/` and the checked-in artifacts under `test/analysis/fixtures/`; they must not invent browser behavior.
2. Reviewed and imported lanes stay separate. Imported evidence is informational and never release-gating.
3. Exact fields outrank statistical surrogates. If an exact reviewed value exists, a lossy summary may inform diagnostics but may not overrule the exact value.
4. Missing evidence is `unavailable`, not zero, not pass, and not advisory prose. Unavailable metrics remain fail-closed for release claims.
5. Every new test lives in its own file. No inline tests in production sources, no bundled multi-concern tests, and no relaxing red tests to fit broken code.
6. Route policy remains fail-closed. RU and unknown routes keep ECH disabled, QUIC remains blocked by release policy, and fallback transitions must not emit a unique wire image.
7. All work must respect project architecture, the repository TDD rules, and OWASP ASVS L2 security requirements. Parser and generator code must validate untrusted fixture inputs at the first boundary and reject malformed data.
8. Agents must use SocratiCode at phase boundaries to confirm the owning code path, the blast radius of planned changes, and the generated-artifact dependencies before refactors or schema changes.
9. Reviewed evidence must carry provenance sufficient for deduplication and independence accounting. Missing `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, or `parser_version` in reviewed lanes is release-blocking until corrected.
10. Changing `source_path` alone may not create a new independent source.
11. Generated seeds are runtime stress inputs, not independent browser evidence.
12. Missing `scenario_id` may not be replaced with `artifact_path.stem` for Tier1-or-higher counting, confidence intervals, or release evidence.
13. The source-kind vocabulary is canonical and must remain consistent across extractor, summaries, baselines, renderers, registries, CI, and operator documentation.
14. Agents must refresh generated artifacts in topological order from the Artifact Dependency DAG; hand-editing generated headers, generated JSON, or generated docs is invalid.
15. Every listed defect candidate must be re-verified at current `HEAD` by a failing test, contract probe, or reproducible script output before implementation starts; stale assumptions are not actionable evidence.
16. Every implemented fix must carry red-to-green proof: one failing command before code changes and the same command passing after changes.
17. One slice has exactly one writing owner at a time. Parallel agent work may gather read-only evidence in parallel, but write phases are serialized per owning file surface.
18. Release-facing statistical claims are invalid unless artifacts disclose denominator and method fields (`x_pass`, `n_cluster`, `cluster_level`, `ci_method`, and where applicable `bootstrap_resamples`).
19. Generated release artifacts must preserve lineage ordering from upstream producers; if lineage cannot be proven, status remains fail-closed.
20. SocratiCode must be green before write phases. If the index is stale, unavailable, or the owning path cannot be found, the slice is blocked until the index is refreshed and the agent can record the relevant symbol, flow, or impact output.
21. Agents must not review their own slice as the final quality gate. The implementing agent supplies red-to-green evidence; a separate verifier or human confirms closeout.
22. A red test is presumed to reveal a product or pipeline defect until proven otherwise. Test relaxation requires a written, file-local explanation of why the original expectation was invalid and which contract supersedes it.
23. Every release-facing statistic must be reproducible with deterministic inputs: fixed `now_utc`, explicit seed derivation, stable fixture ordering, and the exact command used to regenerate the artifact.
24. Parsing of release-critical wire fields must be single-owner and reusable. Duplicated `0x002B` parsing logic across extractor, loaders, summaries, and matchers is a defect unless one implementation delegates to the canonical helper.
25. Deterministic structure outranks convenience. If an AI agent can make a fix either by adding a narrow, traceable data path or by adding an implicit heuristic, it must choose the traceable data path.
26. Do not relax the first red test. The first failing test for a slice is attack evidence; it may only be changed after the agent writes a file-local explanation proving that the test contradicted reviewed fixture truth or an explicit contract.
27. Every phase must persist an Agent Gate Artifact under `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/` before handoff. Chat-only claims are not evidence.
28. Release-family labels must be generation-aware when exact wire fields differ by browser or OS generation. A broad label such as `apple_ios_tls` may not mix iOS 17.2, iOS 18.7, and iOS 26 evidence unless every release-critical exact field is proven identical for the merged claim.
29. The canonical Python parser for ClientHello `supported_versions` and the canonical C++ test helper for generated wire parsing must share the same positive, negative, adversarial, fuzz, and stress corpus. No agent may claim a parser fix by changing only one language.
30. Agent plans must name the exact real fixture anchors they use. A statement such as "browser-like" or "typical Chrome" is not an implementation requirement unless backed by a checked-in raw dump or reviewed fixture path.
31. Every release-critical fingerprint gate must have at least one controlled mutant that proves the test can fail when the masking-relevant field is damaged. A test that only passes on good fixtures and has no hostile mutant is advisory, not release-grade.
32. Raw-to-fixture differential reproduction is mandatory for reviewed fixture schema changes: regenerated fields must be byte-identical for release-critical extracted fields, or the discrepancy must block the slice until the parser, extractor, or fixture provenance defect is understood.
33. Every release-facing predicate must carry an explicit `cohort_id` in addition to `family_id`. If exact reviewed fields prove that two browser or OS generations differ, they are separate cohorts until a red-to-green test proves a safe merge.
34. Real dump path spelling is evidence. Agents may not normalize away spaces, punctuation, case, Cyrillic characters, or Unicode composition when joining raw dumps to reviewed fixtures; path alias handling must be tested as a hostile input.
35. A test suite that cannot be shown to fail on a controlled masking-damaging mutant is not release-grade, even if it passes every current real fixture.
36. Checked-in provenance paths must be portable and repository-scoped. Absolute workspace prefixes such as `/home/<user>/...` are legacy migration inputs only; release artifacts must preserve the exact checked-in path spelling under `docs/Samples/Traffic dumps/` without leaking local machine paths.
37. Artifact freshness must be content-addressed. Timestamp ordering is a diagnostic hint, not proof; release artifacts must record upstream input hashes, generator version, command line, and sorted input list so stale regenerated outputs cannot pass by clock skew.
38. ECH and QUIC gates are two-layer decisions: browser-fidelity evidence describes what real browsers emitted on the captured route, while route policy may suppress ECH or QUIC for RU and unknown lanes. Agents must test both layers and must not use RU blocking as a shortcut for non-RU browser mimicry.
39. Small-sample statistics must be worded conservatively. A `1/1` or `2/2` exact pass is an anchor, not "100% fidelity"; every percentage must carry its clustered numerator, denominator, Wilson lower bound where binary, and the exact cohort scope.
40. AI agents must stop instead of guessing when a raw dump, reviewed fixture, SocratiCode owner path, or generated-artifact lineage edge cannot be found. Missing evidence is a blocker, not an invitation to infer browser behavior from a profile name.
41. Release predicates, thresholds, confidence methods, and mutation expectations must be pre-registered before the first red run for a slice. Agents may not tune thresholds, cohort boundaries, or wording after seeing failures unless they record a new red reason and invalidate the previous statistical claim.
42. Every capture-derived expectation must name the selected packet tuple: raw dump path, `source_sha256`, `frame_number`, `tcp_stream`, direction, record offset or reassembly source, and parser version. A fixture derived from the wrong stream, retransmission, server-to-client packet, or fallback reassembly is invalid evidence.
43. GREASE is release-critical where real fixtures show GREASE slots. Exact non-GREASE vectors must be paired with tests for valid GREASE values, slot positions, and raw extension-body length so a generated wire image cannot pass by preserving only the lossy non-GREASE view.
44. ECH policy tests must include plaintext-name leakage checks. If a route disables ECH or QUIC, fallback and error paths still may not emit a sensitive or unique target-name probe unless an explicit reviewed cover-name policy and real fixture anchor permit that wire image.

## 1. Verified Repository Reality

### 1.1 Real Reviewed Fixture Truth

1. `test/analysis/fixtures/clienthello/ios/safari26_3_1_ios26_3_1_a.clienthello.json` contains extension `0x002B` body `063a3a03040303`.
2. `test/analysis/fixtures/clienthello/ios/chrome147_0_7727_47_ios26_4_a.clienthello.json` contains extension `0x002B` body `062a2a03040303`.
3. In both fixtures, after GREASE removal the ordered `supported_versions` vector is exactly `[0x0304, 0x0303]`.
4. Therefore any modern iOS 26 Apple TLS release candidate for Safari 26.x, Chrome 146/147 iOS, Brave 1.88 iOS, or Firefox 149.2 iOS that advertises TLS 1.1 or TLS 1.0 is wrong, regardless of whether a lossy JA4-like summary still says "TLS 1.3 present".
5. The reviewed fixture chain already contains the decisive ground truth. The escape risk is pipeline modeling, not lack of capture evidence.
6. The corresponding raw captures exist under `docs/Samples/Traffic dumps/iOS/`, including `Ios 26.3.1, Safari 26.3.1.pcap` and `iOS 26.4, Google Chrome 147.0.7727.47.pcap`; reviewed truth must be regenerated from real dumps, not manually inferred.
7. Older reviewed iOS 17.2 and iOS 18.7 Safari captures are real and contain the ordered non-GREASE vector `[0x0304, 0x0303, 0x0302, 0x0301]`; they must not be pooled with modern iOS 26 Apple TLS for a release claim that asserts `[0x0304, 0x0303]`.
8. The correct architectural response is generation-aware family classification plus exact-field tests, not deleting older fixtures and not weakening the modern iOS 26 expectation.

### 1.2 Confirmed Current Pipeline Gaps

| ID | Severity | File | Verified issue |
|---|---|---|---|
| FP-01 | Critical | `test/stealth/CorpusStatHelpers.h` | JA4 segment A collapses `supported_versions` to the maximum non-GREASE version, which hides exact vector drift. |
| FP-02 | Critical | `test/analysis/merge_client_hello_fixture_summary.py` | Reviewed summary headers do not emit exact `supported_versions`, so later stages cannot consume them as first-class reviewed facts. |
| FP-03 | Critical | `test/analysis/build_family_lane_baselines.py` | Exact invariants do not include `supported_versions`, and family-lane baselines are therefore blind to this release-critical field. |
| FP-04 | Critical | `test/stealth/FamilyLaneMatchers.cpp` | Exact family matching does not reject wrong `supported_versions`. |
| FP-05 | High | `test/analysis/build_family_lane_baselines.py` | `classify_family_id` still folds Firefox iOS into `apple_ios_tls`; this is only safe if every release-critical exact field is proven identical, which is not currently documented or enforced. |
| FP-06 | High | `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp` | Nightly gates validate counts, envelopes, and ECH presence, but not authoritative exact fields such as `supported_versions`. |
| FP-07 | High | `test/stealth/ReviewedFamilyLaneBaselines.h` | The generated `apple_ios_tls` baseline currently has sparse exact invariants, which is too permissive for release-critical iOS fidelity claims. |
| FP-08 | High | `test/analysis/build_transport_coherence_status.py` | Transport gating is threshold-based and correctly distinguishes `available` from `unavailable`, but it does not yet attach confidence reporting or independence-aware summaries. |
| FP-09 | High | `test/analysis/build_family_lane_baselines.py` | `_source_identity` currently includes `source_path`, so copied or renamed captures can inflate independent-source counts even when `source_sha256` proves the underlying capture is the same. |
| FP-10 | High | `test/analysis/extract_client_hello_fixtures.py` | Extracted fixture JSON already persists exact fields such as `non_grease_cipher_suites` and `non_grease_supported_groups`, but it does not yet persist non-GREASE `supported_versions`. |
| FP-11 | Critical | `test/analysis/build_family_lane_baselines.py` | `load_samples()` currently substitutes `artifact_path.stem` when `scenario_id` is missing, so independent-session counts are not fail-closed today. |
| FP-12 | High | `test/analysis/build_family_lane_baselines.py` | `_merge_exact_invariants()` collapses list disagreement to empty vectors without a reasoned diagnostic, so mixed-family instability can masquerade as sparse exact invariants. |
| FP-13 | High | `test/analysis/merge_client_hello_fixture_summary.py` | The reviewed summary header exports `source_sha256`, `scenario_id`, `route_mode`, and `parser_version`, but not `source_kind` or exact `supported_versions`, so end-to-end provenance and version audits are incomplete in generated reviewed artifacts. |
| FP-14 | High | `docs/Documentation/FINGERPRINT_OPERATIONS_GUIDE.md` | Operator documentation still contains stale prose implying missing SYN-phase transport evidence degrades to `0.0`, while the code and contracts require metric value `null` plus `availability = unavailable`. |
| FP-15 | High | `test/analysis/render_fingerprint_policy_artifacts.py` | Release evidence rendering does not yet require cluster-level confidence metadata fields for binary predicates, so downstream summaries can become statistically under-specified. |
| FP-16 | High | `.github/workflows/fingerprint-policy-integrity.yml` | Drift checks regenerate artifacts but do not yet assert explicit cross-artifact lineage ordering from producer timestamps, so stale downstream summaries can pass if not directly diffed. |
| FP-17 | Critical | `test/analysis/common_tls.py` | Shared ClientHello and ServerHello loaders still fall back to `artifact_path.stem` when `scenario_id` is missing, so the invalid-session-count bug is not limited to the family-baseline generator. |
| FP-18 | High | `test/analysis/common_tls.py`, `test/analysis/extract_client_hello_fixtures.py`, `test/analysis/build_family_lane_baselines.py` | The source-kind vocabulary is not single-owned: the extractor accepts only a subset of authoritative kinds while the baseline builder separately hardcodes authoritative and advisory sets. |
| FP-19 | Critical | `test/analysis/common_tls.py` | The shared `ClientHello` model has no first-class `supported_versions` field, so downstream tooling cannot consume exact version vectors through the common loader even after extraction is fixed. |
| FP-20 | High | `test/analysis/common_tls.py` | `_infer_tls_gen()` currently infers TLS generation by searching for the substring `0304` inside raw extension bodies instead of using the canonical parser, so malformed or unrelated bytes can make a sample look TLS 1.3-capable. |
| FP-21 | Critical | Test architecture | Existing exact-field tests do not yet have a systematic mutation-adequacy gate. A lane can pass good fixtures while still being unable to detect a controlled downgrade, ordering drift, provenance alias, or route-policy leak. |
| FP-22 | High | `test/analysis/fixtures/clienthello/**/*.clienthello.json`, `test/analysis/common_tls.py` | All current reviewed ClientHello artifacts store `source_path` as an absolute workspace path. That is non-portable, leaks local path context, and makes path identity dependent on the agent machine rather than the checked-in raw dump path. |
| FP-23 | High | Generated release artifacts and CI lineage checks | Lineage is described primarily by producer timestamps today; timestamp-only freshness can be defeated by clock skew, copied generated files, or stale downstream artifacts with newer mtimes. Release lineage must include upstream content hashes and generator command metadata. |
| FP-24 | High | Release predicate and statistical policy surfaces | There is no explicit pre-registered release-predicate manifest tying each predicate to its cohort, denominator, confidence method, threshold, and mutation IDs before the first red run. That leaves room for threshold tuning or optional stopping after outcomes are known. |
| FP-25 | High | `test/analysis/extract_client_hello_fixtures.py`, `test/analysis/common_tls.py` | Capture selection and stream reassembly are release-critical but are not yet pinned by adversarial tests for wrong direction, retransmission, multi-ClientHello streams, truncated records, or fallback reassembly from unrelated bytes. |
| FP-26 | High | `td/mtproto/BrowserProfile.cpp`, `td/mtproto/ClientHelloExecutor.cpp`, C++ fixture matchers | Exact non-GREASE checks can still miss GREASE-slot damage: invalid GREASE values, missing GREASE slots, or raw extension-body length drift can preserve `[0x0304, 0x0303]` while changing the browser fingerprint. |
| FP-27 | Critical | Runtime route transition and ECH/QUIC fallback tests | Existing route-policy assertions must explicitly prove that ECH-disabled or QUIC-blocked fallback paths do not leak sensitive plaintext names or emit unique target-probe traffic under RU, unknown, error, and partial-blocking routes. |

### 1.3 Transport And Route Reality

1. `test/analysis/build_transport_coherence_status.py` already treats missing SYN-phase transport evidence as `unavailable`; the plan must preserve that fail-closed design rather than reintroduce fake `0.0` scores.
2. `test/analysis/render_fingerprint_policy_artifacts.py` enforces `pass`, `fail`, or `pending` at the top-level transport artifact while `metric_availability` carries the metric-level availability model. Therefore top-level transport status remains `pending` while individual missing metrics remain `unavailable`.
3. `test/analysis/fingerprint_trust_tiers.json` currently defines Tier0-Tier4 using minimum counts, sources, and sessions. Those thresholds remain the repository baseline, but distributional claims must become more statistically disciplined.
4. Active probing and route policy are already part of release semantics. They must stay integrated with the same reviewed-lane evidence model rather than becoming a side channel or nightly-only footnote.

### 1.4 Provenance And Independence Reality

1. Reviewed fixture JSON already carries `source_kind`, `source_path`, `source_sha256`, and `scenario_id`.
2. `extract_client_hello_fixtures.py` currently emits `non_grease_cipher_suites` and `non_grease_supported_groups`, but it does not yet emit `supported_versions` as a first-class extracted field.
3. `build_family_lane_baselines.py` currently falls back to `artifact_path.stem` when `scenario_id` is missing; release counting must remove that fallback rather than normalize it into fake independent sessions.
4. The source-kind vocabulary is not yet surfaced end-to-end in generated reviewed artifacts, even though authoritative-versus-advisory classification depends on it.
5. Therefore the statistically correct boundary for independence is deduplicated capture and session metadata, not filenames, path aliases, `artifact_path.stem`, or Monte Carlo seed counts.
6. A current reviewed-fixture census found 134 raw dump files, 106 reviewed ClientHello artifacts, no missing required provenance fields among `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, and `parser_version`, zero checked-in reviewed `source_path` SHA-256 mismatches, and 458 reviewed ClientHello samples with extension `0x002B` that still lack first-class `non_grease_supported_versions` despite carrying raw extension bodies.
7. SocratiCode impact analysis shows `test/analysis/common_tls.py` has broad blast radius across fixture loading, smoke checks, registry generation, and many contract tests; any loader semantics change must be treated as a shared boundary change rather than a local helper tweak.
8. `source_sha256` must be verified against the bytes at `source_path` during the census. A path that exists but hashes differently is a provenance failure, not a warning.

### 1.5 Current Fixture Census And Scope Guardrails

This section pins the refreshed evidence snapshot used to improve this plan. Implementing agents must recompute it at Phase 0A, but they may not replace it with guesses.

| Evidence item | Current observed value | Release interpretation |
|---|---:|---|
| Raw files under `docs/Samples/Traffic dumps/` | 134 raw dump files | Reviewed fixture refreshes must trace back to these captures or a documented reviewed source. |
| Reviewed ClientHello fixture artifacts | 106 reviewed ClientHello artifacts | All current reviewed artifacts are `source_kind = browser_capture` and `route_mode = non_ru_egress`. |
| Reviewed ClientHello samples carrying extension `0x002B` | 458 reviewed ClientHello samples with extension `0x002B` | Every one must gain first-class raw and non-GREASE `supported_versions` fields. |
| Missing first-class `supported_versions` in reviewed samples | 458 | This is a schema and loader defect, not an evidence gap. |
| Deduplicated authoritative capture identities `(source_kind, source_sha256)` | 106 | This is the current conservative `n_capture` / `n_source` ceiling before richer source metadata exists. |
| Independent reviewed sessions with non-empty `scenario_id` | 106 | This is the current `n_session` ceiling; missing future sessions must be `unavailable`, not synthesized. |
| Reviewed provenance missing-field count for `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, `parser_version` | 0 | Future missing fields are regressions and release-blocking. |
| Checked-in reviewed `source_path` SHA-256 mismatches | 0 | Future hash mismatches block fixture trust. |
| Reviewed ClientHello artifacts with absolute `source_path` values | 106 | This is a portability and diagnostics defect. The exact raw dump spelling must be preserved as a repository-relative path under `docs/Samples/Traffic dumps/`, while legacy absolute prefixes are accepted only as migration input after proving they resolve inside the repository. |
| Raw dump names containing non-ASCII path bytes | at least 5 observed in census sample | Cyrillic, punctuation, commas, and spaces are real evidence. Path tests must include these names and must reject Unicode-normalized aliases as distinct release identities unless the original checked-in path is preserved. |

Observed non-GREASE `supported_versions` distribution from current reviewed ClientHello samples:

| Ordered non-GREASE vector | Count | Scope rule |
|---|---:|---|
| `[0x0304, 0x0303]` | 449 | Modern reviewed lanes, including all current iOS 26 Apple TLS samples, use this vector. |
| `[0x0304, 0x0303, 0x0302, 0x0301]` | 9 | Legacy Windows and older iOS 17.2 or iOS 18.7 Safari captures are real but must not be pooled with modern iOS 26 Apple TLS. |

Required raw iOS anchors include:

1. `docs/Samples/Traffic dumps/iOS/Ios 26.3.1, Safari 26.3.1.pcap`
2. `docs/Samples/Traffic dumps/iOS/iOS 26.4, Google Chrome 147.0.7727.47.pcap`
3. `docs/Samples/Traffic dumps/iOS/iOS 17.2.1, Safari 17.2, auto iOS 17.2.1, auto Safari 17.2.pcap`
4. `docs/Samples/Traffic dumps/iOS/iOS 18.7.6. Safari 18.7.6.pcap`

Required reviewed fixture anchors for the iOS generation split include:

| Role | Fixture | `fixture_id` | `frame_number` | `tcp_stream` | `0x002B` body | Ordered non-GREASE vector |
|---|---|---|---:|---:|---|---|
| Modern Safari positive control | `test/analysis/fixtures/clienthello/ios/safari26_3_1_ios26_3_1_a.clienthello.json` | `safari26_3_1_ios26_3_1_a:frame76` | 76 | 1 | `063a3a03040303` | `[0x0304, 0x0303]` |
| Modern Chrome iOS positive control | `test/analysis/fixtures/clienthello/ios/chrome147_0_7727_47_ios26_4_a.clienthello.json` | `chrome147_0_7727_47_ios26_4_a:frame148` | 148 | 2 | `062a2a03040303` | `[0x0304, 0x0303]` |
| Legacy Safari out-of-cohort control | `test/analysis/fixtures/clienthello/ios/safari17_2_ios17_2_1_087f3601.clienthello.json` | `safari17_2_ios17_2_1_087f3601:frame56` | 56 | 2 | `0a3a3a0304030303020301` | `[0x0304, 0x0303, 0x0302, 0x0301]` |
| Legacy Safari out-of-cohort control | `test/analysis/fixtures/clienthello/ios/safari18_7_6_ios18_7_6.clienthello.json` | `safari18_7_6_ios18_7_6:frame184` | 184 | 5 | `0afafa0304030303020301` | `[0x0304, 0x0303, 0x0302, 0x0301]` |

These anchors are deliberately concrete. If an implementing agent cannot find one of these fixture paths at current `HEAD`, the slice blocks until the census explains whether the fixture was renamed, regenerated, or removed; the agent must not substitute a browser-brand guess.

Required path-portability anchors include at least one Cyrillic or non-ASCII raw dump path from the current corpus, such as:

1. `docs/Samples/Traffic dumps/iOS/iOS_18_7,_Brave_Версия_1_88_137,_auto_iOS_18_7,_auto_unknown_bro.pcap`
2. `docs/Samples/Traffic dumps/Android/Андроид_14,_Adblock_browser_3_11_1,_auto_Android_10,_auto_Chromi.pcap`
3. `docs/Samples/Traffic dumps/Windows/Windows_11_22Н2_22621_4317_Google_Chrome_146_0_7680_178_auto_Win.pcap`

The Phase 0A census must emit these counts as `n_sample_raw`, `n_capture`, `n_source`, `n_session`, `n_supported_versions_002b`, `n_supported_versions_first_class`, `n_source_hash_mismatch`, `n_absolute_source_path`, `n_source_path_outside_repo`, and `n_unicode_path_alias_collision`. Agents must treat count changes as evidence requiring explanation, not as automatic progress.

### 1.6 SocratiCode Preflight Snapshot

1. The repository was indexed and the watcher was active when this plan was refreshed; implementing agents must rerun `codebase_status` because this snapshot is not a substitute for current preflight.
2. `build_family_lane_baselines.py` flows from `main()` to `generate_for()`, `load_samples()`, `build_baselines()`, `_merge_exact_invariants()`, and `render_header()`. This is the controlling path for family exact-invariant and tier-count changes.
3. SocratiCode identified direct blast radius from `build_family_lane_baselines.py` into `build_fingerprint_stat_baselines.py`, `render_family_lane_tier_status.py`, and the family-baseline evidence, stress, and renderer tests.
4. SocratiCode identified `merge_client_hello_fixture_summary.py` as directly covered by generated-header escaping, reviewed-summary coverage, and reviewed-header tests.
5. SocratiCode identified `common_tls.py` as a shared anti-corruption boundary with dozens of direct consumers. Do not change its fallback semantics without first adding contract, adversarial, integration, fuzz, and stress coverage for the affected loaders.
6. Current SocratiCode health was green during this refresh: Docker, managed Qdrant, external Ollama, and `nomic-embed-text` were reachable; `/home/david_osipov/tdlib-obf` was indexed at `2026-05-24T14:15:27.263Z` with 3,529 files, watcher active, and a code graph covering 2,992 files built at `2026-05-24T14:14:49.827Z`. This proves the plan update used live repository structure, but every implementing agent must rerun status because this snapshot ages immediately.
7. SocratiCode impact for `common_tls.py` showed 53 impacted files across loaders, smoke checks, registry generation, fixture intake, pairing, malformed-packet, fuzz, and stress tests. Any `ClientHello` model or identity change must be planned as a shared boundary change.
8. SocratiCode search confirmed duplicated `supported_versions` parsing exists in `CorpusStatHelpers.h`, Safari invariance tests, GREASE-slot tests, and structural-invariant tests. This is why the parser ownership work must cover both the canonical Python parser and canonical C++ test helper.
9. SocratiCode flow for `build_family_lane_baselines.py` confirms `main()` reaches `generate_for()`, `load_samples()`, `build_baselines()`, `_merge_exact_invariants()`, and `render_header()`. Any baseline or exact-invariant slice must prove this path red-to-green, not only isolated helpers.
10. SocratiCode symbol exploration for `load_clienthello_artifact()` found 58 callers; changing loader identity or exact-field parsing is a shared-boundary change, not a local refactor.
11. SocratiCode flow for `merge_client_hello_fixture_summary.py` confirms `main()` reaches `merge_artifacts()`, `emit_fixture_block()`, and generated C++ vector emitters; reviewed-header fields must be added through that path rather than patched in the generated header.
12. SocratiCode impact for `merge_client_hello_fixture_summary.py` is concentrated in generated-header escaping, reviewed-summary coverage, and reviewed-header tests; those are the minimum red-to-green tests for summary slices.

### 1.7 Fingerprint-Damaging Bug Classes The Plan Must Catch

The tests produced by this plan are release-defense tests, not cosmetic regression tests. A release-critical gate is incomplete unless it catches at least one real masking-damaging bug class from this table and pins the corresponding positive real fixture control.

| Bug class | Real fixture anchor or evidence source | Required negative proof |
|---|---|---|
| TLS version-vector downgrade or expansion | iOS 26 Safari and Chrome fixtures with `0x002B` bodies `063a3a03040303` and `062a2a03040303` | Mutant reintroduces `0x0302` or `0x0301` after GREASE removal and fails extractor, header, family baseline, matcher, and nightly exact gates. |
| GREASE evasion of exact checks | Same iOS 26 fixtures plus all reviewed samples carrying extension `0x002B` | Mutant changes only GREASE values while damaging the non-GREASE vector; lossy max-version summaries must not pass it. |
| Extension-order drift | Reviewed `extension_types` and `non_grease_extensions_without_padding` in fixture JSON | Mutant swaps two non-padding non-GREASE extensions and fails family exact or set-membership gates for the scoped family. |
| Cipher-suite order drift | Reviewed `non_grease_cipher_suites` in fixture JSON | Mutant keeps the same cipher set but changes order; tests fail on ordered exact comparison, not only set comparison. |
| Supported-groups or key-share order drift | Reviewed `non_grease_supported_groups` and `key_share_entries` | Mutant shuffles non-GREASE groups or removes a required post-quantum/curve group and fails before release evidence is computed. |
| ALPN drift | Reviewed `alpn_protocols`, especially Safari `h2,http/1.1` versus Chrome iOS observed `http/1.1` lane | Mutant changes ALPN order or deletes `h2`; grouping logic must split or block instead of silently emptying invariants. |
| ECH route-policy leak | Route policy plus reviewed non-RU browser captures | RU and unknown route mutants emit ECH; route-transition tests fail even if browser mimicry outside Russia would allow ECH. |
| QUIC blocked-lane leak | RU-to-non-RU policy and active-probing evidence | Mutant enables QUIC or emits QUIC-looking probe packets on blocked lanes; route-policy and active-probing gates fail. |
| Fallback transition fingerprint | Runtime route transition path and masking-on captures | Selective-blocking mutant causes a distinctive retry, timing, padding, or probe signature; integration tests fail with a stable reason. |
| Record-size and segmentation drift | Reviewed `record_length`, `record_lengths`, `handshake_length`, and first-flight artifacts | Mutant preserves semantic TLS fields but changes first-flight record segmentation; exact or transport gates fail if the field is release-critical for that lane. |
| Source or session independence inflation | Reviewed fixture provenance fields and raw dump SHA-256 hashes | Copied fixture or Unicode-normalized path keeps the same `source_sha256`; `n_capture`, `n_source`, `n_session`, tier, and confidence denominator do not increase. |
| Advisory evidence contamination | Imported registry, `source_kind`, trust-tier spec, and reviewed registry | Advisory or imported fixture tries to satisfy reviewed release thresholds; release renderer blocks it and reports diagnostic-only status. |
| Parser leniency or data corruption | Canonical Python parser and canonical C++ test helper shared corpus | Malformed, odd-length, trailing-byte, duplicate, oversized, and GREASE-only extension bodies either fail closed or preserve exact audited semantics with typed diagnostics. |
| Statistical pseudoreplication | Fixture census and clustered release evidence | Many frames from one capture or many Monte Carlo seeds try to promote tier or confidence; cluster-collapsed denominators remain unchanged. |
| Absolute path provenance leak | Current reviewed fixtures with absolute `source_path` values | Fixture copied to another checkout or agent home directory must still map to the same checked-in raw dump and must not leak the local workspace root into generated release artifacts. |
| Timestamp-only lineage bypass | Generated release artifacts and producer DAG | Downstream artifact with a fresh timestamp but stale upstream content hash fails lineage validation before release evidence is rendered. |
| GREASE-slot fingerprint drift | Modern iOS fixture `0x002B` bodies and reviewed extension order | Mutant preserves `[0x0304, 0x0303]` but removes the GREASE element, uses a non-GREASE placeholder, changes the GREASE slot, or changes raw vector length; exact raw-body or generated-wire gates fail. |
| Capture-selection or reassembly drift | Raw dump anchors with selected `frame_number` and `tcp_stream` | Mutant swaps direction, selects a retransmission, concatenates unrelated stream bytes, or chooses a second ClientHello; raw-to-fixture reproduction and fixture identity tests fail before exact fields are trusted. |
| Plaintext-name leakage during fallback | ECH/route policy plus runtime route-transition captures | RU, unknown, error, and partial-blocking mutants expose a sensitive or unique target hostname in plaintext SNI or a probe packet; route-transition and ECH fallback tests fail even if ECH is intentionally disabled. |
| Statistical optional stopping or threshold overfit | Release predicate registry, handoff artifacts, and generated evidence | Agent changes a confidence floor, cohort boundary, or predicate set after seeing outcomes; pre-registration contracts reject the release artifact and require a new red-to-green slice. |

Agents must add more bug classes when a new release-critical field is introduced. They may not delete a bug class from this table unless a separate verifier proves that the field is no longer release-critical and updates the risk register, test matrix, and release blockers in the same change.

## 2. Authoritative Implementation Surfaces

1. Fixture extraction: `test/analysis/extract_client_hello_fixtures.py`
2. Reviewed summary generation: `test/analysis/merge_client_hello_fixture_summary.py`
3. Family baseline generation: `test/analysis/build_family_lane_baselines.py`
4. Generated reviewed fixture header: `test/stealth/ReviewedClientHelloFixtures.h`
5. Generated family baseline header: `test/stealth/ReviewedFamilyLaneBaselines.h`
6. Exact family matching: `test/stealth/FamilyLaneMatchers.cpp`
7. Lossy statistical helper path: `test/stealth/CorpusStatHelpers.h`
8. Nightly Monte Carlo gate: `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
9. Apple/iOS exact regression lane: `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`
10. Multi-dump iOS family lane: `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
11. Runtime wire generation: `td/mtproto/BrowserProfile.cpp`
12. Wire serializer: `td/mtproto/ClientHelloExecutor.cpp`
13. Transport status generator: `test/analysis/build_transport_coherence_status.py`
14. Release evidence renderer: `test/analysis/render_fingerprint_policy_artifacts.py`
15. Trust-tier spec: `test/analysis/fingerprint_trust_tiers.json`
16. Family-lane tier renderer: `test/analysis/render_family_lane_tier_status.py`
17. Family baseline evidence-tier contracts: `test/analysis/test_build_family_lane_baselines_evidence_tiers.py`
18. Family baseline stress contracts: `test/analysis/test_build_family_lane_baselines_stress.py`
19. Plan and policy contract surface: `test/analysis/test_fingerprint_hardening_plan_contract.py`
20. CI workflow contract: `.github/workflows/fingerprint-policy-integrity.yml`
21. Shared normalization and provenance helpers: `test/analysis/common_tls.py`
22. Raw dump importer and manifest builder: `test/analysis/import_traffic_dumps.py`
23. Imported registry generator: `test/analysis/generate_imported_fixture_registry.py`
24. Transport observation extractor: `test/analysis/extract_tcp_transport_signatures.py`
25. Active-probing status generator: `test/analysis/build_active_probing_status.py`
26. Operations guide contract surface: `docs/Documentation/FINGERPRINT_OPERATIONS_GUIDE.md`
27. Canonical Python parser owner for ClientHello `supported_versions`: `test/analysis/common_tls.py` or a narrow helper imported by it, the extractor, summary generator, and baseline generator.
28. Canonical C++ test helper owner for generated-wire `supported_versions`: `test/stealth/TlsHelloParsers.h` or a narrow helper imported by `FamilyLaneMatchers.cpp`, JA4 diagnostics, and C++ invariance tests.
29. Mutation adequacy catalog and reports: `test/analysis/fingerprint_mutation_catalog.json` or a narrow generated equivalent, plus per-slice handoff attachments.
30. Raw-to-fixture differential reproduction report: generated by the extractor or census phase and attached to the Agent Gate Artifact.
31. Plan handoff artifacts: `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/`
32. Raw dump path canonicalization and portability contracts: `test/analysis/test_fixture_source_path_portability_contract.py` or a narrow equivalent.
33. Content-addressed artifact lineage contracts: `test/analysis/test_artifact_lineage_content_hash_contract.py` or a narrow equivalent.
34. Capture-selection and reassembly contracts: `test/analysis/test_capture_selection_reassembly_contract.py` or a narrow equivalent.
35. Release predicate registry and optional-stopping contracts: `test/analysis/test_release_predicate_registry_contract.py` plus `test/analysis/test_outcome_tuned_threshold_adversarial.py` or narrow equivalents.
36. GREASE raw-wire validity tests: `test/stealth/test_tls_grease_slot_validity_contract.cpp` or a narrow equivalent sharing the parser corpus.
37. Plaintext-name leakage and route fallback tests: `test/stealth/test_tls_plaintext_name_leak_adversarial.cpp` or a narrow equivalent integrated with route transition tests.
38. Release predicate registry artifact: `test/analysis/fingerprint_release_predicates.json` or a deterministic generated equivalent consumed by release evidence rendering.

### 2.1 Artifact Dependency DAG

1. Raw reviewed captures under `docs/Samples/Traffic dumps/**` feed `test/analysis/extract_client_hello_fixtures.py`, which produces reviewed ClientHello fixture JSON under `test/analysis/fixtures/clienthello/**`.
2. Reviewed ClientHello fixture JSON feeds `test/analysis/generate_server_hello_fixture_corpus.py`, which produces reviewed ServerHello fixture JSON under `test/analysis/fixtures/serverhello/**`.
3. Reviewed ClientHello fixture JSON feeds `test/analysis/merge_client_hello_fixture_summary.py`, which produces `test/stealth/ReviewedClientHelloFixtures.h`.
4. Reviewed ClientHello fixture JSON feeds `test/analysis/build_family_lane_baselines.py`, which produces `test/stealth/ReviewedFamilyLaneBaselines.h`.
5. Reviewed ClientHello fixture JSON feeds `test/analysis/build_fingerprint_stat_baselines.py`, which produces `test/stealth/ReviewedFingerprintStatBaselines.h` and `test/analysis/fingerprint_stat_baselines.json`.
6. Transport observations under `test/analysis/transport_coherence_observations.json` feed `test/analysis/build_transport_coherence_status.py`, which produces `docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json`.
7. Active-probing observations under `test/analysis/active_probing_nightly_observations.json` feed `test/analysis/build_active_probing_status.py`, which produces `docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json`.
8. `test/analysis/fingerprint_trust_tiers.json` plus the generated transport and active-probing status artifacts feed `test/analysis/render_fingerprint_policy_artifacts.py`, which produces `docs/Generated/FINGERPRINT_RELEASE_EVIDENCE_POLICY.generated.json`, `docs/Generated/FINGERPRINT_TRUST_TIERS.generated.md`, and the generated trust-tier blocks embedded in the fingerprint documentation set.
9. The release predicate registry artifact feeds `test/analysis/render_fingerprint_policy_artifacts.py`, nightly exact gates, and CI contract tests; statistical release evidence must not be rendered without it.
10. AI agents must refresh this DAG from upstream truth to downstream artifacts. If a slice changes a producer, every downstream node must be regenerated after the producer is proven green.
11. Any release artifact that consumes upstream generated artifacts must expose lineage fields sufficient to prove that producer timestamps and producer inputs are newer-or-equal to the downstream rendering pass.
12. If lineage verification fails, the affected lane remains `pending` or `fail` by policy; no implicit promotion to pass is allowed.

### 2.2 Real Fixture Anchor Selection Protocol

Agents must use this protocol before writing any test or implementation change that asserts browser behavior.

1. Start from a checked-in raw dump under `docs/Samples/Traffic dumps/`, not from a browser brand assumption, JA4 string, or generated runtime profile.
2. Compute the raw dump SHA-256 and join it to reviewed fixtures through `source_sha256`; if no reviewed fixture matches, the slice is blocked until the capture is imported, reviewed, or explicitly marked as non-release evidence.
3. Record the raw dump path exactly as checked in, including spaces, commas, case, Cyrillic text, and Unicode composition. A normalized path may be a diagnostic alias, but it may not be a release identity.
4. Record the reviewed fixture path, `fixture_id`, `frame_number`, `tcp_stream`, `scenario_id`, `route_mode`, `source_kind`, `source_sha256`, and `parser_version` before deriving an expectation.
5. For ClientHello `supported_versions`, record both the raw extension `0x002B` `body_hex` and the ordered non-GREASE vector produced by the canonical parser. A lossy maximum-version summary is not sufficient evidence.
6. Select at least one positive real-fixture control for the target cohort and at least one out-of-cohort control when a family or generation split is involved. For modern iOS 26 Apple TLS, the required out-of-cohort controls include older iOS 17.2 or iOS 18.7 captures carrying `[0x0304, 0x0303, 0x0302, 0x0301]`.
7. If a fixture has multiple samples, decide whether the predicate is sample-level, capture-clustered, or session-clustered before computing any pass count.
8. Record packet direction, record source, and any reassembly fallback before deriving a release-critical field. If these are absent, the fixture may be used only as diagnostic evidence until remediated.
9. If a release-critical field is absent from the reviewed fixture but present in raw extension bodies, treat that as a schema defect and red-test seed. Do not infer the missing field in downstream tests without fixing the owning extractor or loader contract.
10. Any fixture expectation copied into C++ tests must have a Python-side source-of-truth test that proves the same value was derived from the reviewed fixture or raw dump.
11. Any runtime-generated wire expectation must be compared against reviewed fixture truth through generated headers or canonical helpers, not through ad hoc constants embedded in a production source.
12. Every Agent Gate Artifact must include the fixture anchor table used by the slice; the verifier must reject claims such as "browser-like", "Chrome-like", or "Safari-like" unless the exact raw dump and fixture anchors are listed.
13. If the reviewed fixture currently stores an absolute `source_path`, the agent must prove that removing the workspace prefix yields the exact checked-in path under `docs/Samples/Traffic dumps/` and that the hash still matches. The absolute prefix is not part of release identity and must not be emitted by new release artifacts.
14. Path comparisons must be byte-preserving after repository-root stripping only. Case folding, slash rewriting beyond platform separators, Unicode normalization, symlink escape, and punctuation simplification are hostile alias mutations and must have negative tests.

### 2.3 Agent Red-Test Evidence Standard

1. A red test is valid only if it fails because the current product or pipeline accepts a masking-damaging condition that the plan forbids, or because a required field or diagnostic is absent from the current pipeline.
2. A red test is invalid if it fails only because a file is missing, a test name is not discovered, a dependency is not installed, a path is misspelled, or the test fixture contradicts checked-in raw dump truth.
3. The first red command for a slice must be narrow and reproducible. The later green command must exercise the same behavior and may be wider, but it may not replace the original command.
4. The red failure excerpt must name the predicate, fixture anchor, mutated field, expected failure lane, and Risk IDs. A generic assertion failure with no field context is not enough for handoff.
5. Red tests must include both positive controls and controlled mutants where the gate is intended to catch a release-critical fingerprint bug. If a controlled mutant survives, the implementation or test architecture is wrong until proven otherwise.
6. Agents may add harness scaffolding before implementation, but they may not weaken the expected result after seeing the red failure unless a file-local comment proves the original expectation contradicted fixture truth or a stronger explicit contract.
7. Any red test that relies on time, randomness, file ordering, or generated seeds must pin `now_utc`, seed derivation, and sorted input order so the verifier can reproduce the failure.

## 3. Contract Snapshot

### CONTRACT: Reviewed Supported Versions Extraction

1. Input: raw extension `0x002B` body from reviewed fixture JSON.
2. Output: ordered non-GREASE `supported_versions` vector, preserved exactly as observed on wire.
3. Preconditions: the vector length byte is present, even, and consistent with the payload length.
4. Postconditions: malformed, truncated, or inconsistent payloads fail closed; valid payloads preserve order and values exactly.
5. Side effects: none beyond emitted checked-in fixture artifacts.

### CONTRACT: Canonical Supported Versions Parser

1. A single canonical Python parser owns `0x002B` body parsing for extractor, common loaders, summary generation, baseline generation, and Python tests.
2. Input validation happens at the first parse boundary: non-hex text, short body, odd vector length, declared-length mismatch, trailing bytes, or values outside `uint16` are rejected with a typed diagnostic.
3. GREASE removal is a view, not mutation. The artifact must be able to expose both the raw ordered vector and the ordered non-GREASE vector when needed for audits.
4. Duplicate non-GREASE values are not silently deduplicated. For reviewed browser-capture lanes they are release-blocking diagnostics unless a real capture plus browser-version note proves that browser legitimately emitted duplicates.
5. The helper must be fuzzed with random bytes and adversarial length prefixes; the accepted output must be deterministic and allocation-bounded.
6. A canonical C++ test helper must parse generated-wire `supported_versions` under the same contract. It may be implemented separately for language-boundary reasons, but it must be fed by the same golden corpus and adversarial vectors as the Python helper.
7. Parser-fix rule: no agent may claim a parser fix by changing only one language. A valid slice changes or proves both sides: fixture pipeline parsing and generated-wire parsing.
8. JA4 segment A may continue to report its lossy maximum-version summary only as diagnostics; exact gates must call the canonical helper or consume its first-class output.

### CONTRACT: Generation-Aware Family Scope

1. Family identifiers used for release evidence must encode enough generation context to avoid pooling exact-field-incompatible populations.
2. Modern iOS 26 Apple TLS samples with non-GREASE `supported_versions` `[0x0304, 0x0303]` may not be grouped with iOS 17.2 and iOS 18.7 samples that carry `[0x0304, 0x0303, 0x0302, 0x0301]` for any release claim about modern iOS 26 behavior.
3. If the implementation keeps a coarse `apple_ios_tls` label for compatibility, it must add a second release-evidence discriminator such as browser generation, OS major generation, or exact-field cohort; otherwise generation-aware family classification is required.
4. A family split is not complete until exact corpus tests, family baseline tests, nightly exact gates, and generated release evidence all agree on the same cohort boundary.

### CONTRACT: Cohort Identity And Release Predicate Scope

1. `family_id` is a coarse implementation or browser-stack grouping; `cohort_id` is the release-evidence stratum used for exact and statistical claims.
2. `cohort_id` must be deterministic from reviewed metadata and exact-field compatibility, such as OS major generation, browser generation, route lane, and stable exact-field cohort hash. It may not be derived from whether a candidate happens to pass.
3. A release predicate is scoped to `(family_id, cohort_id, route_lane, evidence_lane, predicate_id)`. Omitting `cohort_id` from generated release evidence is a schema defect.
4. If two samples in one family disagree on a release-critical exact field, the builder must either split the `cohort_id` or emit a blocking mixed-cohort diagnostic. It may not average, downgrade confidence, or empty the invariant silently.
5. Legacy iOS 17.2 and iOS 18.7 Apple TLS samples remain valid evidence for their own cohorts, but they are neither passes nor failures for a modern iOS 26 `supported_versions` predicate.
6. A cohort merge is allowed only after a red-to-green contract test proves that every release-critical exact field and denominator field remains unchanged by the merge.
7. Every confidence interval, bootstrap interval, tier claim, mutation report, and Agent Gate Artifact that names `family_id` must also name the `cohort_id` or explicitly mark the result `diagnostic_only`.

### CONTRACT: Reviewed Summary And Generated Headers

1. Reviewed headers must expose release-critical exact fields, including `supported_versions`, in generated artifacts.
2. Generated headers may not silently drop a populated reviewed field during refresh.
3. Generated headers remain generated-only files; agents must not hand-edit them.

### CONTRACT: Family Exact Invariants

1. A family-lane baseline must include every stable release-critical exact field that the reviewed corpus proves stable.
2. A family-lane may not quietly degrade to empty exact invariants for a release-critical field without emitting an explicit diagnostic and failing the corresponding contract tests.
3. If family grouping empties a critical invariant, the grouping is defective until proven otherwise.

### CONTRACT: Fixture Provenance And Independence Accounting

1. Reviewed fixture JSON must expose non-empty `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, and `parser_version` fields.
2. Capture identity for statistical independence is `(source_kind, source_sha256)`; `source_path` is provenance only. Changing `source_path` alone may not create a new independent source.
3. Session identity is `scenario_id`; if it is missing, the reviewed fixture may not contribute to Tier1-or-higher independence counts until remediated.
4. The same `source_sha256` may not quietly appear in both reviewed and imported lanes, or in conflicting family or route assignments, without a dedicated diagnostic and failing contract test.
5. `source_path` must exist for checked-in reviewed fixtures, and its computed SHA-256 must match `source_sha256` unless the artifact explicitly names a documented offline reviewed source.
6. The loader must report `n_sample`, `n_capture`, `n_source`, and `n_session` as separate fields; a missing field must be `unavailable`, not zero-filled.

### CONTRACT: Canonical Raw Dump Path Identity

1. The release identity of a raw dump path is the repository-relative, byte-preserving path under `docs/Samples/Traffic dumps/` plus the raw file `source_sha256`.
2. Absolute fixture `source_path` values are legacy compatibility inputs. Loaders may accept them only if they resolve inside the repository root and can be converted to the exact repository-relative checked-in path without changing spelling below the root.
3. New generated artifacts, release evidence, handoff records, and diagnostics intended for CI must emit the repository-relative raw dump path, not `/home/<user>/...` or any other local workspace prefix.
4. A path that resolves outside the repository, uses `..` to escape the allowed root, traverses a symlink escape, differs only by Unicode normalization, differs only by case on a case-insensitive host, or changes punctuation or spacing is a hostile alias until proven to point to the same checked-in file and must not increase independence counts.
5. The path-portability contract must include real corpus names containing spaces, commas, mixed case, and Cyrillic characters. Synthetic ASCII-only fixtures are insufficient.
6. Changing `source_path` representation may not change `source_sha256`, `scenario_id`, `fixture_id`, `cohort_id`, `n_capture`, `n_source`, `n_session`, tier, Wilson denominator, or mutation adequacy results.

### CONTRACT: Shared Fixture Loader Fail-Closed Identity

1. `common_tls.py` is the shared anti-corruption boundary for fixture artifacts. It may not silently synthesize `scenario_id`, `source_kind`, `source_sha256`, `route_mode`, or `parser_version` for reviewed release lanes.
2. Backward compatibility for old diagnostics may be explicit and non-release only; such paths must mark the identity status as `diagnostic_only` and must not feed Tier1-or-higher counts.
3. Missing `scenario_id` in ClientHello or ServerHello loaders must be observable to callers through an error or an unavailable identity state, not hidden by `artifact_path.stem`.
4. Any fixture loader change must run the common TLS contract suite, corpus smoke, registry-generation tests, and at least one fuzz or stress test because the blast radius spans dozens of consumers.

### CONTRACT: Reviewed Raw-Capture To Fixture Traceability

1. Reviewed fixture changes must be reproducible from raw captures under `docs/Samples/Traffic dumps/` or a documented reviewed capture source with the same `source_sha256`.
2. Hand-editing reviewed JSON to change exact wire fields is prohibited; reviewed fixture payload changes must flow through extractor or generator code.
3. A reviewed fixture refresh must preserve `source_sha256`, `source_path`, `parser_version`, frame identifiers, and the raw-to-derived mapping note in the agent handoff.

### CONTRACT: Capture Selection And Reassembly Integrity

1. A reviewed fixture sample is identified by `(source_sha256, frame_number, tcp_stream, direction, parser_version)` plus the repository-relative raw dump path. A generator may not derive release truth from a sample that lacks these selectors.
2. Extractors must distinguish client-to-server ClientHello packets from server-to-client packets, retransmissions, late records, HelloRetryRequest follow-up ClientHellos, and unrelated TCP stream bytes before parsing release-critical fields.
3. Fallback from per-frame record bytes to stream reassembly is allowed only when a typed record-length or handshake-length diagnostic proves the frame payload is incomplete, and the fallback must record the reassembly source and byte range.
4. If multiple ClientHello candidates exist in one raw dump, the fixture must either include all selected candidates with stable sample IDs or state the explicit selection rule. A hidden first-match heuristic is not release evidence.
5. Tests must include hostile captures or synthetic extractor rows for wrong direction, duplicate retransmission, partial record, second ClientHello, empty TCP payload, and stream bytes that parse only after unrelated concatenation.

### CONTRACT: Raw-to-fixture differential reproduction

1. Any extractor, parser, or fixture-schema slice must regenerate the affected reviewed fixture fields from the raw dump anchors and compare the extracted values against checked-in JSON.
2. The comparison must be byte-identical for release-critical extracted fields: raw extension bodies, ordered non-GREASE exact vectors, extension order, cipher-suite order, supported groups, ALPN, key-share groups, record lengths, handshake lengths, frame numbers, route metadata, and provenance hashes.
3. Reproduction must preserve real path spelling, spaces, case, commas, and non-ASCII characters. Unicode normalization, case-folding, path aliasing, or symlink traversal may not change `source_path`, `source_sha256`, `scenario_id`, or evidence counts.
4. A reproduction mismatch is a product or pipeline defect until proven otherwise. It may not be papered over by editing reviewed JSON by hand.
5. The Agent Gate Artifact must include the exact raw dump path, checked-in fixture path, extractor command, parser version, mismatch count, and a bounded diff of every mismatched release-critical field.

### CONTRACT: Fingerprint Mutation Adequacy

1. Every release-critical fingerprint gate must have at least one controlled mutant with a stable `mutation_id`, fixture anchor, mutated field, expected failing lane, and mapped Risk IDs.
2. A controlled mutant is a minimal hostile change to one masking-relevant property while preserving enough surrounding structure to avoid trivial parse failure unless the risk under test is parser rejection.
3. Required mutants include: TLS 1.1 or TLS 1.0 reintroduced into modern iOS 26 `supported_versions`; GREASE value changed while non-GREASE vector is wrong; extension body length altered with a plausible maximum TLS version; ALPN order changed; extension order changed; cipher-suite order changed; ECH emitted on RU or unknown route; QUIC enabled on a blocked lane; `source_path` copied or Unicode-normalized with unchanged `source_sha256`; missing `scenario_id`; and seed-only evidence inflated into `n_capture` or `n_session`.
4. The controlled-mutant suite must produce zero false-negative results for exact-field and policy gates: if a mutant survives the expected lane, the test architecture is defective even when good fixtures pass.
5. The positive side must also be checked: reviewed real fixtures from `docs/Samples/Traffic dumps/` must not become false-positive failures after a mutant harness is introduced.
6. Mutants are tests, not evidence. Mutated samples may never contribute to reviewed fixture counts, trust tiers, confidence intervals, or generated release evidence.
7. Mutation reports must disclose `mutation_id`, `risk_ids`, `fixture_anchor`, `raw_dump_anchor`, `mutated_field`, `expected_failure_lane`, `actual_failure_lane`, `false-negative`, and `false-positive` counts.

### CONTRACT: Session Identity And Cluster Collapse

1. Missing `scenario_id` may not silently fall back to `artifact_path.stem` for release-facing `n_session`, trust-tier promotion, or Wilson 95% confidence intervals.
2. When `scenario_id` is missing, the artifact may contribute to diagnostics only; `n_session` is unavailable until the capture is remediated.
3. Every release-facing rate, interval, or threshold report must disclose whether clustering is capture-level or session-level.

### CONTRACT: Exact Invariant Collapse Diagnostics

1. When a list-valued invariant is emptied because samples disagree, the builder must emit a reasoned diagnostic such as `mixed_values`, `missing_field`, or `synthetic_fail_closed_route`.
2. Release-critical fields including `supported_versions`, cipher-suite order, extension order, supported groups, and ALPN may not collapse to an unlabeled empty vector.
3. Family regrouping is blocked until every empty release-critical invariant has an explicit, tested reason.

### CONTRACT: Source-Kind Vocabulary

1. The source-kind vocabulary is canonical across extractor, summaries, baselines, registries, renderers, and documentation.
2. Any reviewed surface that emits `source_sha256` and `scenario_id` must also emit `source_kind` or explicitly document why it is unavailable.
3. Advisory and authoritative source kinds may not be conflated by aliasing, defaulting, or documentation shorthand.
4. The canonical source-kind registry must define at least: authoritative `browser_capture`, `curl_cffi_capture`, and `pcap`; advisory `utls_snapshot` and `advisory_code_sample`; and any future kind with an explicit release-use policy.
5. Scripts may import the canonical registry, but they may not carry divergent hardcoded sets for release classification.

### CONTRACT: Statistical Helper Boundaries

1. JA4-like or other lossy summaries remain diagnostics only.
2. They must never be the sole gate for a field whose exact reviewed value exists.

### CONTRACT: Runtime Monte Carlo Separation

1. Generated seeds are runtime stress inputs, not independent browser evidence.
2. Seed counts may exercise RNG-sensitive layout and padding drift, but they may not satisfy minimum capture, source, or session thresholds, and they may not become denominators for release-facing confidence intervals.
3. Nightly Monte Carlo remains a runtime determinism and drift gate; browser-fidelity claims must still come from reviewed fixtures plus the attached transport and active-probing evidence.

### CONTRACT: Route And Transport Fail-Closed Behavior

1. RU and unknown routes must not emit ECH.
2. QUIC remains blocked for release policy.
3. Missing SYN-phase transport evidence remains `unavailable`; it may not be replaced with guessed values or zero-filled pass/fail shortcuts.

### CONTRACT: ECH And QUIC Browser-Fidelity Versus Route-Policy Layers

1. Browser-fidelity extraction records what the reviewed real browser emitted on the captured route. Route policy records what this system is allowed to emit on the operational route. These are separate fields and separate gates.
2. A non-RU reviewed fixture with ECH present is evidence that the browser-fidelity layer must understand ECH; it does not authorize RU or unknown route emission.
3. A RU or unknown route policy override that suppresses ECH or blocks QUIC must preserve the rest of the mimicked browser wire image unless the policy explicitly requires a fail-closed abort.
4. QUIC-looking probes, fallback packets, timing patterns, or retry payloads on blocked RU-to-non-RU lanes are route-policy leaks even when a browser would attempt QUIC elsewhere.
5. Tests must include both positive non-RU browser-fidelity controls and RU or unknown policy mutants. Passing only one layer is insufficient for release evidence.

### CONTRACT: Plaintext Name Leakage And Fallback Privacy

1. ECH availability, route-policy suppression, and plaintext name leakage are separate gates. Suppressing ECH on RU or unknown routes is not proof that fallback behavior is safe.
2. Runtime code must not emit a sensitive target hostname, unique probe hostname, or route-specific diagnostic name in plaintext SNI, ALPN-adjacent metadata, QUIC probes, logs, or generated release artifacts unless an explicit reviewed cover-name policy allows that exact wire image.
3. If a policy requires fail-closed abort instead of fallback, the abort path must leave no distinctive packet, retry cadence, or diagnostic payload that identifies the blocked route.
4. Tests must cover success, error, retry, timeout, selective-drop, and partial-blocking paths with ECH enabled outside RU, ECH suppressed in RU or unknown lanes, and QUIC blocked on RU-to-non-RU lanes.

### CONTRACT: GREASE Slot And Raw Wire Validity

1. For fields that use GREASE in reviewed fixtures, exact validation has two layers: raw wire validity, including GREASE value class, slot count, and extension-body length, and normalized non-GREASE equality.
2. A normalized non-GREASE match is insufficient if the candidate removes a reviewed GREASE slot, uses a non-GREASE value in a GREASE position, emits an impossible GREASE value, or changes the raw vector length without a reviewed cohort split.
3. Tests must not claim GREASE randomness distribution beyond reviewed captures. They may enforce valid GREASE classes, real fixture slot positions, deterministic serialization under a fixed seed, and no false acceptance when GREASE churn hides a damaged non-GREASE field.
4. GREASE-slot tests must be paired with positive real-fixture controls so valid observed GREASE values such as `0x3A3A`, `0x2A2A`, and `0xFAFA` are accepted only in the scoped fixture context.

### CONTRACT: Transport Status Semantics

1. Metric-level availability is the authoritative place for `available` versus `unavailable`.
2. Top-level transport status remains `pending` while individual missing metrics remain `unavailable`.
3. Operator or CI documentation may not describe missing SYN metrics as `0.0`, synthetic pass, or synthetic fail.
4. Transport thresholds apply only to scorable metrics after availability checks pass.

### CONTRACT: Statistical Claim Serialization

1. Any release-facing confidence statement must include `x_pass`, `n_cluster`, `cluster_level`, and `ci_method`.
2. Bootstrap-based statements must additionally include `bootstrap_resamples` and the reported statistic definition.
3. If any required field is absent, the statement is invalid and must not be used as release evidence.
4. Binary predicates must also disclose `n_sample_raw`, `n_capture`, `n_source`, `n_session`, and `n_seed` when the lane has any seed-generated runtime coverage.
5. If a value is unavailable, the artifact must serialize `availability = unavailable`, a non-empty reason, and a JSON `null` metric value. It must not serialize `0`, `0.0`, an empty vector, or omitted fields as a substitute for unavailable evidence.

### CONTRACT: Small-Sample Statistical Wording

1. Exact anchors with `n_cluster < 3` may be reported as reviewed fixture anchors, but generated Markdown and handoff summaries must not phrase them as "100% fidelity", "statistically proven", or "high confidence".
2. Any displayed percentage must include `x_pass`, `n_cluster`, `cluster_level`, `ci_method`, and `ci_lower_95` in the same JSON object or adjacent generated Markdown row.
3. If `n_cluster < 3`, the default generated prose is count-first, for example `1/1 reviewed capture matched; distributional confidence unavailable for release equivalence`.
4. If Wilson lower bound is displayed, it must be computed from the same collapsed cluster outcomes used for tier eligibility. Raw frame percentages and seed percentages may appear only as diagnostics labelled `n_sample_raw` or `n_seed`.
5. A small-n exact mismatch still fails immediately. Conservative wording is not a downgrade mechanism and cannot rescue a bad exact field.

### CONTRACT: Statistical Implementation Ownership

1. Wilson lower-bound and bootstrap computations must be implemented in one tested helper or one clearly owned renderer module, not copied across artifacts.
2. The helper must reject `n_cluster < 0`, `x_pass < 0`, `x_pass > n_cluster`, unknown `cluster_level`, and confidence requests with no independent denominator.
3. Bootstrap resampling must draw clusters, not raw samples, and must use deterministic seed derivation from `(family_id, cohort_id, route_lane, evidence_lane, parser_version)`.
4. Confidence metadata must be validated both where it is produced and where release artifacts consume it.

### CONTRACT: Pre-Registered Release Predicates And Threshold Freeze

1. Each release-facing predicate must be defined before outcome evaluation in a machine-readable manifest or generated registry containing `predicate_id`, `family_id`, `cohort_id`, `route_lane`, `evidence_lane`, exact fields, positive controls, hostile mutants, denominator policy, confidence method, threshold, and owner surface.
2. A slice may add, remove, or change a predicate only through a red-to-green contract change that records why the previous predicate was incomplete. Outcome-driven threshold edits are invalid release evidence.
3. Repeated nightly or Monte Carlo runs may not be combined by optional stopping. If a release rule uses a rolling window, the window length, inclusion rule, and failure retention policy must be declared before the first run.
4. Generated reports must disclose every attempted release predicate in scope, including failed, unavailable, and diagnostic-only predicates. A passing subset may not be presented as the whole release result.
5. Threshold, cohort, or confidence-method changes require a separate verifier or human review because implementing agents may not self-approve statistical policy changes.

### CONTRACT: Artifact Lineage Ordering

1. Downstream generated release evidence must include enough lineage metadata to identify upstream producer artifacts and timestamps.
2. Downstream artifacts may not claim freshness older than any required upstream producer used in the same rendering pass.
3. Lineage violation is release-blocking until artifacts are regenerated in DAG order.

### CONTRACT: Content-Addressed Artifact Lineage

1. Timestamp checks are necessary but insufficient. Every release-facing generated artifact must also carry or be accompanied by a deterministic lineage record containing sorted upstream input paths, upstream SHA-256 digests, generator script path, generator script SHA-256, parser version, command line, and `now_utc` when applicable.
2. A downstream artifact whose timestamp is newer than its producer but whose recorded upstream digest differs from the current producer output is stale and release-blocked.
3. Lineage records must use repository-relative paths and must not include local workspace roots, environment variables, temporary directory names, or other machine-local details.
4. CI lineage tests must include a hostile stale-artifact case where mtime is fresh but upstream content hash is old; that mutant must fail before release rendering.
5. If a generator intentionally changes its lineage schema, the change must be covered by a contract test and an Agent Gate Artifact that lists the old and new schema fields.

## 4. Statistical And Release Policy

### 4.1 Evidence Classes

1. Reviewed exact evidence is authoritative for browser-fidelity claims.
2. Reviewed set-membership and distributional evidence may support exact evidence, but they may not overrule it.
3. Imported evidence is diagnostic only and never release-gating.
4. Transport coherence and active probing are separate release artifacts and may block release independently of TLS exactness.
5. Generated seeds are runtime stress inputs, not independent browser evidence.

### 4.2 Canonical Identities And Deduplication

1. Canonical capture identity is `(source_kind, source_sha256)`.
2. `source_path` is provenance and operator context only; changing `source_path` alone may not create a new independent source.
3. Canonical session identity is `scenario_id`; if it is missing, fall back to the capture identity only for diagnostics and treat Tier1-and-higher independence as unavailable until fixed.
4. If two fixtures share the same canonical capture identity, they count as one authoritative capture even if filenames, directories, or copied paths differ.
5. If the same canonical capture identity appears in both reviewed and imported lanes, release evidence is blocked until the collision is explained and one authoritative lane remains.
6. `n_source` is intentionally conservative until richer source metadata exists. Today it may equal deduplicated capture identity; future contributor, device, ASN, or lab metadata may only reduce independence, never increase it automatically.
7. `n_session` counts only non-empty reviewed `scenario_id` values. If the field is absent, the lane may still expose `n_capture` for diagnostics, but `n_session` and any session-clustered confidence report are unavailable.
8. `n_sample_raw` is never a release denominator. It is only a debugging field for corpus size, fixture coverage, and stress-test breadth.

### 4.3 Units Of Analysis

1. The unit of release reporting is `(family_id, cohort_id, route_lane, evidence_lane)`.
2. Exact release gates evaluate each reviewed sample independently and fail on the first mismatch.
3. Tier promotion counts use deduplicated capture identities and independent session identities, not raw sample count.
4. Multiple samples from one capture may widen descriptive envelopes and debug traces, but they do not by themselves increase trust tier or statistical confidence.
5. Do not pool Safari iOS, Firefox iOS, Chromium iOS, or desktop Apple TLS into one statistical population unless exact release-critical fields have already been proven identical and the grouping is covered by a dedicated contract test.
6. Do not pool reviewed and imported evidence in any release-facing threshold, histogram, or summary.
7. Do not pool different browser or OS generations when reviewed fixtures prove exact-field divergence. Current fixtures prove that iOS 17.2 and iOS 18.7 Safari are incompatible with modern iOS 26 Apple TLS for the `supported_versions` release claim, so generation-aware family classification is required before reporting iOS confidence.
8. A release-facing denominator must be the count of clusters that belong to the exact cohort under test. Samples outside that cohort are not failures and not passes; they are a different stratum.

### 4.4 Confidence And Denominator Rules

1. Binary match-rate summaries must use independent session-level or capture-level outcomes, never raw frame count, as the denominator for release-facing confidence statements.
2. Cluster-collapse repeated samples to the session or capture level before computing Wilson 95% confidence intervals.
3. When a session contains multiple scorable samples for the same predicate, the collapsed session outcome is pass only if every required sample passes; one reviewed exact mismatch is a failed session.
4. Every release-facing confidence report must disclose whether its `n` is capture-level or session-level, plus the raw sample count used only for diagnostics.
5. Monte Carlo seed counts must be reported separately as `n_seed` and may never appear as `n_capture` or `n_session`.
6. Missing metrics stay `unavailable`. They do not become `0.0`, and they do not become "close enough".
7. The default binary confidence method is the standard Wilson score interval with `z = 1.96` on cluster-collapsed Bernoulli outcomes. If a release rule compares against a confidence floor, it must compare the Wilson lower bound rather than the raw pass rate.
8. If `scenario_id` is missing, `n_session` is unavailable for release-facing reporting; do not substitute `artifact_path.stem`, `source_path`, or raw frame count.
9. Wilson interval implementation must be explicit and deterministic:

$$
\hat{p} = \frac{x}{n},\quad
\mathrm{LB}_{95} = \frac{\hat{p} + \frac{z^2}{2n} - z\sqrt{\frac{\hat{p}(1-\hat{p})}{n} + \frac{z^2}{4n^2}}}{1 + \frac{z^2}{n}},\quad z=1.96
$$

10. If `n_cluster = 0`, confidence output is `unavailable` (not zero, not pass, not fail), and the lane remains blocked or pending.
11. Cluster-level outcomes must be computed before confidence estimation; CI computation on raw frame-level IID assumptions is invalid for release evidence.
12. Reported confidence values must be derived from the same clustered denominator used for tier eligibility decisions in that lane.
13. A release rule that introduces a confidence floor must compare the Wilson lower bound to the floor. It may display raw pass rate as descriptive context only.
14. Exact-field failures bypass confidence estimation. A lane with one exact reviewed mismatch is failed even if its lower bound would otherwise exceed the configured floor.
15. Confidence intervals are not tier-promotion mechanisms. Tier eligibility still requires the explicit minimum authoritative captures, independent sources, and independent sessions in the tier table.

### 4.5 Tier Semantics

| Tier | Minimum authoritative captures | Minimum independent sources | Minimum independent sessions | Allowed release use |
|---|---:|---:|---:|---|
| Tier0 | 0 | 0 | 0 | Advisory only. Never release-gating. |
| Tier1 | 1 | 1 | 1 | Exact structural anchoring only. No distributional equivalence claims. |
| Tier2 | 3 | 2 | 2 | Exact invariants and set-membership gates allowed. Descriptive rates allowed, but no distributional equivalence claims. |
| Tier3 | 15 | 3 | 2 | Exact, set-membership, and contract-tested distributional gates allowed when confidence reporting and independence checks are present. |
| Tier4 | 200 | 3 | 2 | High-confidence longitudinal drift monitoring and release evidence, still subordinate to exact reviewed-field precedence. |

### 4.6 Statistical Rules

1. Exact mismatches fail immediately, regardless of favorable distributional summaries or nightly seed pass rates.
2. No p-value-only release gates are allowed. If a future lane introduces formal hypothesis tests, the null model, correction policy, effect size, minimum sample size, and clustering rule must be documented and pinned by contract tests before the result can gate release.
3. The current transport thresholds remain `0.85` for Tier2 and `0.95` for Tier3, but only for scorable metrics. If a required metric is unavailable, the transport lane remains blocked or pending rather than being scored on partial evidence.
4. Distributional gates must be stratified by family, cohort, and route lane. Cross-family, cross-cohort, or cross-route histogram pooling is forbidden.
5. No histogram-overlap percentage, JA4-like surrogate, or seed-pass percentage may rescue a reviewed exact mismatch.
6. If richer source metadata is added later, it may only reduce claimed independence compared with the canonical capture and session identities above; it may never increase independence automatically.
7. Tier3 and Tier4 distributional language is allowed only when the exact reviewed field for the same property already passes, the collapsed denominator satisfies `n_capture >= 15` or `n_session >= 15`, and the lane discloses deduplicated `n_capture`, `n_session`, and raw sample count separately.
8. For non-binary distributional comparisons, the default method is cluster-resampled bootstrap over capture or session clusters, never raw-frame IID resampling. Use at least 10,000 resamples, stratify by `(family_id, cohort_id, route_lane, evidence_lane)`, and disclose the statistic and interval definition.
9. Any artifact that reports bootstrap or confidence intervals must disclose `x_pass`, `n_cluster`, `cluster_level`, `ci_method`, and `bootstrap_resamples` where applicable.
10. `stale_over_90_days` and `stale_over_180_days` are tier modifiers only; freshness warnings may not rescue an exact mismatch or justify family or route pooling.
11. Bootstrap and Monte Carlo runs must be reproducible in CI: seed derivation must be deterministic per `(family_id, cohort_id, route_lane, evidence_lane, parser_version)`.
12. If future formal hypothesis tests are added across multiple lanes, multiplicity correction policy (for example Holm-Bonferroni) must be declared and contract-tested before release use.

### 4.7 Release Evidence Serialization Schema

Every release-facing lane that reports a binary predicate must serialize at least:

1. `predicate_id`
2. `family_id`
3. `cohort_id`
4. `route_lane`
5. `evidence_lane`
6. `availability`
7. `x_pass`
8. `n_cluster`
9. `cluster_level`
10. `ci_method`
11. `ci_lower_95`
12. `n_sample_raw`
13. `n_capture`
14. `n_source`
15. `n_session`
16. `n_seed`
17. `exact_gate_status`
18. `source_artifacts`
19. `generated_at_utc`

Serialization rules:

1. `availability = unavailable` requires `ci_lower_95 = null`, `x_pass = null`, `n_cluster = null`, and a non-empty reason.
2. `exact_gate_status = fail` makes the release predicate fail before any confidence calculation is considered.
3. `n_seed` may be positive only for runtime stress or Monte Carlo coverage; it may not be copied into `n_capture`, `n_source`, `n_session`, or `n_cluster`.
4. `source_artifacts` must name the exact upstream artifacts used by the calculation and must support lineage-order validation.
5. `cohort_id` must be stable across deterministic regeneration from the same reviewed inputs; if cohort assignment changes, the artifact must include a non-empty reason such as `new_exact_field`, `generation_split`, or `mixed_cohort_blocked`.

### 4.8 Fingerprint Mutation Adequacy And Statistical Sensitivity

1. Exact-field and policy gates must be sensitivity-tested with controlled mutants before they are trusted for release. A gate that passes all real fixtures but fails to reject a mutant is a false-negative gate.
2. Mutation adequacy is not a statistical confidence interval and not a browser-evidence count. It is a test-quality prerequisite that proves the suite can detect known masking-damaging changes.
3. The required exact-gate adequacy target is zero false-negative controlled mutants for release-critical fields. If any controlled mutant survives, the lane remains blocked even if Wilson or bootstrap summaries look favorable.
4. The required positive-control target is zero unexplained false-positive failures on reviewed real fixtures. If a real fixture fails, the agent must determine whether the implementation is wrong, the cohort boundary is wrong, or the fixture provenance is wrong before relaxing a test.
5. Mutation results must be stratified by `(family_id, cohort_id, route_lane, evidence_lane, mutation_class)` so a mutant that only exercises one browser family or cohort cannot be used to claim coverage for another family or cohort.
6. Do not compute p-values on controlled mutants. The expected result is deterministic fail or pass by contract; probabilistic framing would hide the purpose of the mutant.
7. The mutation catalog must be versioned and deterministic. Adding a new release-critical exact field requires adding at least one positive reviewed fixture anchor and one hostile mutant for that field in the same slice.
8. Raw-to-fixture differential reproduction must run before mutation adequacy for fixture-schema changes; otherwise agents may accidentally mutate stale or unreproducible fixture truth.

### 4.9 Statistical Correctness Checklist

Every release-facing statistical artifact, generated report, or operator-facing summary must pass this checklist before it can be treated as evidence.

1. State the claim as a predicate over `(family_id, cohort_id, route_lane, evidence_lane)` before computing any rate or interval.
2. Define the cohort from exact reviewed fields and generation metadata first; do not discover a cohort by optimizing for a passing confidence interval.
3. Emit the five denominators separately: `n_sample_raw`, `n_capture`, `n_source`, `n_session`, and `n_seed`.
4. Select exactly one release denominator for the predicate: capture-clustered or session-clustered. The chosen denominator must match `cluster_level` and `n_cluster`.
5. Collapse within each cluster before scoring: a cluster passes a binary exact predicate only if every required reviewed sample in the cluster passes.
6. Treat one exact reviewed mismatch as a predicate failure before computing confidence or bootstrap intervals.
7. Use Wilson lower bound only for binary clustered outcomes. Do not compute Wilson intervals on raw frame counts, seed counts, extension counts, or unclustered samples.
8. Use cluster-resampled bootstrap only for non-binary distributional metrics, only after exact gates for the same property pass, and only when the report names the statistic and `bootstrap_resamples >= 10000`.
9. If `n_cluster = 0`, or if the selected identity field is unavailable, serialize `availability = unavailable`, `metric = null`, and a non-empty reason. Do not serialize `0`, `0.0`, `[]`, `pass`, or omitted fields as placeholders.
10. Never pool modern iOS 26 Apple TLS with iOS 17.2 or iOS 18.7 for a claim about modern `supported_versions`; the current reviewed fixtures prove exact-field divergence.
11. Never pool reviewed and imported evidence, authoritative and advisory `source_kind`, RU and non-RU route lanes, or runtime seeds and browser captures in one release denominator.
12. Any percentage shown in generated Markdown or JSON must disclose the numerator, denominator, cluster level, method, and exact source artifacts.
13. Multiple release predicates may be displayed together, but no aggregate pass rate may hide a failed predicate. Future hypothesis-test gates must declare effect size, multiplicity correction, minimum clustered denominator, and fail-closed handling before use.
14. Confidence intervals are descriptive release evidence, not a substitute for mutation adequacy. A gate with a surviving controlled mutant is blocked regardless of its interval.
15. Statistical helpers must be deterministic under fixed `now_utc`, sorted inputs, and documented seed derivation. Non-deterministic ordering in JSON, headers, or Markdown is a release-artifact defect.
16. For `n_cluster < 3`, generated operator prose must be count-first and confidence-conservative; it may not use percentage-only or high-confidence wording even when `x_pass = n_cluster`.
17. A raw pass percentage may never be displayed more prominently than the Wilson lower bound for binary release predicates. If both are shown, the lower bound is the release comparator.
18. Predicate and threshold choices must be made before looking at the current run's outcomes. A report that changes threshold, cohort split, fixture inclusion, or confidence method after seeing failures is `invalid_outcome_tuned`, not `pass`.
19. Repeated nightly or Monte Carlo attempts must retain failures according to a predeclared window. Agents may not rerun until pass and publish only the final successful run.
20. Every release report must list the full predicate universe for the slice, including pass, fail, unavailable, diagnostic-only, and blocked predicates, so multiple comparisons cannot be hidden by selective reporting.

### 4.10 Required Mutation Catalog Schema

The mutation catalog may be a checked-in JSON file or a generated equivalent, but every entry must be deterministic and auditable. Each entry must include at least:

1. `mutation_id`: stable snake-case identifier; renaming requires a compatibility note in the Agent Gate Artifact.
2. `risk_ids`: non-empty list from Section 5.
3. `family_id` and `cohort_id`: the scoped release family and cohort under attack.
4. `route_lane` and `evidence_lane`: the exact lane expected to fail.
5. `fixture_anchor`: reviewed fixture path plus `fixture_id`, `frame_number`, and `tcp_stream` where applicable.
6. `raw_dump_anchor`: raw dump path under `docs/Samples/Traffic dumps/` plus `source_sha256`.
7. `mutated_field`: one release-critical field such as `non_grease_supported_versions`, `extension_types`, `alpn_protocols`, `source_sha256`, `scenario_id`, `route_policy`, or `n_seed`.
8. `mutation_class`: one of `exact_field_downgrade`, `grease_evasion`, `grease_slot_drift`, `length_mismatch`, `order_drift`, `capture_selection_drift`, `plaintext_name_leak`, `identity_alias`, `missing_identity`, `advisory_contamination`, `route_policy_leak`, `seed_inflation`, `lineage_stale`, `threshold_overfit`, or a newly documented class.
9. `expected_failure_lane`: the contract, generator, matcher, nightly, renderer, or route-policy gate that must fail.
10. `positive_control`: the reviewed fixture or runtime control that must continue to pass.
11. `expected_diagnostic`: a stable reason code or error substring that is precise enough to distinguish the intended failure from harness breakage.
12. `created_by_phase` and `last_verified_utc`: provenance for the mutation entry.

Catalog rules:

1. Mutations must be minimal. Change one masking-relevant property unless the mutation class explicitly tests cross-field consistency.
2. Mutants must never be counted as reviewed evidence, source diversity, session diversity, bootstrap clusters, or Monte Carlo seeds.
3. A mutation entry is not complete until the positive control passes and the mutant fails the expected lane.
4. If a mutant starts passing after an implementation change, the slice is red again until the product bug or test architecture gap is fixed.
5. If a positive control fails after a mutant is introduced, the agent must prove whether the fixture truth, cohort boundary, or implementation is wrong before changing the expectation.
6. The catalog must be regenerated or validated in sorted order so unrelated agent runs cannot reorder entries and create noisy diffs.

### 4.11 Release Predicate Registry And Optional-Stopping Guard

The release predicate registry may be a checked-in JSON file, a generated artifact, or a deterministic section inside a generated release evidence artifact. It exists to prevent outcome-tuned statistics.

Each predicate entry must include at least:

1. `predicate_id`, `family_id`, `cohort_id`, `route_lane`, and `evidence_lane`.
2. The exact fields under test, including raw-wire fields when GREASE or record layout is release-critical.
3. Positive reviewed fixture anchors and hostile `mutation_id` entries.
4. Cluster level, denominator source, confidence method, confidence threshold if any, and small-n wording rule.
5. Inclusion and exclusion rules for raw samples, captures, sessions, imported fixtures, seeds, stale artifacts, route lanes, and advisory evidence.
6. The first red command and expected failure reason for new or changed predicates.
7. The UTC timestamp and agent or verifier that approved the predicate before outcome evaluation.

Registry rules:

1. Missing registry entry means `availability = unavailable` or `status = blocked`; it may not become a default pass.
2. A threshold change after a failed run creates a new predicate version and invalidates prior confidence language until the new red-to-green proof is attached.
3. A predicate can be retired only with a verifier-approved reason and a replacement predicate or explicit out-of-scope blocker.
4. CI must include a stale or outcome-tuned registry mutant where a threshold is edited after results are known; that mutant must fail before release artifacts are rendered.

## 5. Risk Register

| Risk ID | Category | Attack or failure mode | Impact |
|---|---|---|---|
| RISK-FP-01 | Schema omission | `supported_versions` exists in reviewed fixtures but is absent from generated reviewed artifacts | Exact Apple/iOS drift escapes to release |
| RISK-FP-02 | Lossy surrogate overreach | JA4-style reduction hides exact vector drift | False statistical confidence |
| RISK-FP-03 | Family coarsening | Firefox iOS and WebKit iOS are merged despite divergent exact fields | Empty or weakened exact invariants |
| RISK-FP-04 | Matcher permissiveness | Family matcher accepts wrong exact vectors | Wrong wire image passes reviewed-lane checks |
| RISK-FP-05 | Nightly undercoverage | Nightly lane passes on counts and envelopes only | Regression detected too late |
| RISK-FP-06 | Transport score abuse | Unavailable SYN-phase evidence is coerced into numeric metrics | False release evidence |
| RISK-FP-07 | Fallback fingerprint leak | RU and unknown route fallback emits a unique probe or transition signature | DPI fingerprinting of fallback path |
| RISK-FP-08 | Provenance contamination | Imported or advisory evidence leaks into reviewed release claims | Invalid release sign-off |
| RISK-FP-09 | Parser hardening gap | Malformed `0x002B` or transport metadata is parsed leniently | Silent data corruption or security defect |
| RISK-FP-10 | Independence inflation | `source_path` aliases or copied fixtures create fake independent-source counts | False Tier2 or Tier3 promotion and overstated confidence intervals |
| RISK-FP-11 | Seed inflation | Monte Carlo seed counts are treated as capture evidence | False statistical confidence and invalid release claims |
| RISK-FP-12 | Provenance collision | The same `source_sha256` appears across reviewed/imported or conflicting family or route lanes | Contaminated release evidence and wrong grouping decisions |
| RISK-FP-13 | Session fallback inflation | Missing `scenario_id` falls back to `artifact_path.stem` and is counted as an independent session | False `n_session`, false tier promotion, and invalid Wilson intervals |
| RISK-FP-14 | Silent invariant collapse | Mixed samples collapse exact list invariants to empty vectors without a reason code | Release-critical instability hidden behind sparse baselines |
| RISK-FP-15 | Dump-to-fixture drift | Reviewed JSON is patched or refreshed without traceable raw-capture provenance | Unverifiable release evidence |
| RISK-FP-16 | Source-kind vocabulary drift | Extractor, summaries, baselines, renderers, and docs disagree on `source_kind` meaning | Wrong authoritative or advisory classification |
| RISK-FP-17 | Status semantics drift | Metric-level `unavailable` is flattened into numeric or top-level ready states in docs or CI | False transport readiness signals |
| RISK-FP-18 | Shared-loader identity fallback | Common ClientHello or ServerHello loaders synthesize `scenario_id` from filenames | Invalid release counts propagate through smoke, registries, and renderers |
| RISK-FP-19 | Parser divergence | Different scripts parse or strip `supported_versions` differently | Conflicting exact truth between extractor, headers, baselines, and matchers |
| RISK-FP-20 | Raw-fixture reproduction drift | Extractor output cannot be reproduced byte-for-byte from checked-in raw dumps | Tests assert stale or hand-edited truth |
| RISK-FP-21 | Test false negatives | A fingerprint gate passes real fixtures but cannot catch controlled masking-damaging mutants | Masking regressions escape CI and release review |
| RISK-FP-22 | Cohort denominator ambiguity | Release evidence omits `cohort_id` or pools exact-field-incompatible generations under one denominator | Statistically invalid pass rates, confidence intervals, and tier decisions |
| RISK-FP-23 | Path provenance leakage | Reviewed fixtures or generated artifacts preserve absolute workspace roots or accept path aliases as release identities | Non-portable evidence, local path disclosure, and false source/session independence |
| RISK-FP-24 | Timestamp-only lineage | Downstream release artifacts rely on timestamps without upstream content hashes and generator command provenance | Stale generated evidence can masquerade as current release proof |
| RISK-FP-25 | Small-n confidence overstatement | Generated reports phrase `1/1` or `2/2` exact anchors as high-confidence percentages | Statistically invalid release confidence and operator overtrust |
| RISK-FP-26 | Optional stopping and threshold overfit | Agents tune thresholds, cohort boundaries, predicate sets, or confidence wording after seeing outcomes | Statistically invalid release evidence and false confidence |
| RISK-FP-27 | Capture-selection drift | Extractor picks the wrong direction, retransmission, second ClientHello, or concatenated stream bytes | Reviewed fixture truth no longer matches real browser traffic |
| RISK-FP-28 | GREASE raw-wire drift | Candidate preserves the non-GREASE vector but damages GREASE slot count, value class, or raw extension-body length | DPI-visible browser fingerprint drift escapes exact gates |
| RISK-FP-29 | Plaintext-name leakage | ECH-disabled fallback, QUIC-blocked routes, or error paths emit sensitive or unique target names in plaintext | Information leakage and route fingerprinting |

### 5.1 Risk-To-Test Coverage Matrix

Every high or critical risk must be tied to at least one red test before implementation starts. The table below names the minimum expected coverage; agents may add more tests but may not remove a risk without updating this matrix.

| Risk IDs | Minimum test coverage | Required hostile scenario |
|---|---|---|
| RISK-FP-01, RISK-FP-09, RISK-FP-19 | `test/analysis/test_extract_client_hello_fixtures.py`, new supported-versions parser contract/fuzz file | malformed, truncated, odd-length, GREASE-only, duplicate, and oversized `0x002B` bodies fail closed or preserve exact audited duplicates |
| RISK-FP-01, RISK-FP-16 | `test/analysis/test_reviewed_clienthello_fixture_summary_coverage.py`, `test/analysis/test_reviewed_clienthello_fixtures_header.py` | generated reviewed header drops `source_kind` or `supported_versions` and the test fails |
| RISK-FP-03, RISK-FP-14, RISK-FP-22 | `test/analysis/test_build_family_lane_baselines_evidence_tiers.py`, `test/analysis/test_build_family_lane_baselines_stress.py`, new iOS split adversarial test, new cohort release-predicate contract | mixed iOS family collapses a release-critical list to empty without a reason code, legacy iOS 17.2/18.7 samples weaken the modern iOS 26 Apple TLS `[0x0304, 0x0303]` invariant, or release evidence omits `cohort_id` |
| RISK-FP-04 | new `test/stealth/test_family_lane_supported_versions_contract.cpp` | family matcher accepts `{0x0304, 0x0303, 0x0302, 0x0301}` for an Apple/iOS lane that reviewed fixtures prove is `{0x0304, 0x0303}` |
| RISK-FP-05, RISK-FP-11 | new nightly exact-field gate plus existing nightly Monte Carlo suite | seed expansion hides exact vector mismatch or seed count is reported as capture/session evidence |
| RISK-FP-06, RISK-FP-17 | `test/analysis/test_transport_metrics_unavailable_not_zero.py`, `test/analysis/test_transport_and_active_probing_status_contract.py` | missing SYN evidence serializes as `0.0`, omitted, or top-level pass instead of metric-level `unavailable` and appropriate pending/blocked status |
| RISK-FP-07 | route-transition integration/adversarial C++ tests | RU or unknown fallback emits ECH, QUIC, replayable probe traffic, or a unique transition signature |
| RISK-FP-08, RISK-FP-12 | `test/analysis/test_check_fixture_registry_independence.py`, `test/analysis/test_fixture_metadata_collision.py` | same `source_sha256` appears in reviewed/imported lanes or conflicting family/route lanes without blocking release evidence |
| RISK-FP-10, RISK-FP-13, RISK-FP-18 | common-loader and family-baseline identity tests | path alias or missing `scenario_id` increases `n_source`, `n_session`, tier, or Wilson denominator |
| RISK-FP-15 | fixture provenance and raw-capture traceability tests | reviewed fixture wire bytes change while `source_path` hash or raw-dump provenance cannot reproduce the change |
| RISK-FP-16 | source-kind vocabulary contract test | extractor, loader, baseline, renderer, and docs disagree about authoritative versus advisory `source_kind` |
| RISK-FP-20, RISK-FP-21 | raw-to-fixture reproduction and mutation-adequacy suites | checked-in fixtures are not byte-identical to regenerated release-critical fields, or a controlled mutant survives the gate it is meant to break |
| RISK-FP-23 | new raw-dump path portability and alias-adversarial contract | absolute `/home/<user>/...` path leaks into generated release artifacts, Unicode-normalized Cyrillic path alias changes identity, or `..`/symlink escape leaves `docs/Samples/Traffic dumps/` |
| RISK-FP-24 | artifact lineage content-hash contract and CI stale-artifact mutant | downstream release evidence has a fresh timestamp but an old upstream SHA-256, generator SHA-256, or command line |
| RISK-FP-25 | statistical wording and small-n serialization tests | generated Markdown or JSON reports `100%` or high confidence for `n_cluster < 3` without Wilson lower bound and denominator fields |
| RISK-FP-26 | new release-predicate pre-registration and threshold-freeze contract | confidence floor, cohort boundary, predicate set, or small-n wording changes after outcome evaluation and the artifact is rejected as outcome-tuned |
| RISK-FP-27 | new capture-selection and reassembly adversarial contract | wrong direction, retransmission, partial record, second ClientHello, or unrelated stream concatenation changes extracted release-critical fields without a blocking diagnostic |
| RISK-FP-28 | new GREASE slot validity C++ and Python parser tests | normalized `[0x0304, 0x0303]` still passes after a GREASE slot is removed, made invalid, moved, or length-mutated |
| RISK-FP-29 | new plaintext-name leakage and ECH fallback adversarial route tests | RU, unknown, error, timeout, or partial-blocking fallback emits plaintext target-name, unique probe SNI, QUIC probe, or diagnostic payload |

### 5.2 ASVS L2 Mapping For This Plan

1. Input validation: hostile `0x002B`, transport, and fixture metadata inputs must be rejected at parser or loader entry points.
2. Error handling: diagnostics must expose the failing artifact, field, and reason without leaking secrets, local environment details beyond already checked-in provenance paths, or stack traces into release artifacts.
3. Data integrity: generated artifacts must be reproducible from raw captures and upstream JSON; direct edits to generated outputs are integrity failures.
4. Resource exhaustion: parser fuzz and stress tests must cover large extension bodies, many fixtures, many duplicate hashes, and pathological route or source-kind combinations without unbounded allocation or superlinear behavior.
5. Access and policy control: release-mode evidence must deny by default when source class, route lane, transport availability, or identity fields are missing or advisory.
6. Path traversal prevention: any script that accepts a capture path, fixture path, or output path must resolve it under an explicit allowed repository root and reject `..`, symlink escape, absolute-path aliasing, and Unicode-normalized path collisions before opening files.
7. Secure deserialization: JSON fixtures, registries, and generated status files are untrusted until schema-checked. Loaders must reject wrong top-level type, missing required fields, duplicate IDs, type mismatches, oversized arrays, and inconsistent length fields before deriving release counts.
8. Secret-safe diagnostics: tests and tools may report checked-in fixture paths, hashes, profile IDs, field names, and bounded diffs, but they must not dump full packet payloads, TLS key material, environment variables, local usernames outside repository paths, or network credentials.
9. Fail-closed authorization boundary: imported, advisory, diagnostic-only, stale, unavailable, or identity-incomplete data may be read by tools, but release renderers must require an explicit authoritative reviewed lane before using the data in any release-blocking pass claim.
10. Deterministic cleanup: stress, fuzz, and integration tests must clean temporary fixtures, generated files, subprocess outputs, and route-transition simulation state on both pass and fail paths so later tests cannot inherit contaminated evidence.
11. Local path minimization: release tooling must not serialize local workspace prefixes into generated artifacts or operator-facing diagnostics; repository-relative paths plus SHA-256 provenance are the allowed disclosure boundary.
12. Sensitive name minimization: route, ECH, QUIC, and fallback diagnostics must not expose sensitive hostnames, credentials, proxy secrets, or unique route probes in wire output, logs, generated artifacts, or test failure messages.
13. Capture parser hardening: packet-capture parsing must treat pcap contents as untrusted input, validate frame selection and reassembly boundaries before parsing nested TLS fields, and reject oversized or contradictory stream data without unbounded allocation.
14. Statistical integrity: release tooling must deny by default when predicates, thresholds, denominators, confidence methods, or cohort boundaries were not pre-registered before outcome evaluation.

## 6. Agent Execution Phases

### 6.0 Slice Protocol For AI Agents

1. Claim exactly one slice at a time: extractor, summary generator, family baseline generator, matcher, runtime profile, transport status, active probing, or CI and documentation contract. Do not mix unrelated slices in one pass.
2. Before editing, run SocratiCode `codebase_status`, `codebase_symbol`, and `codebase_impact`; if the control path is still unclear, add `codebase_flow` for one nearby hop only.
3. Record one reviewed fixture anchor or raw-capture anchor, one controlling code path, one generated-artifact dependency, and one narrow validation command.
4. Write or update dedicated contract, adversarial, integration, fuzz, and stress tests in separate files before touching implementation.
5. Run the narrow red command and capture the failing reason. If the failure is unrelated to the intended slice, fix the harness first and rerun red.
6. Edit the smallest owning implementation surface.
7. Run the same narrow validation immediately after the first substantive change.
8. If a generator or generated header changes, refresh in dependency order: generator script, generated artifacts, `cmake -S . -B build ...`, `cmake --build build --target run_all_tests ...`, and then `cmake -S . -B build ...` again before any new `ctest` invocation so discovery is refreshed from the built test binary.
9. Re-run reviewed smoke, policy artifact generation, and only then widen to adjacent slices.
10. If the slice touches reviewed fixtures, record the upstream raw dump or the documented reviewed capture source before editing downstream generated artifacts.
11. If the slice touches `common_tls.py`, treat it as a shared anti-corruption-boundary slice and include smoke, registry, loader, fuzz, and stress tests in the red-to-green plan.
12. Publish a handoff note listing fixture anchors, modified owners, generated outputs, red-to-green proof, SocratiCode queries, and `NOTICED BUT NOT TOUCHING` items.

### 6.0A Multi-Agent Coordination And Write Safety

1. Allow parallel read-only discovery, but serialize writes per owning surface (`extractor`, `summary`, `baseline`, `matcher`, `runtime`, `transport`, `active probing`, `CI/docs`).
2. Two agents may not modify the same producer or the same generated artifact chain in parallel.
3. Before each write phase, the owner agent must restate the slice boundary, target files, and the red command it will use.
4. If concurrent edits collide, freeze integration, regenerate from the last green producer state, and rerun red-to-green for the merged slice.

### 6.0B Mandatory Phase-Gate Ledger

Each phase must publish a compact gate record with:

1. `phase_id`
2. `entry_checks`
3. `artifacts_read`
4. `artifacts_written`
5. `red_command`
6. `green_command`
7. `risks_covered`
8. `status` (`pass`, `fail`, or `blocked`)

A phase without a gate record is incomplete and may not hand off to the next phase.

### 6.0C Agent Gate Artifact

Each phase owner must write one JSON or YAML Agent Gate Artifact under `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/` before handing off. The artifact name must include the phase, slice, agent identifier, and UTC timestamp. It must include:

1. The SocratiCode status, flow, symbol, and impact queries used.
2. The exact fixture and raw-dump anchors, including any `0x002B` body used as evidence.
3. The red command, red failure excerpt, green command, and green output summary.
4. The tests added or modified, grouped by contract, adversarial, integration, fuzz, and stress category.
5. The implementation files and generated artifacts changed, with Artifact Dependency DAG refresh order.
6. The statistical denominator fields before and after the slice: `cohort_id`, `n_sample_raw`, `n_capture`, `n_source`, `n_session`, `n_seed`, `cluster_level`, and `ci_method` where applicable.
7. The Risk IDs covered and any high or critical risks still uncovered.
8. Any test relaxation request, including the file-local explanation. Absence of this field means no relaxation occurred.
9. `NOTICED BUT NOT TOUCHING` items.

The verifier must reject a slice with no Agent Gate Artifact, no red proof, missing fixture anchors, or changed generated artifacts without the corresponding upstream refresh command.

### 6.0D Agent Definition Of Ready And Done

An AI agent may start a write slice only when all Definition of Ready items are true:

1. The agent names the slice owner surface, target files, and generated outputs before editing.
2. The agent names at least one raw dump anchor under `docs/Samples/Traffic dumps/` and one reviewed fixture anchor under `test/analysis/fixtures/` for the exact behavior under test.
3. The agent records current SocratiCode status plus at least one symbol, flow, or impact query for the owner surface.
4. The agent maps the slice to Section 2.1 Artifact Dependency DAG and writes the refresh order.
5. The agent lists the Risk IDs, contracts, cohort IDs, and mutation IDs that the first red tests will cover.
6. The agent has one narrow red command and one later green command that exercise the same behavior.
7. The agent has confirmed that the planned tests live in separate files and that no production source will receive inline tests.
8. The agent has identified whether the slice touches a shared anti-corruption boundary (`common_tls.py`, fixture schemas, generated headers, route policy, or statistical renderers). Shared-boundary slices require wider pre-change tests.
9. If the slice reports release statistics, the agent has a pre-registered predicate entry and threshold freeze before viewing outcome data.
10. If the slice derives fixture truth from pcaps, the agent has the packet tuple `(source_sha256, frame_number, tcp_stream, direction, record source)` for every anchor.

An AI agent may mark a slice done only when all Definition of Done items are true:

1. The first red test failed for the intended reason and is preserved in the Agent Gate Artifact.
2. The same command is green after the minimal implementation change.
3. Positive real-fixture controls pass and controlled mutants fail the intended gates.
4. All changed generated artifacts were refreshed from upstream producers, not hand-edited.
5. The slice's validation row in Section 7 is green or is explicitly blocked with a reproducible blocker.
6. Statistical artifacts, if touched, disclose `n_sample_raw`, `n_capture`, `n_source`, `n_session`, `n_seed`, `cluster_level`, and `ci_method`.
7. No release evidence depends on imported, advisory, seed-only, or path-aliased data.
8. The Agent Gate Artifact is written and names `NOTICED BUT NOT TOUCHING` items instead of silently expanding scope.
9. A separate verifier or human can reproduce the reported red-to-green commands from a clean checkout plus the generated artifact refresh order.
10. Predicate, threshold, cohort, and confidence-method metadata were not changed after seeing outcomes, or the change is recorded as a new red-to-green statistical-policy slice.
11. Capture-selection, GREASE-slot, and plaintext-name leakage mutants relevant to the slice fail in the expected lane while positive real-fixture controls pass.

### 6.0E Agent Workstream Dependency Table

The phases below are intentionally step-by-step. Agents may parallelize read-only discovery, but write slices must follow this dependency order unless a verifier explicitly splits an independent subgraph.

| Workstream | Earliest phase | Depends on | Primary owner surfaces | Required first red proof | Required green evidence |
|---|---|---|---|---|---|
| W0 census and provenance freeze | Phase 0A | None | fixture census scripts, raw dump tree, profiles registries | Missing field, hash mismatch, alias collision, or missing first-class `supported_versions` is reported with exact path | Machine-readable census with counts and no unexplained collisions |
| W1 canonical parser corpus | Phase 0D / Phase 1 | W0 | `common_tls.py` or narrow parser helper, C++ test helper | Hostile `0x002B` bodies expose parser leniency or cross-language disagreement | Shared golden/adversarial/fuzz corpus passes in Python and C++ helper paths |
| W2 extractor and fixture schema | Phase 2 | W0, W1 | `extract_client_hello_fixtures.py`, fixture JSON schema | Real fixture with extension `0x002B` lacks first-class non-GREASE vector, or a wrong-direction/reassembly mutant changes release truth | Regeneration from raw dumps emits exact raw and non-GREASE vectors with byte-identical existing release fields and stable packet selectors |
| W3 shared loader identity | Phase 2 | W0, W1 | `common_tls.py`, ClientHello and ServerHello loaders | Missing `scenario_id` or path alias changes release identity | Loader fails closed or marks diagnostic-only; downstream smoke and registry tests stay green |
| W4 reviewed summary and headers | Phase 2 | W2, W3 | `merge_client_hello_fixture_summary.py`, `ReviewedClientHelloFixtures.h` | Header drops `source_kind`, provenance, or `supported_versions` | Generated header exposes exact fields and is deterministic |
| W5 family baseline and cohort split | Phase 3 | W2, W3, W4 | `build_family_lane_baselines.py`, `ReviewedFamilyLaneBaselines.h`, tier renderer | Legacy iOS and modern iOS pooling empties or weakens a critical invariant, or release evidence lacks `cohort_id` | Generation-aware or exact-field cohorting blocks mixed strata and preserves correct denominators |
| W6 C++ matcher and runtime profiles | Phase 4 | W4, W5 | `FamilyLaneMatchers.cpp`, `BrowserProfile.cpp`, `ClientHelloExecutor.cpp` | Wrong generated wire vector, GREASE-slot mutant, or raw extension length mutant passes matcher or runtime profile | Exact mismatch and GREASE raw-wire drift fail despite padding or seed changes; route fail-closed tests remain green |
| W7 nightly and statistical release metadata | Phase 5 | W5, W6 | nightly C++ gates, policy renderers, statistical helpers, release predicate registry | Seed count or raw-frame count promotes evidence, exact mismatch is hidden by a rate, or threshold changes after outcome evaluation | Clustered Wilson/bootstrap metadata is emitted, exact failures dominate statistics, and predicate registry blocks optional stopping |
| W8 transport, active probing, and route transitions | Phase 6 | W0 plus route-policy fixtures | transport extractors, active-probing status, runtime route tests | Missing SYN data becomes numeric, RU/unknown emits ECH/QUIC, or fallback leaks plaintext target-name/probe traffic | Unavailable metrics stay unavailable; route-transition and plaintext-name mutants fail without false positives on real controls |
| W9 CI, docs, and lineage | Phase 7 | W4 through W8 | workflows, operations guide, generated release artifacts, predicate registry | Downstream release artifact is stale, outcome-tuned, missing predicate registry, or points to old plan/semantics | CI contracts enforce single plan, DAG lineage, threshold freeze, fail-closed language, and reviewed/imported separation |
| W10 independent verification | Phase 8 | All writing workstreams | no code-writing owner; verifier-only | Any missing handoff, missing red proof, or surviving mutant blocks closeout | Full matrix evidence is reproducible, and no implementing agent self-approves final quality |

### 6.0F Agent Prompt Packet For Each Slice

Before an AI agent starts a slice, the orchestrator must provide a compact prompt packet with these exact fields. If any field is unknown, the agent performs read-only discovery and blocks write work until the packet is complete.

1. `slice_id` and owner surface.
2. `objective` as one release-critical predicate, not a vague quality goal.
3. `raw_dump_anchors` under `docs/Samples/Traffic dumps/` with exact spelling.
4. `reviewed_fixture_anchors` under `test/analysis/fixtures/` with `fixture_id`, `frame_number`, `tcp_stream`, `source_sha256`, `scenario_id`, `route_mode`, and `source_kind`.
5. `cohort_id`, `family_id`, `route_lane`, and `evidence_lane`.
6. `risk_ids`, `contracts`, and `mutation_ids` to cover.
7. `socraticode_queries` to rerun: status/watch plus symbol, flow, or impact targets.
8. `artifact_dag_refresh_order` and generated outputs.
9. `first_red_command`, `expected_red_reason`, and `green_command`.
10. `positive_controls` and `hostile_mutants`.
11. `statistical_denominator_policy`: capture-clustered or session-clustered, plus unavailable handling.
12. `write_lock_surface` so parallel agents do not edit the same producer or generated chain.
13. `packet_selection_policy`: required when raw dumps are parsed; includes direction, record source, and reassembly fallback rule.
14. `predicate_registry_entry`: required for release-facing statistics; includes threshold, confidence method, and frozen predicate version.
15. `name_leakage_policy`: required for ECH, QUIC, route, or fallback slices; identifies sensitive names, cover names, and forbidden diagnostics.

The implementing agent may not replace the packet with assumptions discovered from profile names or browser brands. Any correction to the packet must cite raw dump and reviewed fixture evidence.

If a workstream reveals a new release-critical bug class, the owning agent must update Section 1.7, Section 5, Section 5.1, the mutation catalog, and the validation matrix before fixing implementation. New risk documentation is part of the red phase, not cleanup.

### Phase 0A. Raw Dump, Fixture Census And Provenance Audit

Inputs:

1. `docs/Samples/Traffic dumps/`
2. `test/analysis/fixtures/`
3. `test/analysis/profiles_validation.json`
4. `test/analysis/profiles_imported.json`

Tasks:

1. Enumerate raw reviewed captures under `docs/Samples/Traffic dumps/` and reviewed or imported fixture identities with fields `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, `profile_id`, and `parser_version`.
2. Prove that each reviewed fixture under change maps back to a raw capture or a documented reviewed capture source with matching `source_sha256`; manual JSON edits of reviewed wire fields are invalid.
3. Fail if a reviewed fixture lacks any of those fields or carries an empty value.
4. Deduplicate authoritative capture counts on `(source_kind, source_sha256)` and separately record independent session counts on `scenario_id`.
5. Detect collisions where the same capture hash appears in both reviewed and imported lanes, or where one capture hash maps to conflicting family or route assumptions.
6. Record raw sample count, deduplicated capture count, and session count separately.
7. Recompute SHA-256 for every checked-in reviewed `source_path` and fail if it differs from `source_sha256`.
8. Produce a list of samples carrying raw extension `0x002B` but lacking first-class `supported_versions` fields; this list becomes the extractor and loader red-test seed corpus.
9. Preserve spaces, non-ASCII characters, and case in traffic-dump paths when checking traceability; path normalization must not rewrite real capture names.
10. Emit a candidate `cohort_id` table for every reviewed family and route lane, including the exact fields used to split cohorts and any mixed-cohort blockers.
11. Emit a raw-to-fixture join table that proves every fixture anchor used by the slice maps back to a real dump or a documented reviewed source.
12. Count absolute `source_path` values, paths outside the repository root, symlink escapes, Unicode-normalized aliases, case-only aliases, and path strings that cannot be converted to a repository-relative raw dump path without changing the checked-in spelling.
13. Record whether generated release artifacts currently emit local workspace prefixes; such emissions are blockers for any release-facing report.
14. Compute a content lineage seed manifest for the current DAG: sorted upstream artifact paths, SHA-256 digests, generator script digests, parser versions, and commands needed to reproduce each generated artifact.
15. Record packet selectors for every fixture anchor: `frame_number`, `tcp_stream`, direction, record source, reassembly fallback, and whether duplicate ClientHello candidates exist in the capture.
16. Build a preliminary release-predicate registry draft for every release-facing claim the slice may touch, including threshold and confidence method before any outcome evaluation.

Outputs:

1. A fixture census note with raw-capture anchors, reviewed counts, imported counts, deduplicated capture counts, independent session counts, and any collision report.
2. A machine-readable census artifact or handoff attachment containing `cohort_id`, `n_sample_raw`, `n_capture`, `n_source`, `n_session`, missing-field counts, and hash-collision details.
3. A path-portability appendix with `n_absolute_source_path`, `n_source_path_outside_repo`, `n_symlink_escape`, `n_unicode_path_alias_collision`, and representative real path anchors.
4. A preliminary content-lineage manifest for every generated artifact that the slice will touch.
5. A packet-selection appendix listing selected frames, streams, directions, reassembly source, and any duplicate ClientHello candidates.
6. A predicate-registry draft listing frozen predicates, thresholds, denominators, and mutation IDs for later red-to-green work.

Exit criteria:

1. No unexplained raw-capture, reviewed, or imported provenance collisions remain.
2. Deduplicated counts are known before any tier or confidence claim is discussed.
3. Cohort candidates are known before any family-level confidence or tier claim is discussed.
4. Absolute or machine-local paths are either migrated to repository-relative provenance in the planned slice or explicitly blocked as legacy debt that cannot appear in release artifacts.
5. Every touched generated artifact has a content-hash lineage plan before downstream refresh begins.
6. Packet-selection ambiguity is resolved before exact fields are trusted; unresolved duplicate ClientHello, direction, or reassembly ambiguity blocks extractor and fixture-schema slices.
7. Every release-facing statistical predicate in scope has a draft registry entry before any pass, fail, or confidence result is interpreted.

### Phase 0B. Evidence Freeze And SocratiCode Preflight

Inputs:

1. `docs/Samples/Traffic dumps/`
2. `test/analysis/fixtures/`
3. The implementation surfaces listed in Section 2

Tasks:

1. Use SocratiCode `codebase_status`, `codebase_watch`, `codebase_symbol`, and `codebase_impact` to confirm the current owning surfaces before edits; use `codebase_flow` if the control path crosses more than one layer.
2. Record the exact reviewed fixture samples that define the release-critical expectations for the slice under change.
3. Confirm whether a generated file is downstream of the target change.
4. Record the nearby blast-radius surfaces from the family-lane tier renderer, the family baseline evidence-tier tests, the family baseline stress tests, and the workflow contract when they are affected.
5. If `codebase_status` is not green or the watcher is inactive, stop write work and restore the SocratiCode index/watch state before continuing.
6. For `supported_versions`, record the SocratiCode flow through extractor, common loader, summary generator, family-baseline generator, matcher, and nightly gate before writing tests.

Outputs:

1. A local evidence note naming the source fixtures, the owning generator or matcher, the affected generated outputs, and the likely blast-radius tests.

Exit criteria:

1. The agent can name one controlling code path, one reviewed fixture anchor, one generated dependency, and one narrow validation command before editing.

### Phase 0C. Artifact Dependency DAG Lock

Inputs:

1. The implementation surfaces listed in Section 2
2. The Artifact Dependency DAG in Section 2.1

Tasks:

1. Map the slice onto the Artifact Dependency DAG and record every upstream producer plus downstream consumer that the change can affect.
2. Freeze upstream truth before editing downstream generators or renderers.
3. Record the exact refresh order for the slice. If the slice changes reviewed ClientHello schema, refresh reviewed summary, family baselines, fingerprint stat baselines, dependent serverhello corpus, then build and policy artifacts.
4. Reject any plan that edits generated headers or generated docs directly instead of regenerating them from their owning producer.
5. Add or update a content lineage manifest before accepting downstream release evidence. Timestamp-only refresh notes are not enough.
6. Verify that lineage manifests use repository-relative paths and include the generator script SHA-256 and command line.
7. Add or update the release predicate registry before rendering downstream statistics or Markdown. Missing predicate lineage is treated the same as missing artifact lineage.
8. Record whether each downstream node consumes raw-wire GREASE fields, normalized non-GREASE fields, or both; a consumer may not silently switch from raw to normalized evidence.

Outputs:

1. A DAG lock note naming the edited producer, the regenerated downstream nodes, and the required refresh order.
2. A predicate-registry lock note naming every release predicate, threshold, confidence method, and mutation ID affected by the slice.

Exit criteria:

1. Every edited node is upstream of every regenerated node.
2. No downstream artifact is refreshed before its upstream producer is proven green.
3. A stale-content mutant with a fresh timestamp is known for the slice or is listed as a blocker before CI lineage work begins.
4. An outcome-tuned predicate mutant is known for any statistical slice or is listed as a blocker before release-renderer work begins.

### Phase 0D. Mutant Sensitivity And Raw-Fixture Reproduction Gate

Inputs:

1. Raw dump anchors from `docs/Samples/Traffic dumps/`
2. Reviewed fixture anchors under `test/analysis/fixtures/clienthello/`
3. The parser and extractor surfaces for the slice
4. The mutation catalog or the slice-local mutant definitions

Tasks:

1. Run Raw-to-fixture differential reproduction for every fixture anchor the slice uses before writing implementation code.
2. Compare regenerated values against checked-in fixture JSON and require byte-identical for release-critical extracted fields.
3. Build or update the controlled mutant list for the slice. Each mutant must include `mutation_id`, fixture anchor, raw dump anchor, mutated field, expected failing lane, and Risk IDs.
4. Include both semantic mutants and parser/security mutants: exact-field downgrade, length mismatch, GREASE evasion, GREASE-slot drift, capture-selection drift, order drift, missing identity, path aliasing, Unicode normalization, plaintext-name leakage, route-policy leak, threshold overfit, and seed-inflation cases as applicable.
5. Run the mutant suite in red mode and verify that each mutant fails for the intended reason. If a mutant passes, preserve it as the red evidence and fix implementation later; do not relax it.
6. Run the positive reviewed-fixture controls in the same harness to prove that real fixture truth is not being rejected by the mutant machinery.
7. For pcap-derived fields, include at least one mutant that keeps TLS bytes parseable but changes `frame_number`, `tcp_stream`, direction, or reassembly source so the extractor proves it can detect wrong-capture truth.
8. Attach the reproduction diff and mutation result table to the Agent Gate Artifact.

Outputs:

1. A raw-fixture reproduction report with extractor command, parser version, fixture count, mismatch count, and release-critical field diffs.
2. A mutation adequacy report containing `mutation_id`, `false-negative`, `false-positive`, and expected-versus-actual failing lane for each mutant.
3. A packet-selection mutation report for extractor slices and a threshold-freeze mutation report for statistical slices.

Exit criteria:

1. Reproduction is green for all unchanged fixture anchors, or mismatches are explicit blockers with field-level diagnostics.
2. Every release-critical gate in scope has at least one controlled mutant.
3. No controlled mutant survives the intended exact-field or policy gate unless the slice is still intentionally red and the mutant is recorded as the defect evidence.
4. No reviewed real fixture is rejected by the mutant harness without a filed product, cohorting, or fixture-provenance defect.
5. GREASE-slot, capture-selection, plaintext-name, and optional-stopping mutants are either covered in this slice or explicitly mapped to a later dependent workstream in the Agent Gate Artifact.

### Phase 1. Contract, Adversarial, Integration, Fuzz, And Stress Tests First

Tasks:

1. Add dedicated contract tests for exact `supported_versions` extraction, reviewed summary generation, provenance identity and deduplication, raw-capture traceability, source-kind vocabulary, family baseline schema, and family matcher enforcement.
2. Add adversarial tests for malformed `0x002B` lengths, odd payload lengths, truncated payloads, GREASE-only vectors, duplicate version elements, duplicate source hashes, missing `scenario_id`, silent fallback to `artifact_path.stem`, reviewed or imported lane collisions, and illegal reintroduction of TLS 1.1 or TLS 1.0 into modern iOS 26 Apple TLS cohorts.
3. Add integration tests proving that a wrong modern Apple or iOS vector fails in at least three independent lanes: exact corpus, family-lane or multi-dump baseline, and nightly or structural release-gating lane.
4. Add transport contract tests proving `unavailable` metrics stay unavailable when SYN metadata is absent, top-level transport status stays pending while metric availability is unavailable, and seed counts never appear as evidence counts.
5. Add stress or repeated-seed tests ensuring no single seed hides an exact mismatch and that seed-only expansion does not alter tier counts.
6. Add documentation contract tests pinning `unavailable`-not-`0.0` transport semantics in the operator surfaces and generated evidence language.
7. Add statistical-contract tests for Wilson lower-bound serialization, unavailable confidence output, bootstrap metadata, cluster-level denominator validation, and seed-count segregation.
8. Record the red proof for each slice before implementation begins.
9. Map every red test to at least one Risk ID from Section 5.1; a risk without a hostile test blocks the implementation phase.
10. Add generation-aware family tests proving that iOS 17.2 and iOS 18.7 fixtures with `[0x0304, 0x0303, 0x0302, 0x0301]` do not weaken or empty the modern iOS 26 Apple TLS invariant `[0x0304, 0x0303]`.
11. Add negative statistical tests where legacy and modern iOS samples are incorrectly pooled; the pooled lane must be blocked as mixed-cohort, not converted into a low confidence score.
12. Add mutation adequacy tests for every release-critical gate changed by the slice. The mutant must damage exactly one masking-relevant property where practical and must be named by `mutation_id`.
13. Add raw-to-fixture differential reproduction tests for every extractor or fixture-schema change, including paths with spaces, commas, mixed case, Cyrillic text, and Unicode normalization edge cases.
14. Add cohort release-predicate tests proving that generated release artifacts include `cohort_id`, that legacy iOS samples do not enter modern iOS 26 denominators, and that missing `cohort_id` fails closed.
15. Add path-portability tests proving that current absolute fixture `source_path` values can only be accepted after repository-root stripping, that generated release artifacts do not emit local roots, and that real Cyrillic path aliases do not inflate independence.
16. Add content-lineage tests with a hostile stale generated artifact whose timestamp is fresh but whose upstream content hash, generator hash, parser version, or command line is stale.
17. Add small-n statistical wording tests that reject high-confidence or percentage-only release prose for `n_cluster < 3` and require count-first wording plus Wilson metadata.
18. Add capture-selection and reassembly tests proving wrong direction, retransmission, second ClientHello, truncated record fallback, and unrelated stream concatenation cannot produce trusted reviewed fixture truth.
19. Add GREASE-slot validity tests proving that normalized non-GREASE equality does not hide removed GREASE slots, invalid GREASE values, shifted slots, or raw `0x002B` length drift.
20. Add plaintext-name leakage tests for ECH and QUIC route transitions, including RU, unknown, error, timeout, selective-drop, and partial-blocking paths.
21. Add release-predicate pre-registration tests proving threshold, cohort, confidence method, and predicate-set changes after outcome evaluation are blocked as `invalid_outcome_tuned`.

Required test coverage surfaces (extend existing files first; add new files only where no appropriate contract exists):

1. `test/analysis/test_fixture_provenance_contract.py`
2. `test/analysis/test_check_fixture_registry_independence.py`
3. `test/analysis/test_fixture_metadata_collision.py`
4. `test/analysis/test_reviewed_clienthello_fixtures_header.py`
5. `test/analysis/test_reviewed_clienthello_fixture_summary_coverage.py`
6. `test/analysis/test_build_family_lane_baselines_evidence_tiers.py`
7. `test/analysis/test_build_family_lane_baselines_stress.py`
8. `test/analysis/test_transport_and_active_probing_status_contract.py`
9. `test/analysis/test_transport_metrics_unavailable_not_zero.py`
10. `test/analysis/test_fingerprint_policy_generation_contract.py`
11. `test/analysis/test_fingerprint_policy_ci_contract.py`
12. `test/analysis/test_common_tls_supported_versions_contract.py` (new if absent)
13. `test/analysis/test_common_tls_identity_fail_closed_contract.py` (new if absent)
14. `test/analysis/test_statistical_release_metadata_contract.py` (new if absent)
15. `test/analysis/fuzz_supported_versions_parser.py` (new if absent)
16. `test/stealth/test_family_lane_supported_versions_contract.cpp` (new if absent)
17. `test/stealth/test_ios_family_split_adversarial.cpp` (new if absent)
18. `test/stealth/test_tls_nightly_exact_supported_versions_gate.cpp` (new if absent)
19. `test/stealth/test_tls_route_transition_indistinguishability_integration.cpp` (new if absent)
20. `test/stealth/test_transport_coherence_stress.cpp` (new if absent)
21. `test/analysis/test_raw_fixture_reproduction_contract.py` (new if absent)
22. `test/analysis/test_fingerprint_mutation_adequacy_contract.py` (new if absent)
23. `test/stealth/test_tls_fingerprint_mutation_adequacy.cpp` (new if absent)
24. `test/analysis/test_release_cohort_identity_contract.py` (new if absent)
25. `test/analysis/test_fixture_source_path_portability_contract.py` (new if absent)
26. `test/analysis/test_artifact_lineage_content_hash_contract.py` (new if absent)
27. `test/analysis/test_small_n_statistical_wording_contract.py` (new if absent)
28. `test/analysis/test_capture_selection_reassembly_contract.py` (new if absent)
29. `test/analysis/test_release_predicate_registry_contract.py` (new if absent)
30. `test/analysis/test_outcome_tuned_threshold_adversarial.py` (new if absent)
31. `test/stealth/test_tls_grease_slot_validity_contract.cpp` (new if absent)
32. `test/stealth/test_tls_plaintext_name_leak_adversarial.cpp` (new if absent)

Exit criteria:

1. The new tests fail against the current blind spot for the correct reason, not because of harness mistakes.
2. No implementation change begins before this red suite exists.
3. Every changed release-critical gate has both a positive real-fixture control and a hostile controlled mutant.

### Phase 2. Fixture Schema, Provenance, And Reviewed Summary Hardening

Tasks:

1. Promote `non_grease_supported_versions` from raw `0x002B` into extracted fixture JSON.
2. Add the same vector to the shared `ClientHello` loader model so downstream tools do not need to reparse raw extension bodies.
3. Validate that reviewed fixtures carry the required provenance fields and reject missing or empty reviewed provenance at the first boundary.
4. Keep malformed or truncated payload parsing fail-closed through the canonical parser helper.
5. Export the same `supported_versions` field into reviewed summary generation and generated C++ headers.
6. Prevent generated headers from silently dropping the new field during refresh.
7. Eliminate any Tier1-or-higher fallback from missing `scenario_id` to `artifact_path.stem`; missing session identity remains diagnostic only until remediated.
8. Eliminate the same fallback in shared ClientHello and ServerHello loaders, or explicitly gate legacy compatibility behind a non-release diagnostic path.
9. Export `source_kind` alongside `source_sha256`, `scenario_id`, and `route_mode` in reviewed summary generation and downstream reviewed artifacts that already surface provenance.
10. Preserve canonical capture and session identities in downstream JSON, headers, or derived reports without path-based double counting.
11. Keep reviewed fixture refreshes reproducible from raw captures under `docs/Samples/Traffic dumps/` or documented reviewed capture sources.
12. Migrate release-facing provenance output to repository-relative raw dump paths while preserving exact checked-in path spelling and `source_sha256`.
13. Treat absolute fixture `source_path` values as legacy read compatibility only; no new generated header, release JSON, Markdown, or handoff artifact may emit an absolute workspace root.
14. Add migration tests proving that repository-root stripping does not change source identity and that outside-root, symlink, Unicode-normalized, or case-only aliases fail closed.
15. Persist packet-selection metadata or a deterministic derivation note for reviewed fixtures so raw-to-fixture reproduction can prove the selected frame, stream, direction, and reassembly source.
16. Preserve raw `supported_versions` vectors, including GREASE values and declared vector length, alongside the ordered non-GREASE view when the field is promoted.

Exit criteria:

1. Reviewed fixture artifacts and generated reviewed headers expose exact `supported_versions` vectors.
2. Reviewed provenance validation fails closed on missing identity fields.
3. Reviewed summary headers surface the canonical source-kind vocabulary needed to distinguish authoritative from advisory evidence.
4. Contract tests fail if a refresh removes the field, path aliases change counts, or missing `scenario_id` is normalized into fake session independence.
5. Common loader consumers can access exact `supported_versions` without reparsing raw JSON extension lists.
6. Generated release-facing provenance no longer leaks local workspace prefixes, while legacy absolute checked-in fixture paths remain readable only when they resolve inside the repository root.
7. Reproduction tests prove packet selection and reassembly metadata are stable for every fixture anchor touched by the slice.
8. GREASE raw vectors and non-GREASE views are both available to downstream tests, and neither can silently overwrite the other.

### Phase 3. Family Baseline, Classification, And Counting Repair

Tasks:

1. Add `supported_versions` to exact invariants in family-lane baselines.
2. Replace path-sensitive independence counting with canonical capture and session identities.
3. Audit `apple_ios_tls` membership against real fixtures. Keep the merged family only if every release-critical exact field is proven identical; otherwise split it.
4. Add a generator-side diagnostic that fails when a release-critical exact invariant collapses to empty, or when deduplicated counts change only because of file or path aliasing.
5. Keep `ios_chromium` disjoint from Apple TLS and prove the distinction with tests.
6. Emit explicit invariant-collapse reason codes instead of unlabeled empty vectors for release-critical fields.
7. Treat ALPN, extension order, supported groups, key-share group order, cipher-suite order, `supported_versions`, record version, legacy version, ECH presence, and compress-certificate algorithms as release-critical exact fields for family-merge decisions.
8. If an iOS skin is grouped with Apple TLS because platform policy requires WebKit, the plan must still prove the concrete wire fields are identical in the reviewed fixtures; platform rules alone are insufficient.
9. Emit separate `raw_tier`, `effective_tier`, `n_sample_raw`, `n_capture`, `n_source`, `n_session`, stale flags, and invariant-collapse diagnostics in generated or adjacent machine-readable artifacts.
10. Introduce generation-aware family classification or an equivalent exact-field cohort discriminator before computing iOS release evidence. The discriminator must separate modern iOS 26 Apple TLS from legacy iOS 17.2 and iOS 18.7 captures unless every release-critical exact field is proven identical.
11. When a mixed cohort is detected, emit `mixed_generation_exact_field` or a more specific reason code instead of empty invariants or downgraded confidence.
12. Emit a stable `cohort_id` for every family-lane baseline and downstream release predicate; if a baseline is diagnostic-only, emit `cohort_id = diagnostic_only` plus the reason it cannot gate release.

Exit criteria:

1. Wrong modern Apple or iOS `supported_versions` fails family baseline matching.
2. File or path aliasing does not change reported trust tier or confidence denominator.
3. Mixed-family empty-invariant collapse is no longer silent.
4. Family merge or split decisions are backed by checked-in fixture evidence and a red-to-green contract test, not by browser-brand assumptions.
5. Legacy iOS 17.2 and iOS 18.7 fixtures remain useful reviewed evidence for their own strata, but they cannot weaken modern iOS 26 exact invariants.
6. Release predicates cannot be rendered without `cohort_id`, and cohort assignment is deterministic across two regenerations from the same inputs.

### Phase 4. Matcher And Runtime Hardening

Tasks:

1. Teach `FamilyLaneMatchers.cpp` to reject wrong exact `supported_versions`.
2. Align runtime profile definitions in `td/mtproto/BrowserProfile.cpp` with reviewed fixture truth.
3. Preserve RU and unknown fail-closed semantics and keep them under dedicated contract tests.
4. Ensure the serializer keeps exact-field changes deterministic and does not mask them with unrelated randomness.
5. Keep lossy JA4-like fields diagnostic-only when exact reviewed fields exist; runtime selection and release matching must consume exact structured expectations.
6. Add black-hat tests for downgrade vectors that reintroduce TLS 1.1 or TLS 1.0 into modern iOS 26 Apple TLS, shuffle only GREASE values to evade simple checks, or alter extension body length while preserving a plausible maximum TLS version.
7. Add raw-wire GREASE-slot tests against generated ClientHello bytes so a candidate cannot pass with a missing GREASE slot, invalid GREASE class, shifted slot, or length-preserving but browser-visible raw-body drift.
8. Add plaintext-name leakage tests for route fallback and serializer error paths whenever ECH or QUIC policy code is touched.

Exit criteria:

1. Runtime and reviewed artifacts agree on release-critical Apple or iOS fields.
2. Route fail-closed tests remain green.
3. A wrong exact vector cannot be hidden by GREASE churn, padding differences, seed selection, or a JA4-style maximum-version summary.
4. A normalized exact match cannot hide raw GREASE-slot drift or plaintext-name leakage in generated wire bytes.

### Phase 5. Nightly, Monte Carlo, And Statistical Gate Hardening

Tasks:

1. Remove release dependence on lossy JA4-style helpers for fields that have exact reviewed truth.
2. Add nightly exact-field gates for authoritative families, starting with Apple or iOS `supported_versions`.
3. Add cluster-collapsed confidence reporting plus scorable-versus-unavailable reporting to transport and distributional outputs.
4. Keep nightly gates stratified by family, cohort, route lane, and evidence lane.
5. Label nightly seed counts as runtime coverage only and keep them out of trust-tier denominators and release-evidence summaries.
6. For binary release predicates, compare against cluster-collapsed Wilson lower bounds when a confidence floor is introduced.
7. For non-binary distributional metrics, use cluster-resampled bootstrap only after exact reviewed gates pass.
8. Emit statistical metadata in release artifacts (`x_pass`, `n_cluster`, `cluster_level`, `ci_method`, optional `bootstrap_resamples`) and fail closed if any required field is missing.
9. Ensure CI validates lineage ordering between transport, active-probing, and rendered release-evidence artifacts.
10. Add negative tests where raw sample count is high but deduplicated capture or session count is too low; the lane must not promote tier or confidence.
11. Add negative tests where every seed passes but the reviewed exact field fails; exact failure must still dominate.
12. Add deterministic golden tests for Wilson lower bound edge cases: `n_cluster = 0`, `x_pass = 0`, `x_pass = n_cluster`, and invalid `x_pass > n_cluster`.
13. Add small-n wording and serialization tests for `n_cluster = 1` and `n_cluster = 2`; generated reports must avoid high-confidence wording and must show count-first evidence plus Wilson metadata.
14. Ensure every statistical JSON record uses repository-relative `source_artifacts` and content-hash lineage references, not local absolute paths or timestamp-only freshness claims.
15. Add a release predicate registry or generated equivalent before rendering any confidence statement, and require CI to fail if a predicate, threshold, cohort, or confidence method is missing.
16. Add optional-stopping adversarial tests where a failed predicate is removed, a threshold is lowered, or a later passing nightly run hides an earlier failure; all must remain release-blocking.

Exit criteria:

1. The old Apple or iOS vector defect fails nightly or structural release-gating checks.
2. Statistical summaries can no longer pass while exact release-critical fields are wrong.
3. Release artifacts disclose `n_capture`, `n_session`, raw sample count, and `n_seed` separately where applicable.
4. Release artifacts fail closed if confidence metadata is absent, denominator identity is unavailable, `cohort_id` is absent, or the lineage graph is stale.
5. Release artifacts fail closed if the predicate registry is missing, stale, or outcome-tuned after failures were observed.

### Phase 6. Transport, Active Probing, And Route Transition Hardening

Tasks:

1. Extend transport evidence only from real captures with validated SYN metadata. Do not synthesize SYN traits.
2. Add adversarial and integration tests for selective drop, reorder, replay, fallback transitions, and partial blocking.
3. Verify that route transitions do not emit a unique signature.
4. Keep `unavailable` transport metrics explicit until real SYN-phase evidence exists.
5. Keep RU and unknown route policy ECH-off and QUIC-blocked even when browser mimicry would otherwise enable ECH or QUIC outside Russia.
6. Test partial blocking where only selected packets are dropped, including retry, recovery, fallback, and timeout paths; no path may emit a distinctive probe or diagnostic packet on the wire.
7. Keep active-probing evidence separate from browser-fidelity fixture evidence while still making active-probing failure release-blocking.
8. Add plaintext-name leakage assertions for every ECH-disabled, QUIC-blocked, fallback, retry, timeout, and error path; the test must inspect wire bytes or structured packet events, not only logs.
9. Add a positive non-RU browser-fidelity control when ECH or QUIC behavior is asserted, so RU blocking does not become a shortcut for untested non-RU mimicry.

Exit criteria:

1. Transport evidence remains fail-closed.
2. Active probing and route-transition suites are deterministic and CI-runnable.
3. Route transition evidence exposes pass, fail, or unavailable states without inventing transport metrics.
4. Route transition evidence proves no plaintext target-name leak or unique probe packet is emitted on blocked or fallback lanes.

### Phase 7. CI, Documentation, And Release-Evidence Consolidation

Tasks:

1. Keep this file as the single authoritative fingerprint hardening plan.
2. Remove legacy plan and audit files, and update workflow and operator-guide references to this document.
3. Ensure release evidence references only reviewed-lane gates, transport status, active-probing status, deduplicated capture or session counts, and explicit unavailable states.
4. Keep trust-tier semantics sourced from the canonical machine-readable tier spec.
5. Maintain documentation and CI contract tests that pin provenance audit, seed or evidence separation, and cluster-collapsed confidence language.
6. Correct any operator or CI prose that claims missing SYN-phase transport metrics collapse to `0.0`; the documented contract is metric value `null` with `availability = unavailable` and top-level `pending` where appropriate.
7. Add lineage-order checks that fail when release evidence is rendered from older transport, active-probing, family-baseline, or reviewed-fixture inputs.
8. Keep CI job names aligned with release policy: reviewed corpus gates are release-blocking, imported smoke is informational, and active-probing or transport status may block through generated release evidence rather than by masquerading as reviewed browser evidence.
9. Add content-addressed lineage checks that fail when a downstream artifact has a fresh timestamp but stale upstream SHA-256, stale generator SHA-256, stale parser version, or mismatched command line.
10. Add documentation contracts that reject local absolute path leakage and require repository-relative raw dump anchors in generated evidence and operator-facing summaries.
11. Add CI checks that compare the release predicate registry against rendered release artifacts and fail when predicates are missing, thresholds differ, or failed or unavailable predicates are omitted from generated Markdown.
12. Add documentation contracts that forbid "run until green" or percentage-only language and require failed, unavailable, and diagnostic-only predicates to remain visible in operator-facing summaries.

Exit criteria:

1. Only one active fingerprint hardening plan remains in the repository.
2. CI triggers, workflow contract tests, and operator documentation reference the same plan path and evidence semantics.
3. Generated release evidence can identify every upstream artifact and refuses stale or missing lineage.
4. Generated release evidence uses content hashes and repository-relative paths, not timestamp-only claims or local workspace roots.
5. Generated release evidence is a complete projection of the pre-registered predicate universe, not a curated subset of passing checks.

### Phase 8. Survive, Refactor, And Closeout

Tasks:

1. Re-run contract, adversarial, integration, fuzz, and stress suites after the implementation slices land.
2. Refresh generated artifacts deterministically and verify there is no drift.
3. Publish blocker state explicitly: exact gates, nightly gates, transport status, active-probing status, reviewed smoke, imported informational smoke, provenance census, deduplicated counts, and seed-only coverage.
4. Record out-of-scope issues as `NOTICED BUT NOT TOUCHING` instead of patching adjacent surfaces opportunistically.
5. Run a separate verification pass that reads this plan, the handoff records, generated artifacts, and test outputs without changing implementation code.
6. Verify there are no new TODOs, no suppressed errors, no hand-edited generated outputs, and no release claims backed only by advisory evidence.
7. Verify the complete predicate registry, mutation catalog, packet-selection appendix, and lineage manifest are mutually consistent before closeout.

Exit criteria:

1. Every blocker has an explicit pass, fail, or unavailable state with evidence.
2. No release claim depends on advisory or imported evidence, path-based duplicate counting, or Monte Carlo seed inflation.
3. Every Risk ID in Section 5 has at least one passing test that was demonstrated red before the fix or explicitly justified as a pre-existing contract test.
4. No release claim depends on a post-outcome threshold edit, missing packet selector, hidden GREASE-slot drift, or untested plaintext-name fallback behavior.

## 7. Validation Matrix

| Slice | Command | Required outcome |
|---|---|---|
| Fixture provenance suite | `python3 -m unittest discover -s test/analysis -p 'test_*provenance*.py'` | Reviewed and imported provenance, deduplication, and independence contracts green |
| Family baseline evidence-tier suite | `python3 -m unittest discover -s test/analysis -p 'test_build_family_lane_baselines*.py'` | Tier counting, deduplication, and stress invariants green |
| Analysis contract suite | `python3 -m unittest discover -s test/analysis -p 'test_*contract.py'` | Contract tests green for extractor, baseline, CI, and plan/documentation contracts |
| Common TLS loader contract suite | `python3 -m unittest discover -s test/analysis -p 'test_common_tls*_contract.py'` | Shared ClientHello and ServerHello loaders reject missing release identity and expose exact supported-version data |
| Supported versions parser fuzz | `python3 -m unittest discover -s test/analysis -p 'test_*supported_versions*.py' && python3 test/analysis/fuzz_supported_versions_parser.py` | Canonical `0x002B` parser survives hostile length, truncation, GREASE, duplicate, and random-byte inputs |
| Raw-to-fixture reproduction contract | `python3 -m unittest discover -s test/analysis -p 'test_raw_fixture_reproduction_contract.py'` | Regenerated reviewed fixture fields are byte-identical to raw dump truth for release-critical extracted fields |
| Fingerprint mutation adequacy contract | `python3 -m unittest discover -s test/analysis -p 'test_fingerprint_mutation_adequacy_contract.py'` | Every controlled mutant has `mutation_id`, expected failing lane, Risk IDs, and zero unexpected false-negative outcomes |
| Fixture independence and collision suite | `python3 -m unittest discover -s test/analysis -p 'test_check_fixture_registry_independence.py' && python3 -m unittest discover -s test/analysis -p 'test_fixture_metadata_collision.py'` | Path aliases and duplicate metadata cannot inflate independence claims |
| Plan contract suite | `python3 -m unittest discover -s test/analysis -p 'test_fingerprint_hardening_plan_contract.py'` | The master plan, workflow references, and fail-closed transport language remain pinned |
| Reviewed summary generation | `python3 test/analysis/merge_client_hello_fixture_summary.py --input-dir test/analysis/fixtures/clienthello --out-header test/stealth/ReviewedClientHelloFixtures.h` | Header refresh is deterministic and includes exact reviewed provenance plus `supported_versions` fields |
| Family baseline generation | `python3 test/analysis/build_family_lane_baselines.py --input-dir test/analysis/fixtures/clienthello --output test/stealth/ReviewedFamilyLaneBaselines.h` | Baseline refresh is deterministic, no path-based source inflation remains, and release-critical invariant collapse is diagnosed rather than hidden |
| Transport observation extraction | `python3 test/analysis/extract_tcp_transport_signatures.py --repo-root . --output test/analysis/transport_coherence_observations.json` | Observations preserve metric-level `unavailable` semantics and do not synthesize SYN traits |
| Statistical metadata contract | `python3 -m unittest discover -s test/analysis -p 'test_statistical_release_metadata_contract.py'` | Wilson lower-bound, unavailable confidence, denominator, seed, and bootstrap metadata semantics are pinned |
| Release cohort identity contract | `python3 -m unittest discover -s test/analysis -p 'test_release_cohort_identity_contract.py'` | Release predicates include `cohort_id`, mixed iOS generations are not pooled, and missing cohort identity fails closed |
| Raw dump source-path portability contract | `python3 -m unittest discover -s test/analysis -p 'test_fixture_source_path_portability_contract.py'` | Absolute workspace prefixes are legacy-only, repository-relative paths preserve exact raw dump spelling, and Unicode/path aliases do not inflate identity |
| Content-addressed lineage contract | `python3 -m unittest discover -s test/analysis -p 'test_artifact_lineage_content_hash_contract.py'` | Fresh-timestamp stale-content mutants fail and lineage records include upstream input hashes, generator hash, parser version, and command line |
| Small-n statistical wording contract | `python3 -m unittest discover -s test/analysis -p 'test_small_n_statistical_wording_contract.py'` | `n_cluster < 3` outputs are count-first, denominator-explicit, and do not claim high-confidence percentages |
| Capture-selection reassembly contract | `python3 -m unittest discover -s test/analysis -p 'test_capture_selection_reassembly_contract.py'` | Wrong direction, retransmission, second ClientHello, partial record fallback, and unrelated stream concatenation cannot produce trusted fixture truth |
| Release predicate registry contract | `python3 -m unittest discover -s test/analysis -p 'test_release_predicate_registry_contract.py'` | Release predicates disclose cohort, threshold, denominator, confidence method, positive controls, hostile mutants, and owner surfaces before outcome evaluation |
| Outcome-tuned threshold adversarial contract | `python3 -m unittest discover -s test/analysis -p 'test_outcome_tuned_threshold_adversarial.py'` | Post-outcome threshold, cohort, predicate-set, or confidence-method changes are rejected as `invalid_outcome_tuned` |
| C++ discovery refresh | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON && cmake --build build --target run_all_tests --parallel 4 && cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON` | Generated headers compile and `ctest` discovery is refreshed from the current `run_all_tests` binary |
| Reviewed corpus smoke | `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_validation.json --fixtures-root test/analysis/fixtures/clienthello --server-hello-fixtures-root test/analysis/fixtures/serverhello` | Reviewed lane green |
| Imported corpus smoke | `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_imported.json --fixtures-root test/analysis/fixtures/imported/clienthello --server-hello-fixtures-root test/analysis/fixtures/imported/serverhello` | Informational only; never release-gating |
| Apple/iOS exact corpus lane | `./build/test/run_all_tests --filter IosAppleTlsCorpus1k` | Wrong Apple/iOS exact vector fails |
| Safari exact corpus lane | `./build/test/run_all_tests --filter Safari26_3Invariance1k` | Safari exact invariants remain pinned |
| iOS multi-dump lane | `./build/test/run_all_tests --filter TLS_MultiDumpIosAppleTls` | Family-lane and cross-family checks green |
| C++ fingerprint mutation adequacy | `./build/test/run_all_tests --filter FingerprintMutationAdequacy` | Controlled wire mutants fail the intended exact-field, route-policy, or matcher gate while positive real-fixture controls pass |
| C++ GREASE slot validity | `./build/test/run_all_tests --filter TlsGreaseSlotValidity` | Generated wire preserves valid GREASE classes, slot positions, and raw extension-body lengths required by reviewed fixture truth |
| C++ plaintext-name leakage | `./build/test/run_all_tests --filter TlsPlaintextNameLeak` | ECH-disabled, QUIC-blocked, retry, timeout, error, and partial-blocking paths do not emit plaintext target-name or unique probe traffic |
| Route and active policy slice | `ctest --test-dir build --output-on-failure -j 14 -R 'RouteEchQuic|TlsRuntimeActivePolicy|TlsHmacReplayAdversarial'` | RU and unknown fail-closed policy green |
| Nightly corpus lane | `env TD_NIGHTLY_CORPUS=1 ./build/test/run_all_tests --filter 1k` | Exact nightly gates and distributional reporting green |
| Transport status generation | `python3 test/analysis/build_transport_coherence_status.py --repo-root . --now-utc <RFC3339Z>` | Required metrics emitted as pass, fail, or unavailable; never guessed |
| Release artifact rendering | `python3 test/analysis/render_fingerprint_policy_artifacts.py --repo-root . --now-utc <RFC3339Z>` | Deterministic release evidence artifacts |
| Artifact lineage contract | `python3 -m unittest discover -s test/analysis -p 'test_*lineage*.py'` | Downstream release artifacts fail when upstream producer artifacts are stale or untraceable |
| Policy CI contract suite | `python3 -m unittest discover -s test/analysis -p 'test_fingerprint_policy_ci_contract.py' && python3 -m unittest discover -s test/analysis -p 'test_fingerprint_policy_generation_contract.py'` | Workflow, release artifact semantics, and reviewed/imported lane boundaries remain fail-closed |

Commands that reference new tests are mandatory outputs of Phase 1. Before those files exist, their absence is a blocker for the relevant slice, not a reason to skip the validation row.

## 8. Release Blockers And Exit Criteria

The fingerprint stack remains release-blocked until all of the following are true:

1. `supported_versions` is a first-class reviewed field from extractor to generated header to family baseline to matcher to nightly gate.
2. A wrong modern Apple/iOS `supported_versions` vector fails in at least three independent lanes.
3. Authoritative iOS families are either split correctly, generation-aware family classification is in place, or the merged cohort is formally proven safe across every release-critical exact field.
4. Nightly release-gating contains at least one exact reviewed-field gate for each authoritative family it claims to protect.
5. Transport evidence uses real SYN-phase captures where required, or the affected metrics remain explicitly unavailable and release-blocking.
6. Active-probing and route-transition suites are deterministic, green, and attached to release evidence.
7. Reviewed smoke is green, imported smoke remains informational, and advisory profiles remain excluded from release-mode runtime selection.
8. Provenance census is green: deduplicated capture and session counts are explicit, reviewed and imported collisions are resolved, and file or path aliases do not change tier counts.
9. Release-facing confidence intervals and thresholds are computed from deduplicated capture or session outcomes; Monte Carlo seed counts remain segregated as runtime-only coverage.
10. Relevant OWASP ASVS L2 checks are covered by tests: malformed input rejection, provenance validation, fail-closed error handling, and no secret leakage in tooling or diagnostics.
11. Missing `scenario_id` no longer falls back to `artifact_path.stem` for Tier1-or-higher counting, confidence reporting, or release evidence.
12. Generated reviewed artifacts expose the source-kind vocabulary and exact provenance needed to audit authoritative versus advisory evidence end to end.
13. No release-critical invariant collapses to an unlabeled empty vector; every empty critical field has a tested diagnostic reason or remains release-blocking.
14. Operator and CI documentation use the same fail-closed transport semantics as the code: top-level `pending` where required and metric-level `unavailable`, never `0.0` substitution.
15. Artifact refresh follows the Artifact Dependency DAG; no downstream artifact is hand-edited or refreshed before its upstream truth is locked.
16. Release evidence artifacts publish clustered statistical metadata fields for every release-facing confidence statement.
17. Cross-artifact lineage ordering is validated in CI so downstream summaries cannot lag behind upstream producer artifacts.
18. `common_tls.py` and every release-facing loader reject or explicitly mark missing session identity as unavailable; no `artifact_path.stem` fallback can influence release counts.
19. Source-kind classification is single-owned and consumed consistently by extractor, loader, baseline, registry, renderer, CI, and documentation surfaces.
20. Every critical or high risk in Section 5 has a hostile test that was observed red before implementation or is documented as an already-red pre-existing contract at the start of the slice.
21. Raw-to-fixture differential reproduction is green for every reviewed fixture touched by the release-critical schema, and every regenerated release-critical extracted field is byte-identical to checked-in fixture truth.
22. Fingerprint Mutation Adequacy is green: every release-critical exact-field, route-policy, provenance, and statistical-denominator gate has at least one controlled mutant, every mutant reports a stable `mutation_id`, and no expected failing mutant survives as a false-negative.
23. Every release-facing predicate publishes `cohort_id`; modern iOS 26 Apple TLS, legacy iOS 17.2 or iOS 18.7 Apple TLS, imported diagnostics, and runtime seed coverage cannot share one denominator.
24. Release-facing provenance is repository-relative and content-addressed; local absolute workspace roots are legacy inputs only and never appear in generated release artifacts, CI diagnostics, or operator documentation.
25. Content-addressed lineage is green: every release-facing generated artifact records upstream input hashes, generator hash, parser version, command line, sorted input list, and fixed `now_utc` where applicable.
26. Small-n release language is statistically conservative: `n_cluster < 3` is reported as exact anchoring only, never as high-confidence percentage fidelity.
27. Release predicates are pre-registered and threshold-frozen before outcomes are evaluated; optional stopping, predicate omission, or post-failure threshold edits block release evidence.
28. Capture-selection and reassembly integrity is green: every reviewed fixture used as release evidence has stable frame, stream, direction, record-source, and parser-version provenance, and wrong-selection mutants fail.
29. GREASE raw-wire validity is green: normalized exact fields cannot pass while GREASE slots, GREASE value class, or raw extension-body lengths drift from reviewed fixture truth.
30. Plaintext-name leakage gates are green: ECH-disabled and QUIC-blocked route transitions, retries, errors, and partial blocking do not emit sensitive target names or unique probe traffic.

## 9. Document Governance

1. This file is the only active fingerprint hardening plan.
2. Historical drift must be prevented by updating this file instead of spawning sibling plan or audit documents for the same initiative.
3. If future work needs a closure memo, it belongs in the status and blocker sections of this document or in generated evidence artifacts, not in a parallel narrative plan.

## 10. Agent Handoff Schema

Each implementing agent must end its slice with a machine-readable handoff record saved as an Agent Gate Artifact under `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/`:

```json
{
  "slice": "family-baseline",
  "phase_id": "Phase 3",
  "status": "pass",
  "agent_gate_artifact": "docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/phase3_family-baseline_<agent>_<utc>.json",
  "fixture_anchors": [
    "test/analysis/fixtures/clienthello/ios/safari26_3_1_ios26_3_1_a.clienthello.json"
  ],
  "raw_dump_anchors": [
    "docs/Samples/Traffic dumps/iOS/Ios 26.3.1, Safari 26.3.1.pcap"
  ],
  "canonical_source_paths": [
    {
      "fixture_source_path_input": "/home/david_osipov/tdlib-obf/docs/Samples/Traffic dumps/iOS/Ios 26.3.1, Safari 26.3.1.pcap",
      "repo_relative_source_path": "docs/Samples/Traffic dumps/iOS/Ios 26.3.1, Safari 26.3.1.pcap",
      "source_sha256": "4d463a3c4a8fec67c184c536dedf063441a61c70303aba3ead72e7524e7e840f",
      "path_status": "legacy_absolute_resolved_inside_repo"
    }
  ],
  "packet_selection": [
    {
      "fixture_id": "safari26_3_1_ios26_3_1_a:frame76",
      "frame_number": 76,
      "tcp_stream": 1,
      "direction": "client_to_server",
      "record_source": "frame_payload",
      "reassembly_fallback": false,
      "duplicate_clienthello_candidates": 0
    }
  ],
  "wire_field_anchors": [
    {
      "extension": "0x002B",
      "body_hex": "063a3a03040303",
      "raw_supported_versions": ["0x3A3A", "0x0304", "0x0303"],
      "family_id": "apple_ios_tls",
      "cohort_id": "ios26_apple_tls_supported_versions_0304_0303",
      "non_grease_supported_versions": ["0x0304", "0x0303"]
    }
  ],
  "owner_files": [
    "test/analysis/build_family_lane_baselines.py"
  ],
  "red_command": "python3 -m unittest discover -s test/analysis -p 'test_build_family_lane_baselines_evidence_tiers.py'",
  "red_failure_excerpt": "assertion excerpt proving the current blind spot",
  "green_command": "python3 -m unittest discover -s test/analysis -p 'test_build_family_lane_baselines_evidence_tiers.py'",
  "green_output_excerpt": "summary of passing tests",
  "generated_outputs": [
    "test/stealth/ReviewedFamilyLaneBaselines.h"
  ],
  "mutants": [
    {
      "mutation_id": "ios26_supported_versions_tls11_reintroduced",
      "cohort_id": "ios26_apple_tls_supported_versions_0304_0303",
      "mutated_field": "non_grease_supported_versions",
      "mutation_class": "exact_field_downgrade",
      "expected_failure_lane": "family-baseline-exact-match",
      "false-negative": 0,
      "false-positive": 0
    }
  ],
  "raw_fixture_reproduction": {
    "status": "pass",
    "mismatch_count": 0,
    "release_critical_fields": ["supported_versions", "extension_types", "cipher_suites"]
  },
  "artifact_refresh_order": [
    "test/analysis/build_family_lane_baselines.py",
    "test/stealth/ReviewedFamilyLaneBaselines.h"
  ],
  "content_lineage": [
    {
      "generated_artifact": "test/stealth/ReviewedFamilyLaneBaselines.h",
      "upstream_inputs_sha256": [
        {
          "path": "test/analysis/fixtures/clienthello/ios/safari26_3_1_ios26_3_1_a.clienthello.json",
          "sha256": "<fixture-json-sha256>"
        }
      ],
      "generator_path": "test/analysis/build_family_lane_baselines.py",
      "generator_sha256": "<generator-sha256>",
      "command": "python3 test/analysis/build_family_lane_baselines.py --input-dir test/analysis/fixtures/clienthello --output test/stealth/ReviewedFamilyLaneBaselines.h",
      "now_utc": "<fixed-rfc3339z-or-null>"
    }
  ],
  "socraticode": {
    "status": "green",
    "symbols": ["load_samples", "build_baselines"],
    "impact_targets": ["test/analysis/build_family_lane_baselines.py"]
  },
  "risks_covered": ["RISK-FP-03", "RISK-FP-10", "RISK-FP-13", "RISK-FP-14", "RISK-FP-22"],
  "statistics": {
    "cohort_id": "ios26_apple_tls_supported_versions_0304_0303",
    "cluster_level": "capture",
    "n_sample_raw": 0,
    "n_capture": 0,
    "n_source": 0,
    "n_session": 0,
    "n_seed": 0
  },
  "release_predicates": [
    {
      "predicate_id": "ios26_supported_versions_exact",
      "predicate_version": 1,
      "family_id": "apple_ios_tls",
      "cohort_id": "ios26_apple_tls_supported_versions_0304_0303",
      "route_lane": "non_ru_egress",
      "evidence_lane": "reviewed",
      "threshold_freeze_status": "pre_registered_before_red",
      "confidence_method": "wilson",
      "threshold": null,
      "positive_controls": ["safari26_3_1_ios26_3_1_a:frame76"],
      "mutation_ids": ["ios26_supported_versions_tls11_reintroduced"]
    }
  ],
  "name_leakage": {
    "status": "not_applicable_to_slice",
    "forbidden_plaintext_names_checked": []
  },
  "optional_stopping_guard": {
    "status": "not_outcome_tuned",
    "rerun_policy": "single recorded red command followed by same green command"
  },
  "small_n_wording": {
    "n_cluster": 1,
    "allowed_summary": "1/1 reviewed capture matched; distributional confidence unavailable for release equivalence",
    "forbidden_summary_examples": ["100% fidelity", "high confidence"]
  },
  "noticed_but_not_touching": []
}
```

This handoff record is required for every slice and must be attached to the agent handoff note before the next slice begins.
