<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Test And Pipeline Hardening Plan

**Plan ID:** fingerprint-test-and-pipeline-hardening-2026-05-09  
**Date:** 2026-05-09  
**Last updated:** 2026-05-10  
**Status:** Partially Executed (initial runtime/test/tooling hardening merged; pipeline-wide baseline and matcher work still pending)  
**Primary Goal:** Close the structural validation gaps that allowed a real Apple/iOS `supported_versions` regression to evade the broad fingerprint test stack, and harden the full reviewed-fixture pipeline from raw extraction through release-gating test suites.  
**Threat Context:** high-budget adaptive DPI performing multi-feature correlation across TLS structure, extension semantics, route behavior, and family-level wire-image consistency.

**Non-negotiable method:** Contract -> Attack -> Red -> Green -> Survive -> Refactor. No implementation change without failing tests first.

---

## 0. Why This Plan Exists

The recent Apple/iOS `supported_versions` defect was not missed because one assertion was forgotten. It was missed because multiple validation layers were structurally incapable of seeing it.

The verified failure chain is:

1. Real reviewed iOS Safari fixtures already contained the exact ground truth in extension `0x002B`.
2. The broad statistical helper path reduced `supported_versions` to the maximum non-GREASE version, which erased the decisive difference between `{0x0304, 0x0303}` and `{0x0304, 0x0303, 0x0302, 0x0301}`.
3. The reviewed family-baseline generator never modeled `supported_versions` as an exact invariant or set-catalog dimension.
4. The family-lane matcher never checked `supported_versions` because the baseline schema never exposed it.
5. The Apple iOS multi-dump stats lane is advisory-tier and explicitly skips exact/distributional gates when invariants are empty.
6. Apple iOS family classification is too coarse, so list-valued exact invariants are already hollowed out by intersection across mixed families.
7. The nightly Monte Carlo lane only checks counts, envelope, and ECH presence, not the exact version vector.
8. The exact Safari/iOS `supported_versions` assertions now merged on `master` are useful post-incident follow-up tests, not proof that the old safety net worked.

This plan exists to harden the pipeline end-to-end so that a regression of this class fails in multiple independent places before release.

---

## 1. Verified Repository Reality

### 1.1 Confirmed Blind Spots

| ID | Severity | Location | Verified gap |
|---|---|---|---|
| FP-01 | CRITICAL | `test/stealth/CorpusStatHelpers.h` | `supported_versions` is collapsed to `max_supported_version` for JA4-style segment A, losing the full vector |
| FP-02 | CRITICAL | `test/analysis/build_family_lane_baselines.py` | reviewed family baselines omit `supported_versions` from exact invariants and set-membership catalogs |
| FP-03 | CRITICAL | `test/stealth/FamilyLaneMatchers.cpp` | matcher cannot reject wrong `supported_versions` because it never inspects the field |
| FP-04 | HIGH | `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp` | Apple iOS lane is advisory-tier and skips exact/distributional gates when invariants are empty |
| FP-05 | HIGH | `test/analysis/build_family_lane_baselines.py` | coarse family mapping folds Safari iOS and Firefox iOS into `apple_ios_tls`, which weakens exact invariants by intersection |
| FP-06 | HIGH | `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp` | nightly lane validates counts/envelope/ECH presence only, not authoritative exact fields like `supported_versions` |
| FP-07 | HIGH | `test/analysis/merge_client_hello_fixture_summary.py` | reviewed fixture summary exports groups/ALPN/compress-cert but not `supported_versions` |
| FP-08 | IMPORTANT | `test/analysis/extract_client_hello_fixtures.py` | extractor preserves raw `0x002B` body in `extensions[*].body_hex` but does not promote a dedicated summarized `supported_versions` field |

### 1.2 Confirmed Ground Truth Already Exists

The reviewed fixture corpus already contains enough evidence to catch the bug.

1. `test/analysis/fixtures/clienthello/ios/safari26_3_1_ios26_3_1_a.clienthello.json` contains extension `0x002B` with body `063a3a03040303`.
2. That body corresponds to GREASE + TLS 1.3 + TLS 1.2 only.
3. The failure was therefore not a data-collection failure. It was a pipeline modeling failure.

### 1.3 Current Merged Reality (2026-05-10)

`master` now contains the first bounded hardening response from PR #18:

1. a runtime fix in `td/mtproto/BrowserProfile.cpp` restricting Apple TLS `supported_versions` to `{772, 771}`;
2. exact-vector follow-up tests in:
   - `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp`
   - `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`
3. extractor/tooling compatibility fixes in `test/analysis/extract_client_hello_fixtures.py` plus
   the added contract coverage for `tshark` TCP option-kind fallback and hexadecimal `ip.id`
   parsing;
4. reviewed ServerHello fixture refreshes and this planning document.

These merged changes are useful immediate regressions and tooling hardening, but they do **not**
solve the underlying pipeline gap by themselves. This plan treats them as the first landed slice of
the response, with the broader baseline, matcher, family-classification, and nightly-gating work
still pending.

---

## 2. Scope

### 2.1 In Scope

1. Raw reviewed fixture extraction and normalization.
2. Reviewed summary generation.
3. Family baseline schema generation.
4. Family-lane matching logic.
5. 1k corpus exact-match and adversarial suites.
6. Multi-dump baseline and stats suites.
7. Nightly Monte Carlo release-gating checks.
8. Family classification policy for iOS lanes.
9. Documentation and operator guidance for refreshing reviewed artifacts.

### 2.2 Key Files In Scope

1. `test/analysis/extract_client_hello_fixtures.py`
2. `test/analysis/merge_client_hello_fixture_summary.py`
3. `test/analysis/build_family_lane_baselines.py`
4. `test/stealth/ReviewedClientHelloFixtures.h` (generated)
5. `test/stealth/ReviewedFamilyLaneBaselines.h` (generated)
6. `test/stealth/FamilyLaneMatchers.cpp`
7. `test/stealth/CorpusStatHelpers.h`
8. `test/stealth/test_tls_multi_dump_ios_apple_tls_baseline.cpp`
9. `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
10. `test/stealth/test_tls_multi_dump_ios_chromium_stats.cpp`
11. `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
12. `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp`
13. `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`
14. `td/mtproto/BrowserProfile.cpp`

### 2.3 Out Of Scope

1. Unrelated Telegram-core correctness hardening outside the fingerprint stack.
2. Replacing reviewed capture evidence with synthetic-only fixtures.
3. Weakening existing entropy, GREASE, or permutation diversity tests just to improve runtime.

---

## 3. Objectives

1. Make `supported_versions` a first-class validated field from extraction through baseline matching.
2. Ensure a wrong Apple/iOS `supported_versions` vector fails in at least three independent lanes.
3. Split or otherwise refine family classification so authoritative iOS families do not lose exact invariants through over-broad intersection.
4. Preserve fast developer feedback while keeping nightly release-grade validation strict.
5. Convert the current post-incident singleton regressions into durable, pipeline-level protection.

---

## 4. Contract Snapshot

### CONTRACT: Reviewed Supported Versions Summary

1. **Inputs:** raw extension `0x002B` body from reviewed fixture JSON.
2. **Outputs:** ordered non-GREASE `supported_versions` vector, preserved exactly as seen on wire.
3. **Preconditions:** extension body length byte is well-formed and even.
4. **Postconditions:** malformed or truncated bodies fail closed; valid bodies preserve exact order and values.
5. **Side effects:** none beyond emitted fixture artifact fields.

### CONTRACT: Family Exact Invariants

1. Authoritative family baselines must include `supported_versions` as an exact invariant when it is stable across the family-lane corpus.
2. A family baseline may not silently ignore a populated, stable exact field merely because another lossy surrogate exists.
3. Empty exact invariants in authoritative lanes require explicit diagnostic output and must not quietly downgrade protection.

### CONTRACT: Family Classification

1. Different wire families must not be intersected into one exact-invariant lane unless they are proven identical on all release-critical exact fields.
2. If a classification rule causes core exact invariants to collapse to empty, that is a classification defect, not a normal state.

### CONTRACT: Statistical Helper Use

1. Lossy summaries such as JA4-style segment fields may remain as supplemental diagnostics.
2. They must not be the sole release gate for a field when the exact reviewed value is already available.

### CONTRACT: Release-Gating Test Coverage

1. Every authoritative family must have at least one exact-vector gate in the default test path.
2. Nightly lanes must validate at least one exact release-critical field per authoritative family, not only counts or envelopes.

---

## 5. Risk Register

| Risk ID | Category | Attack / failure mode | Impact |
|---|---|---|---|
| RISK-FP-01 | Schema omission | wrong `supported_versions` passes because no artifact records it | release of distinguishable Apple/iOS wire image |
| RISK-FP-02 | Lossy metric overreach | JA4-style surrogate hides exact vector drift | false confidence from statistical pass |
| RISK-FP-03 | Family coarsening | mixed iOS families intersect to empty invariants | baseline exact-match lane becomes permissive |
| RISK-FP-04 | Advisory downgrade leakage | advisory-tier comment silently becomes practical exemption | release-blocking gaps remain open |
| RISK-FP-05 | Generated header drift | extractor, merger, and generated headers diverge | tests validate stale or incomplete artifacts |
| RISK-FP-06 | Gate fragmentation | exact-vector tests exist only as ad hoc regressions | local fix passes, systematic pipeline remains blind |
| RISK-FP-07 | Nightly undercoverage | nightly run passes on envelope/count checks despite semantic drift | delayed detection of capture-fidelity regressions |

---

## 6. Workstreams

### Workstream A: Promote `supported_versions` Into Reviewed Artifacts

**Files:**

1. `test/analysis/extract_client_hello_fixtures.py`
2. `test/analysis/merge_client_hello_fixture_summary.py`
3. `test/stealth/ReviewedClientHelloFixtures.h`

**Tasks:**

1. Add an extracted ordered `non_grease_supported_versions` field derived from extension `0x002B`.
2. Preserve strict fail-closed parsing for malformed/truncated `0x002B` payloads.
3. Export the new field into the reviewed summary header alongside groups, extension order, ALPN, and compress-cert data.
4. Add contract tests that prove GREASE stripping, ordering preservation, and malformed-body rejection.

**Exit Criteria:**

1. Reviewed Safari/iOS and iOS Apple fixtures expose exact `supported_versions` constants in generated C++ artifacts.
2. No reviewed artifact refresh can drop the field silently.

### Workstream B: Extend Family Baselines And Matchers

**Files:**

1. `test/analysis/build_family_lane_baselines.py`
2. `test/stealth/ReviewedFamilyLaneBaselines.h`
3. `test/stealth/FamilyLaneMatchers.cpp`

**Tasks:**

1. Add `supported_versions` to `ExactInvariants` generation and emitted baseline artifacts.
2. Decide whether the field also belongs in the set-membership catalog for families where exact stability is not guaranteed.
3. Teach `FamilyLaneMatcher::matches_exact_invariants(...)` to reject wrong exact vectors.
4. Add generator-contract tests that pin the new schema so future refreshes cannot regress it silently.

**Exit Criteria:**

1. A wrong Apple/iOS `supported_versions` vector fails family baseline matching.
2. Generated baseline headers carry the new invariant for authoritative stable lanes.

### Workstream C: Repair iOS Family Classification

**Files:**

1. `test/analysis/build_family_lane_baselines.py`
2. `test/analysis/fixtures/clienthello/ios/*.json`
3. `test/stealth/ReviewedFamilyLaneBaselines.h`

**Tasks:**

1. Audit current `apple_ios_tls` membership and identify which samples are truly wire-identical.
2. Split the current lane at minimum where Firefox iOS and Safari/WebKit iOS diverge on core exact fields.
3. Keep `ios_chromium` separate and verify it remains disjoint from Apple/WebKit iOS on supported versions, extension traits, and other core invariants.
4. Add a generator-side diagnostic that flags authoritative family-lanes whose core exact invariants collapse to empty.

**Exit Criteria:**

1. Authoritative iOS lanes no longer lose core exact invariants through over-broad intersection.
2. `supported_versions` and other core fields remain non-empty where the reviewed corpus proves stability.

### Workstream D: Harden Exact And Multi-Dump Test Suites

**Files:**

1. `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp`
2. `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`
3. `test/stealth/test_tls_multi_dump_ios_apple_tls_baseline.cpp`
4. `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
5. `test/stealth/test_tls_multi_dump_ios_chromium_stats.cpp`

**Tasks:**

1. Keep the merged exact-vector Safari/iOS assertions as permanent regressions.
2. Add multi-dump exact checks for `supported_versions` in baseline suites.
3. Add adversarial tests that explicitly reject TLS 1.1 / TLS 1.0 reintroduction for Apple TLS families.
4. Add cross-family disjointness checks so Apple/WebKit iOS, Firefox iOS, and iOS Chromium cannot silently converge on the wrong lane.
5. Require at least one baseline or multi-dump test to fail on the old `{772, 771, 770, 769}` Apple vector.

**Exit Criteria:**

1. Wrong Apple/iOS `supported_versions` fails exact 1k tests.
2. Wrong Apple/iOS `supported_versions` fails multi-dump family-lane tests.
3. Cross-family misclassification becomes test-visible.

### Workstream E: Strengthen Nightly And Statistical Gates

**Files:**

1. `test/stealth/CorpusStatHelpers.h`
2. `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
3. `test/stealth/test_tls_structural_invariants_adversarial.cpp`

**Tasks:**

1. Keep lossy JA4-style summaries only as supplemental diagnostics.
2. Add nightly exact-field validation for `supported_versions` on authoritative families.
3. Preserve the existing TLS 1.3 presence check, but stop treating it as sufficient for Apple/iOS fidelity.
4. Ensure nightly Monte Carlo derives expectations from reviewed artifacts where exact data exists, not only from generator self-calibration.

**Exit Criteria:**

1. Nightly corpus runs fail on the old Apple/iOS vector defect.
2. The statistical lane is no longer able to pass purely because TLS 1.3 remains present.

### Workstream F: Generator, Runtime, And Artifact Closeout

**Files:**

1. `td/mtproto/BrowserProfile.cpp`
2. generated reviewed headers
3. plan and operational documentation under `docs/Documentation/` and `docs/Plans/`

**Tasks:**

1. Keep the runtime Apple TLS fix aligned with reviewed corpus truth.
2. Refresh generated reviewed headers after schema changes.
3. Document the new release-blocking expectations for exact reviewed fields.
4. Record a clear refresh workflow so future capture updates cannot skip the new artifact fields.

**Exit Criteria:**

1. Runtime and reviewed artifact truth agree on Apple/iOS `supported_versions`.
2. Documentation reflects the new protection model.

---

## 7. Execution Order

### Wave 0: Contracts And Red Tests

1. Add or tighten contract tests for extraction, summary generation, and exact-vector Safari/iOS assertions.
2. Ensure these tests fail against the pre-fix Apple vectors and pre-schema baseline pipeline.

### Wave 1: Artifact Schema Hardening

1. Implement Workstream A.
2. Refresh reviewed summary outputs.
3. Validate corpus smoke after any schema change.

### Wave 2: Baseline And Matcher Hardening

1. Implement Workstream B.
2. Refresh reviewed family baselines.
3. Add matcher-level failure tests.

### Wave 3: Family Classification Repair

1. Implement Workstream C.
2. Rebuild family baselines.
3. Prove core exact invariants remain populated for authoritative iOS lanes.

### Wave 4: Suite And Gate Hardening

1. Implement Workstreams D and E.
2. Move exact-vector protection into both default and nightly paths.

### Wave 5: Closeout

1. Implement Workstream F.
2. Update plan status and record final validation evidence.

---

## 8. Validation Matrix

These commands are the minimum focused checks for this hardening program.

| Slice | Command | Purpose |
|---|---|---|
| extractor contract | `python3 test/analysis/test_extract_clienthello_syn_tshark_field_fallback.py` | parser/tooling contract stability |
| imported SYN contract | `python3 test/analysis/test_imported_syn_metadata_contract.py` | reviewed fixture metadata parsing |
| corpus smoke | `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_validation.json --fixtures-root test/analysis/fixtures/clienthello --server-hello-fixtures-root test/analysis/fixtures/serverhello` | reviewed fixture pipeline integrity |
| Safari exact lane | `./build/test/run_all_tests --filter Safari26_3Invariance1k` | exact Safari/WebKit iOS vector and structural checks |
| iOS Apple exact lane | `./build/test/run_all_tests --filter IosAppleTlsCorpus1k` | exact Apple iOS vector and structural checks |
| iOS multi-dump lane | `./build/test/run_all_tests --filter TLS_MultiDumpIosAppleTls` | family-lane baseline/stat validation |
| nightly corpus lane | `env TD_NIGHTLY_CORPUS=1 ./build/test/run_all_tests --filter 1k` | full nightly corpus hardening gate |

If a family classification change lands, rerun the broad stealth slice before closeout.

---

## 9. Acceptance Criteria

This plan is complete only when all of the following are true:

1. `supported_versions` is extracted, summarized, generated, and matched as a first-class field.
2. A wrong Apple/iOS `supported_versions` vector fails in at least three independent layers:
   - exact per-family corpus tests,
   - family baseline or multi-dump tests,
   - nightly or structural release-gating lane.
3. No authoritative iOS family-lane silently collapses core exact invariants to empty without an explicit diagnostic and justification.
4. Apple/WebKit iOS, Firefox iOS, and iOS Chromium lanes are either correctly split or formally proven safe to merge.
5. The broad statistical path can no longer pass purely because TLS 1.3 remains present while the rest of the vector is wrong.
6. Reviewed artifact refresh workflow and validation commands are documented.

---

## 10. Release Blockers

The fingerprint stack remains release-blocked until:

1. Workstreams A and B are complete.
2. At least one authoritative family-lane baseline includes exact `supported_versions` and rejects the old Apple vector defect.
3. The nightly corpus lane contains an exact-field gate for authoritative Apple/iOS families.
4. Family classification no longer hollows out the iOS exact invariants that the release gate depends on.

---

## 11. Noticed But Not Touching

1. The initial merged slice in PR #18 does not authorize broader stealth or protocol refactors outside this plan.
2. This plan does not authorize unrelated Telegram-core cleanup or fixture refreshes beyond what is required for fingerprint hardening.
3. The exact final family names for the iOS split should be decided only after the reviewed corpus audit in Workstream C.
