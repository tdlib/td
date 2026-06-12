<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Generation Pipeline: Current Architecture

**Document Version:** 1.2  
**Date:** 2026-04-26  
**Scope:** Current implementation of TLS ClientHello/ServerHello corpus ingestion, runtime profile mapping, and statistical validation

---

## Table of Contents

1. Overview
2. Scope Boundaries
3. Pipeline Stages
4. Reviewed vs Imported Candidate Lanes
5. Runtime Profile Mapping
6. Validation and Test Topology
7. Trust Tiers and Release Gates
8. Practical Workflows

---

## Overview

This subsystem builds and validates TLS fingerprint behavior from real captures. The core chain is:

1. Capture data in PCAP/PCAPng.
2. Extract normalized TLS artifacts.
3. Validate and classify artifacts under fail-closed registry constraints.
4. Generate runtime ClientHello behavior from platform-aware profile selection.
5. Verify with deterministic, adversarial, and statistical suites.

Key properties:

1. Provenance-first: reviewed runtime evidence must be traceable to captured traffic.
2. Platform isolation: Linux/macOS/Windows/iOS/Android lanes are separated by policy.
3. Route-aware ECH policy: RU and unknown lanes are fail-closed for ECH.
4. Dual evidence model: reviewed release lane and imported candidate lane are explicitly separate.
5. Transport-coherence and active-probing release status artifacts are generated from measured observations.
6. Imported fixture generation is capture-driven: browser behavior is taken from real traffic dumps under `docs/Samples/Traffic dumps/**`, not from hand-written approximations.

---

## Scope Boundaries

This document covers TLS handshake and corpus logic only.

In scope:

1. ClientHello and ServerHello fixture extraction and validation.
2. Runtime BrowserProfile selection and ECH route policy behavior.
3. Corpus and multi-dump statistical validation gates.

Out of scope for this document set:

1. TCP/IP stack mimicry (TTL, MSS, SYN option ordering, IPID behavior).
2. Non-TLS packet-level transport fingerprint controls.
3. QUIC implementation internals beyond route-lane policy interaction.

---

## Pipeline Stages

### Stage 1: Capture Intake

Primary source roots:

1. docs/Samples/Traffic dumps/Android
2. docs/Samples/Traffic dumps/iOS
3. docs/Samples/Traffic dumps/Linux, desktop
4. docs/Samples/Traffic dumps/macOS
5. docs/Samples/Traffic dumps/Windows

Capture files are treated as untrusted inputs and validated through parser checks and provenance metadata in the extraction phase.

Imported-manifest fixture paths are also treated as untrusted input; fixture resolution is constrained to repository-root containment to fail closed on path traversal.

### Stage 2: Artifact Extraction

The staged schema changes in this lane are operationally significant because later validation and transport-coherence code consumes them directly.

ClientHello extractor (single-capture API):

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/Linux, desktop/sample.pcapng" \
  --profile-id chrome147_linux_desktop \
  --source-kind browser_capture \
  --capture-date-utc 2026-04-25T00:00:00Z \
  --scenario-id linux_desktop_chrome147_clienthello \
  --device-class desktop \
  --os-family linux \
  --route-mode non_ru_egress \
  --out /tmp/chrome147_linux_desktop.clienthello.json
```

ServerHello extractor (single-capture API):

```bash
python3 test/analysis/extract_server_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/Linux, desktop/sample.pcapng" \
  --route-mode non_ru_egress \
  --scenario linux_desktop_chrome147_clienthello \
  --family chrome147_linux_desktop \
  --client-profile-id chrome147_linux_desktop \
  --out /tmp/chrome147_linux_desktop.serverhello.json
```

ClientHello sample shape now preserves both aggregate and segmented first-flight record metadata:

```json
{
  "record_length": 1914,
  "record_count": 1,
  "record_lengths": [1914]
}
```

Operational meaning:

1. `record_length` remains the total TLS record payload length consumed by the parsed ClientHello message.
2. `record_lengths` preserves the exact per-record sequence when the ClientHello spans multiple TLS records.
3. `record_count` is derived from `record_lengths` and gives a cheap contract check for fragmented first flights.
4. Transport-coherence extraction now prefers `record_lengths` and only falls back to the legacy single `record_length` field when needed for backward compatibility.

ServerHello artifact shape now carries explicit client/server pairing metadata:

```json
{
  "artifact_type": "tls_serverhello_fixtures",
  "capture_provenance": {
    "client_profile_id": "chrome147_linux_desktop",
    "path_layout_note": "ServerHello fixture path mirrors client capture provenance; validation keys on fixture_family_id and source metadata, not OS directory naming."
  },
  "observed_server_endpoints": [
    {"ip": "192.144.14.155", "port": 443},
    {"ip": "192.144.14.155", "port": 8443}
  ],
  "samples": [
    {
      "fixture_id": "chrome147_linux_desktop_serverhello:frame7",
      "server_endpoint": {"ip": "192.144.14.155", "port": 443}
    }
  ]
}
```

Operational meaning:

1. `capture_provenance.client_profile_id` explicitly binds the ServerHello artifact to the ClientHello capture family that produced it.
2. `observed_server_endpoints` is the deduplicated endpoint set across the artifact.
3. `samples[].server_endpoint` preserves the specific source IP and port observed for that handshake frame.
4. The `path_layout_note` exists because validation keys on family/provenance metadata, not on directory names alone.

Batch ServerHello regeneration for reviewed corpus:

```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello
```

### Stage 3: Registry and Policy Materialization

Reviewed lane registry:

1. test/analysis/profiles_validation.json

Imported candidate lane registry (generated):

1. test/analysis/profiles_imported.json

Imported registry generation:

```bash
python3 test/analysis/generate_imported_fixture_registry.py \
  --clienthello-root test/analysis/fixtures/imported/clienthello \
  --serverhello-root test/analysis/fixtures/imported/serverhello \
  --manifest test/analysis/fixtures/imported/import_manifest.json \
  --out test/analysis/profiles_imported.json \
  --route-mode non_ru_egress
```

Reviewed generated artifacts:

1. test/stealth/ReviewedClientHelloFixtures.h
   Generated by merge_client_hello_fixture_summary.py from reviewed JSON fixtures.
2. test/stealth/ReviewedFamilyLaneBaselines.h
   Generated by build_family_lane_baselines.py.
3. test/stealth/ReviewedFingerprintStatBaselines.h
   Generated by build_fingerprint_stat_baselines.py as a forwarder to ReviewedFamilyLaneBaselines.h.

Release-status artifact builders:

1. test/analysis/build_transport_coherence_status.py -> docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json
2. test/analysis/build_active_probing_status.py -> docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json

Observation sources:

1. test/analysis/transport_coherence_observations.json
2. test/analysis/active_probing_nightly_observations.json

### Stage 4: Runtime Fingerprint Mapping

Runtime profile registry is implemented in td/mtproto/stealth/TlsHelloProfileRegistry.cpp.

Current notable properties:

1. Windows desktop profiles are present in runtime mapping.
2. Platform gating is enforced in allowed_profiles_for_platform(...).
3. ECH route policy is fail-closed for RU and unknown lanes.
4. Advisory profiles still exist and are explicitly marked advisory in fixture metadata.
5. Release-mode profile gating excludes advisory profiles from release-gating selection.
6. Blocked advisory release selections are tracked by advisory_blocked_total telemetry.
7. Release fallback is constrained to the runtime platform-allowed profile set.

### Stage 5: Validation

The current validation surface is layered:

1. Reviewed corpus smoke and registry checks in test/analysis scripts.
2. Loader fail-closed checks in `test/analysis/common_tls.py` require `capture_provenance.client_profile_id` for ServerHello artifacts and reject malformed or duplicate samples.
2. Legacy 1k corpus suites in test/stealth/test_tls_corpus_*_1k.cpp.
3. Iteration tiering constants in test/stealth/CorpusIterationTiers.h:
   - kQuickIterations = 3
   - kSpotIterations = 64
   - kFullIterations = 1024
   - TD_NIGHTLY_CORPUS toggles spot/full behavior
4. Multi-dump family-lane suites:
   - test_tls_multi_dump_linux_*
   - test_tls_multi_dump_ios_*
   - test_tls_multi_dump_macos_*
   - test_tls_multi_dump_windows_*
5. Route/ECH/QUIC lane matrix assertions:
   - test_tls_route_ech_quic_block_matrix.cpp
6. Tier-3 style distinguishability checks:
   - test_tls_wire_pattern_distinguisher_contract.cpp
7. Runtime release-gating contract assertions:
  - test_tls_runtime_release_profile_gating_contract.cpp
8. Adversarial transport extraction assertions:
  - test_tcp_transport_extraction_adversarial.py
9. ClientHello/ServerHello pairing fail-closed assertions:
  - test_clienthello_serverhello_pairing_fail_closed.py
10. ServerHello provenance and endpoint contracts:
  - test_common_tls_fail_closed_contract.py
  - test_check_server_hello_matrix.py

---

## Reviewed vs Imported Candidate Lanes

### Reviewed lane

Purpose:

1. Release-facing evidence and runtime regression guarantees.

Inputs:

1. test/analysis/fixtures/clienthello/**
2. test/analysis/fixtures/serverhello/**
3. test/analysis/profiles_validation.json

Outputs:

1. ReviewedClientHelloFixtures.h
2. ReviewedFamilyLaneBaselines.h
3. ReviewedFingerprintStatBaselines.h

### Imported candidate lane

Purpose:

1. Fast intake/testing of new captures without widening release truth.

Inputs:

1. test/analysis/fixtures/imported/**
2. test/analysis/fixtures/imported/import_manifest.json
3. docs/Samples/Traffic dumps/**

Outputs:

1. test/analysis/profiles_imported.json

Policy note:

1. Imported lane is non-release and must not be treated as reviewed release evidence.
2. Imported lane is still capture-backed evidence. If a browser, ECH, or route claim is not represented in the raw dump set, the correct action is to obtain more captures, not to hand-edit fixture JSON to match expectations.

---

## Runtime Profile Mapping

### Platform-aware selection

Runtime chooses from platform-allowed profile subsets only. Windows desktop, Darwin desktop, Linux desktop, iOS mobile, and Android mobile are separated in allowed profile lists.

In release-mode profile gating, advisory profiles are excluded from release-gating selection. If release-gating weighted candidates are unavailable, fallback remains within the current runtime platform-allowed set.

### Route-aware ECH behavior

ECH mode decision is route-aware and fail-closed:

1. Unknown route -> route-disabled behavior.
2. RU route -> route-disabled behavior.
3. Non-RU route -> policy plus circuit-breaker state.

### Advisory profile caveat

Advisory profile entries remain present in runtime profile metadata. They are explicitly tagged advisory and should not be misrepresented as equivalent to reviewed multi-source release-grade evidence.

Release-mode suppression of advisory selections is observable via advisory_blocked_total telemetry.

---

## Validation and Test Topology

### Script-level gates (analysis)

1. check_fixture_registry_complete.py
2. run_corpus_smoke.py
3. generate_server_hello_fixture_corpus.py

### C++ test lanes (stealth)

1. Legacy corpus depth: test_tls_corpus_*_1k.cpp
2. Multi-dump breadth: test_tls_multi_dump_*_{baseline,stats}.cpp
3. Route matrix hardening: test_tls_route_ech_quic_block_matrix.cpp
4. Classifier gate shape: test_tls_wire_pattern_distinguisher_contract.cpp

### Real-Corpus Similarity Gates vs Seed-Stress Diagnostics

A real-corpus similarity gate compares generated TLS ClientHello fields against reviewed browser-capture evidence for the same `(family_id, cohort_id, route_lane, evidence_lane)`. These gates consume `test/analysis/fixtures/clienthello/` through generated reviewed baselines and fail closed when exact release-critical evidence is unavailable or mixed. Examples: `TlsReleaseSimilarityUnavailableFailClosed`, `TlsGeneratorFixtureExactFieldsGate`, `TlsGeneratorExtensionCountSimilarity`, `TlsGeneratorWireLengthFixtureGate`, `TlsGeneratorShuffleSimilarity`.

A seed-stress diagnostic exercises runtime variability across deterministic seeds. Seed-stress diagnostics are valuable for detecting degenerate RNG behavior, duplicate wire images, weak GREASE diversity, and pinned shuffle positions, but generated seeds are not independent browser evidence and may not be used as release-facing denominators. Example: `TLS_NightlyWireBaselineMonteCarlo`.

As a rule, self-calibrated generator tests are not real-browser similarity evidence. A test that derives expected wire lengths, extension counts, or envelopes from the generator under test can only prove internal stability. Note that some fixture-derived gates (for example wire length) still admit the builder's documented padding-target entropy as an explicit tolerance; this is bounded by the reviewed catalog, not self-calibrated from the generator.

---

## Trust Tiers and Release Gates

Trust-tier semantics are aligned to docs/Plans/FINGERPRINT_CORPUS_STATISTICAL_VALIDATION_PLAN_2026-04-11.md.

<!-- BEGIN GENERATED TRUST TIER BLOCK -->
Canonical source: test/analysis/fingerprint_trust_tiers.json
Do not edit this block manually; regenerate via render_fingerprint_policy_artifacts.py.

- Tier0 (Advisory-only): captures >= 0, independent sources >= 0, independent sessions >= 0, release_gating=false. No authoritative network-derived evidence; advisory diagnostics only.
- Tier1 (Anchored): captures >= 1, independent sources >= 1, independent sessions >= 1, release_gating=true. Initial authoritative anchoring with structural gates.
- Tier2 (Corroborated): captures >= 3, independent sources >= 2, independent sessions >= 2, release_gating=true. Corroborated release evidence with structural and set-membership gates.
- Tier3 (Distributional): captures >= 15, independent sources >= 3, independent sessions >= 2, release_gating=true. Distributional and classifier-style evidence enabled when sample power qualifies.
- Tier4 (High-confidence): captures >= 200, independent sources >= 3, independent sessions >= 2, release_gating=true. High-confidence long-horizon equivalence tier.
<!-- END GENERATED TRUST TIER BLOCK -->

Do not use historical 10+/50+ shortcuts from older drafts in release decisions.

Release evidence policy summary is generated at docs/Generated/FINGERPRINT_RELEASE_EVIDENCE_POLICY.generated.json.

---

## Practical Workflows

### Refresh reviewed ServerHello corpus and run smoke

```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello

python3 test/analysis/extract_tcp_transport_signatures.py \
  --repo-root . \
  --output test/analysis/transport_coherence_observations.json

python3 test/analysis/build_transport_coherence_status.py \
  --repo-root . \
  --now-utc 2026-04-26T00:00:00Z

python3 test/analysis/build_active_probing_status.py \
  --repo-root . \
  --now-utc 2026-04-26T00:00:00Z
```

### Regenerate reviewed headers

```bash
python3 test/analysis/merge_client_hello_fixture_summary.py \
  --input-dir test/analysis/fixtures/clienthello \
  --out-header test/stealth/ReviewedClientHelloFixtures.h

python3 test/analysis/build_family_lane_baselines.py \
  --input-dir test/analysis/fixtures/clienthello \
  --output test/stealth/ReviewedFamilyLaneBaselines.h

python3 test/analysis/build_fingerprint_stat_baselines.py \
  --input test/analysis/fixtures/clienthello \
  --output-header test/stealth/ReviewedFingerprintStatBaselines.h \
  --output-json test/analysis/fingerprint_stat_baselines.json
```

### Build and run core corpus slices

```bash
cmake --build build --target run_all_tests --parallel 14
./build/test/run_all_tests --filter "TlsProfile|Corpus|TlsMultiDump|TlsRouteEchQuic"
```

Nightly full corpus mode:

```bash
export TD_NIGHTLY_CORPUS=1
./build/test/run_all_tests --filter "Corpus|TlsMultiDump|TlsWirePattern"
```

---

## References

1. docs/Documentation/FINGERPRINT_OPERATIONS_GUIDE.md
2. docs/Plans/FINGERPRINT_CORPUS_STATISTICAL_VALIDATION_PLAN_2026-04-11.md
3. test/analysis/README.md
4. td/mtproto/stealth/AGENTS.md

---

**Document Status:** Updated for current implementation  
**Last Updated:** 2026-04-26  
**Maintainer:** telemt community
