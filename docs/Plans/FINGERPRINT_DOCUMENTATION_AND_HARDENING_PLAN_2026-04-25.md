<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Documentation and Hardening Plan

**Plan ID:** fingerprint-docs-hardening-2026-04-25  
**Date:** 2026-04-25  
**Language:** English  
**Primary Goal:** Keep fingerprint docs, release evidence policy, and hardening execution aligned with current implementation under high-budget adaptive DPI.

**Method:** TDD only. New or changed behavior must be introduced via failing tests first (contracts, adversarial, integration), then implementation.

---

## 0. Current Baseline (As Of 2026-04-25)

This section is the operational starting point for this plan.

1. Documentation has been refreshed, but the plan needs stronger execution semantics, measurable gates, and ownership.
2. Reviewed vs imported lane separation exists conceptually, but requires stricter CI enforcement and release-policy linkage.
3. Runtime still includes advisory lineage profiles; release handling must be explicit and fail-closed.
4. Transport-coherence and active-probing hardening are partially covered by tests/policies but not yet unified into mandatory release evidence.
5. Corpus smoke outcomes currently indicate non-trivial policy drift risk in reviewed ClientHello validation; this plan treats smoke-green for reviewed lane as a release precondition.

---

## 1. Objectives

1. Remove drift between documentation and current code/script interfaces.
2. Keep trust-tier and release-gate semantics consistent across all docs.
3. Preserve strict separation of reviewed release lane vs imported candidate lane.
4. Add explicit documentation coverage for known security limitations and follow-up hardening work.
5. Make reviewed-lane validation status observable and release-blocking.
6. Strengthen evidence integrity and fail-closed behavior for RU/unknown route constraints (ECH blocked scenarios).

---

## 1.1 Non-Goals

1. This plan does not claim full TCP/IP mimicry parity today.
2. This plan does not relax existing red tests to match current behavior.
3. This plan does not permit imported candidate evidence to gate production release.

---

## 2. What Was Corrected Immediately

1. Updated docs/Documentation/FINGERPRINT_DOCUMENTATION_INDEX.md to reflect current artifacts and trust-tier semantics.
2. Updated docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md to match current extraction APIs, generated artifacts, runtime profile behavior, and test topology.
3. Updated docs/Documentation/FINGERPRINT_OPERATIONS_GUIDE.md so commands are executable against current script CLIs.

---

## 3. Execution Model

### 3.1 Mandatory Gates

1. Reviewed-lane corpus smoke must be green before release tagging.
2. Tier semantics must be generated from one canonical source; manual tier text is forbidden.
3. Advisory profiles must never be implicitly treated as Tier2+ evidence.
4. Imported-lane jobs must never publish pass status into release-gating checks.

### 3.2 Evidence Integrity Rules

1. Provenance fields are required and validated (source_path, source_sha256, source_kind, route_mode, parser_version).
2. Independent corroboration must be explicit and auditable.
3. Any mismatch in family, route, or source metadata is fail-closed.

### 3.3 TDD Enforcement

1. For each workstream below: write failing tests/contracts first.
2. Never delete or weaken failing tests unless corpus/contract defect is proven and documented.
3. Each workstream completion requires tests in separate files (no inline one-off tests).

---

## 4. Remaining Documentation Fixes

### Workstream A: Canonical Trust-Tier Source of Truth

1. Add a single machine-readable tier specification file and generate tier sections in docs from it.
2. Eliminate any remaining references to outdated threshold shortcuts in other docs.
3. Add CI check that fails when tier semantics diverge across documentation files.
4. Add a generated summary artifact consumed by release checklist automation.

**Deliverables:**

1. Generated trust-tier snippets in documentation.
2. CI drift check script and workflow step.
3. Single generated tier summary file for release evidence.

**Definition of Done:**

1. No manual tier block remains in target docs.
2. CI fails on any tier drift.
3. Release checklist reads only generated tier summary.

### Workstream B: Runtime Profile Evidence Clarity

1. Document advisory runtime profiles explicitly in operator-facing docs as non-release-grade evidence.
2. Add a release checklist statement that advisory profiles cannot be counted as Tier 2+ proof.
3. Add cross-reference from runtime profile docs to contamination and provenance constraints.
4. Add explicit policy language for RU/unknown fail-closed ECH semantics.

**Deliverables:**

1. Updated operator guidance sections.
2. Release checklist additions.
3. Explicit advisory-profile matrix (allowed in dev/nightly, blocked in release mode where applicable).

**Definition of Done:**

1. Every runtime profile in docs has evidence class and trust status.
2. Advisory treatment is unambiguous and test-linked.
3. RU/unknown route policy text matches runtime tests.

### Workstream C: Reviewed vs Imported Lane Guardrails

1. Add explicit warning banners in docs that imported lane is candidate-only.
2. Add command recipes that always run reviewed and imported smoke independently.
3. Add validation note that imported registry must never gate production release.
4. Add CI naming/labeling conventions preventing accidental lane conflation.

**Deliverables:**

1. Updated docs and CI snippets for lane separation.
2. Lightweight regression checklist for lane-mixing prevention.
3. Branch protection mapping that only accepts reviewed-lane gate contexts.

**Definition of Done:**

1. Reviewed and imported jobs emit distinct check names.
2. Release workflow consumes reviewed checks only.
3. Lane-mix regression test exists and fails closed.

### Workstream D: Transport-Layer Coverage Documentation Gap

1. Document current boundary clearly: TLS corpus validation does not currently model full TCP/IP fingerprint coherence.
2. Add planned transport-coherence metrics section (TTL bucket, SYN option ordering class, MSS/window-scale buckets, first-flight segmentation signatures).
3. Link this section to DPI hardening plans so reviewers do not infer missing coverage.
4. Define minimum transport metrics required before claiming transport-aware release readiness.

**Deliverables:**

1. New section in architecture docs.
2. Traceability links to implementation plans.
3. Transport-readiness checklist section with explicit "not yet covered" flags.

**Definition of Done:**

1. No reader can confuse TLS-fidelity status with full transport-fidelity status.
2. Transport gap is visible in release evidence summary.
3. Metrics are defined with pass/fail thresholds, not prose-only guidance.

---

## 5. Engineering Follow-Up Plan (Beyond Docs)

### Workstream E: Transport-Coherence Baseline Expansion

1. Extend artifact schema with optional transport fingerprint summary fields.
2. Add extractor support for transport signatures from captures.
3. Add Tier 2 set-membership gates for transport signatures.
4. Add Tier 3 distributional gates for transport signatures when sample sizes qualify.
5. Add adversarial tests for selective packet loss/reorder patterns that could reveal non-browser behavior.

**Engineering Acceptance:**

1. Transport fields are validated as untrusted input.
2. Tier2 and Tier3 transport gates are separated by sample-size and power policy.
3. Nightly report includes transport drift deltas and confidence notes.

### Workstream F: Advisory Profile De-Risking

1. Add explicit runtime policy toggle to exclude advisory profiles from release mode.
2. Add contract tests proving advisory profiles are blocked in release-gating configurations.
3. Add migration plan for replacing advisory profiles with browser-capture-backed equivalents.
4. Add telemetry counter for advisory profile selection attempts in release mode.

**Engineering Acceptance:**

1. Release mode cannot select advisory profiles.
2. Contract tests fail on any advisory leakage into release path.
3. Migration tracking exists per advisory profile with owner and target date.

### Workstream G: Active-Probing Integration Tests

1. Add integration harness for selective drop/reorder/challenge scenarios.
2. Verify fail-closed behavior under RU/unknown and fallback transitions.
3. Add reproducible nightly job for active-probing lanes.
4. Add route-transition indistinguishability checks (no unique fallback fingerprint lane).

**Engineering Acceptance:**

1. Active-probing suite is deterministic and CI-runnable.
2. Route transitions do not leak unique probe artifacts.
3. Fail-closed behavior is validated under malformed and partial-state conditions.

---

## 6. Global Acceptance Criteria

1. No documentation command examples use non-existent CLI flags.
2. Trust-tier semantics are identical across index, pipeline, operations, and plan docs.
3. Reviewed/imported lane distinction is explicit and testable.
4. Advisory profile limitations are explicitly documented in release policy.
5. A tracked backlog exists for transport-layer and active-probing expansion.
6. Reviewed-lane smoke is green and published in release evidence.
7. Tier-generation and lane-guard checks are mandatory in CI for release branches.

---

## 7. Metrics And Reporting

### 7.1 Weekly Metrics

1. Reviewed smoke pass rate.
2. Imported smoke pass rate (informational only).
3. Number of advisory profiles still present in release-capable runtime set.
4. Number of family-lane entries at Tier0/Tier1/Tier2/Tier3/Tier4.
5. Number of unresolved drift incidents (docs vs generated artifacts).

### 7.2 Release Metrics

1. Reviewed smoke must be green.
2. No tier drift incidents open.
3. Active-probing nightly status attached.
4. Transport coverage status attached with explicit limitations.

---

## 8. Suggested Execution Order

1. Workstream A (single source of truth for tiers).
2. Workstream C (lane guardrails).
3. Workstream B (runtime evidence clarity).
4. Workstream D (transport boundary docs).
5. Workstreams E/F/G (engineering hardening execution).

Execution note:
1. A+C are hard prerequisites for trustworthy release evidence.
2. B prevents policy ambiguity during the same cycle.
3. D/E/F/G close DPI-relevant blind spots incrementally without blocking short-cycle doc correctness.

---

## 9. Risk Register

1. **Risk:** Reintroduced tier drift in future edits.  
   **Severity:** High  
   **Mitigation:** CI doc consistency checks and generated tier snippets.

2. **Risk:** Imported candidate evidence accidentally treated as release-grade.  
   **Severity:** Critical  
   **Mitigation:** explicit lane policy docs plus CI guardrails.

3. **Risk:** Overconfidence from TLS-only validation under advanced DPI.  
   **Severity:** Critical  
   **Mitigation:** document gap and execute transport/active-probing expansion plan.

4. **Risk:** Advisory profile leakage into release path.  
   **Severity:** Critical  
   **Mitigation:** runtime release toggle + contract tests + telemetry.

5. **Risk:** Reviewed smoke remains red while docs appear healthy.  
   **Severity:** High  
   **Mitigation:** make reviewed smoke status a mandatory release evidence field.

---

## 10. Milestones

1. M1: Canonical tiers + CI drift gate + lane guardrails merged.
2. M2: Runtime advisory clarity + release checklist enforcement merged.
3. M3: Transport boundary documentation + readiness checklist merged.
4. M4: Advisory de-risking and active-probing engineering slices merged.
5. M5: First release cycle with full evidence bundle (reviewed smoke, tier summary, probing status, transport status).

---

## 11. Status

1. Immediate documentation alignment: completed.
2. Plan finalization audit (2026-04-25): completed.
3. Reviewed-lane corpus smoke: **green as of 2026-04-25** (reviewed lane 0 failures). Imported lane smoke: **green as of 2026-04-25** (0 failures after serverhello corpus regeneration).
4. Release-readiness verdict: blocked until Workstream E acceptance criteria are implemented (transport-coherence gates). Workstreams F and G are complete.
5. Next required checkpoint: complete mandatory blocker item 4 in Section 12.3 before next release branch cut.

---

## 12. Finalization Snapshot (Audit-Locked)

This section finalizes plan state using repository evidence and executed test gates.

### 12.1 Gate Execution Snapshot (2026-04-25)

1. `python3 -m unittest discover -s test/analysis -p 'test_fingerprint_policy_generation*.py'` -> passed.
2. `python3 -m unittest discover -s test/analysis -p 'test_fingerprint_policy_ci_contract.py'` -> passed.
3. `python3 -m unittest discover -s test/analysis -p 'test_build_transport_and_active_probing_status_contract.py'` -> passed.
4. `python3 test/analysis/build_transport_coherence_status.py --repo-root . --now-utc 2026-04-25T00:00:00Z` -> generated from `test/analysis/transport_coherence_observations.json`; status = **fail** (Tier2/Tier3 thresholds not met).
5. `python3 test/analysis/build_active_probing_status.py --repo-root . --now-utc 2026-04-25T00:00:00Z` -> generated from `test/analysis/active_probing_nightly_observations.json`; status = **pass**.
6. `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_validation.json --fixtures-root test/analysis/fixtures/clienthello --server-hello-fixtures-root test/analysis/fixtures/serverhello` -> **passed** (0 failures; reviewed lane green as of 2026-04-25).
7. `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_imported.json --fixtures-root test/analysis/fixtures/imported/clienthello --server-hello-fixtures-root test/analysis/fixtures/imported/serverhello` -> **passed** (0 failures; informational lane).
8. `./build/test/run_all_tests --filter TlsHmacReplayAdversarial` -> passed (11/11).
9. `./build/test/run_all_tests --filter RouteEchQuic` -> passed (7/7).
10. `./build/test/run_all_tests --filter TlsRuntimeActivePolicy` -> passed (3/3).
11. `./build/test/run_all_tests --filter FirstFlightLayoutPairing` -> passed (2/2).

### 12.2 Workstream Closure Status

1. Workstream A (canonical trust-tier source + generated artifacts): completed.
2. Workstream B (runtime profile evidence clarity in docs/policy): completed — advisory profile ops guidance, release checklist advisory exclusion, and RU/unknown ECH policy text are all in FINGERPRINT_OPERATIONS_GUIDE.md and docs/Generated/FINGERPRINT_RELEASE_EVIDENCE_POLICY.generated.json.
3. Workstream C (reviewed/imported lane guardrails in CI/docs): **completed, reviewed lane green** (as of 2026-04-25); refresh_reviewed_profiles.py maintenance tool added; imported serverhello corpus regenerated with full metadata.
4. Workstream D (transport-boundary documentation): completed for documentation scope.
5. Workstream E (transport-coherence engineering gates/extractor/schema): **partially completed** — evidence-backed status generation now reads measured observations from `test/analysis/transport_coherence_observations.json`; current extractor is fail-closed for unavailable SYN-phase transport metadata in imported fixtures and enforces Tier2/Tier3 threshold evaluation. The current measured lane remains **fail** and therefore release-blocking.
6. Workstream F (release-mode advisory exclusion + telemetry + migration tracking): completed — advisory exclusion runtime, contract tests, telemetry, and migration tracking docs are in place.
7. Workstream G (active-probing integration harness + nightly lane): completed — active-probing status is evidence-backed from compiled stealth integration/adversarial slices and reproducible CI nightly automation now refreshes and uploads the evidence bundle.

### 12.3 Mandatory Blockers To Unblock Release

1. [completed] Make reviewed-lane corpus smoke green and keep it green as a release precondition. (Done 2026-04-25; root cause: profile policy fields were not refreshed when new fixture samples were added; refresh_reviewed_profiles.py implemented and applied.)
2. [completed] Implement explicit release-mode runtime toggle that excludes advisory profiles from selection.
3. [completed] Add advisory-selection telemetry for release mode and enforce contract tests on the exclusion boundary.
4. [in-progress] Keep transport-coherence lane blocked until measured metrics satisfy Tier2/Tier3 thresholds under evidence-backed extraction. (Workstream E)
5. [completed] Add reproducible CI nightly automation for active-probing lane refresh; evidence attachment is implemented. (Workstream G)

### 12.4 Final Decision

1. Plan document is finalized.
2. Engineering execution is not fully complete.
3. Release branch cut remains blocked until Section 12.3 items are complete and verified by tests.

---

**Owner:** telemt community  
**Review cadence:** weekly until all documentation workstreams are complete
