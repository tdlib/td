<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fingerprint Pipeline Operations Guide

**Document Version:** 1.2  
**Date:** 2026-04-26  
**Scope:** Real operational commands and workflows for reviewed and imported TLS fixture lanes

Operational rule: when browser behavior matters, refresh from real captures under `docs/Samples/Traffic dumps/**`. Do not hand-author imported fixture JSON to imitate ECH, ALPN, record sizing, or endpoint behavior.

---

## Quick Start (Reviewed Lane)

From repository root:

```bash
cd /home/david_osipov/tdlib-obf

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

cmake --build build --target run_all_tests --parallel 14
./build/test/run_all_tests --filter "TlsProfile|Corpus|TlsMultiDump"
```

---

## Quick Start (Imported Candidate Lane)

Use this when onboarding new raw captures without widening reviewed release truth.

Expected flow:

1. Drop or refresh raw captures under the platform-specific subdirectories in `docs/Samples/Traffic dumps/**`.
2. Run `import_traffic_dumps.py` to normalize those captures into imported ClientHello and ServerHello fixtures plus `import_manifest.json`.
3. Regenerate the imported registry and run smoke against the imported lane.

```bash
python3 test/analysis/import_traffic_dumps.py

python3 test/analysis/generate_imported_fixture_registry.py \
  --clienthello-root test/analysis/fixtures/imported/clienthello \
  --serverhello-root test/analysis/fixtures/imported/serverhello \
  --manifest test/analysis/fixtures/imported/import_manifest.json \
  --out test/analysis/profiles_imported.json \
  --route-mode non_ru_egress

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_imported.json \
  --fixtures-root test/analysis/fixtures/imported/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/imported/serverhello
```

Schema expectations after import:

1. Imported ClientHello samples should carry `record_count` and `record_lengths` in addition to legacy `record_length`.
2. Imported ServerHello artifacts should carry `capture_provenance.client_profile_id`, `observed_server_endpoints`, and per-sample `server_endpoint`.
3. If those fields are missing, treat the import as stale or incomplete and regenerate instead of patching the JSON by hand.

---

## Command Reference

### extract_client_hello_fixtures.py

Single-capture extractor with strict metadata fields.

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap <path/to/input.pcap_or_pcapng> \
  --profile-id <family_id> \
  --source-kind browser_capture \
  --capture-date-utc <RFC3339_Z> \
  --scenario-id <scenario_id> \
  --device-class <desktop|mobile|tablet|unknown> \
  --os-family <linux|macos|windows|ios|android|unknown> \
  --route-mode <non_ru_egress|ru_egress|unknown> \
  --out <output.clienthello.json>
```

### extract_server_hello_fixtures.py

Single-capture ServerHello tuple extractor.

```bash
python3 test/analysis/extract_server_hello_fixtures.py \
  --pcap <path/to/input.pcap_or_pcapng> \
  --route-mode <non_ru_egress|ru_egress|unknown> \
  --scenario <scenario_id> \
  --family <family_id> \
  --client-profile-id <paired_client_profile_id> \
  --out <output.serverhello.json>
```

The `--client-profile-id` argument is not cosmetic. Reviewed and imported loaders now fail closed if `capture_provenance.client_profile_id` is absent or blank.

### generate_server_hello_fixture_corpus.py

Batch refresh of reviewed ServerHello corpus from reviewed ClientHello tree.

```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello
```

This generator now forwards each ClientHello artifact's `profile_id` into the ServerHello extractor so the output corpus preserves explicit client/server provenance.

### extract_tcp_transport_signatures.py

Builds transport-coherence observations from imported fixtures. SYN-phase metrics fail-closed to 0.0 when SYN evidence is unavailable.

```bash
python3 test/analysis/extract_tcp_transport_signatures.py \
  --repo-root . \
  --now-utc 2026-04-26T00:00:00Z \
  --output test/analysis/transport_coherence_observations.json
```

### build_transport_coherence_status.py

Materializes the release-facing transport-coherence status artifact from observations.

```bash
python3 test/analysis/build_transport_coherence_status.py \
  --repo-root . \
  --now-utc 2026-04-26T00:00:00Z
```

### build_active_probing_status.py

Materializes the release-facing active-probing nightly status artifact from observations.

```bash
python3 test/analysis/build_active_probing_status.py \
  --repo-root . \
  --now-utc 2026-04-26T00:00:00Z
```

### refresh_reviewed_profiles.py

Re-derives policy fields (`alps_type`, `pq_group`, `ech_type`, `extension_order_policy`,
`include_fixture_ids`) in `profiles_validation.json` from the actual reviewed clienthello
artifact files. Run this after adding new fixtures to the reviewed corpus to keep policy
fields in sync with observed sample data.

Preserved unchanged: `trust_tier`, `release_gating`, `allowed_tags`, `fingerprint_policy`,
`contamination_guard`, `server_hello_matrix`, and all existing fixture metadata.

```bash
python3 test/analysis/refresh_reviewed_profiles.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello
```

`--dry-run` flag is available to preview changes without writing.

### Symptom: reviewed smoke fails ALPS/extension-order policy or missing-fixture-ids after new captures

Likely cause:

1. New clienthello artifact frames were added to the reviewed corpus but `profiles_validation.json`
   profile policy fields (`include_fixture_ids`, `alps_type`, `extension_order_policy`, `pq_group`)
   were not regenerated to match the new samples.

Fix:

```bash
python3 test/analysis/refresh_reviewed_profiles.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello
```


```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello
```

### run_corpus_smoke.py

Corpus-level fail-closed smoke gate.

```bash
python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello \
  --report-out artifacts/corpus_smoke_report.json
```

### check_fixture_registry_complete.py

Registry completeness and contamination checks.

```bash
python3 test/analysis/check_fixture_registry_complete.py \
  --registry test/analysis/profiles_validation.json \
  --report-out artifacts/fixture_registry_report.json
```

### merge_client_hello_fixture_summary.py

Generates reviewed summary header consumed by PR-2 style tests.

```bash
python3 test/analysis/merge_client_hello_fixture_summary.py \
  --input-dir test/analysis/fixtures/clienthello \
  --out-header test/stealth/ReviewedClientHelloFixtures.h
```

### build_family_lane_baselines.py

Generates the authoritative multi-dump family-lane baseline header.

```bash
python3 test/analysis/build_family_lane_baselines.py \
  --input-dir test/analysis/fixtures/clienthello \
  --output test/stealth/ReviewedFamilyLaneBaselines.h
```

### build_fingerprint_stat_baselines.py

Plan-compatible wrapper output (forward header plus optional JSON).

```bash
python3 test/analysis/build_fingerprint_stat_baselines.py \
  --input test/analysis/fixtures/clienthello \
  --output-header test/stealth/ReviewedFingerprintStatBaselines.h \
  --output-json test/analysis/fingerprint_stat_baselines.json
```

---

## Test Workflows

### Fast PR lane

```bash
cmake --build build --target run_all_tests --parallel 14
./build/test/run_all_tests --filter "TlsProfile|TlsMultiDump|TlsRouteEchQuic"
```

### Corpus and multi-dump lane

```bash
./build/test/run_all_tests --filter "Corpus|TlsMultiDump|TlsWirePattern"
```

### Nightly mode (full statistical iterations)

```bash
export TD_NIGHTLY_CORPUS=1
./build/test/run_all_tests --filter "Corpus|TlsMultiDump|TlsWirePattern"
```

Iteration behavior is controlled by test/stealth/CorpusIterationTiers.h:

1. Quick: 3
2. Spot: 64
3. Full: 1024

---

## Debugging Guide

### Symptom: imported ServerHello loader rejects artifact with missing provenance

Likely cause:

1. The ServerHello artifact was generated before `capture_provenance.client_profile_id` became mandatory.
2. A manual JSON edit dropped provenance fields.

Fix:

```bash
python3 test/analysis/import_traffic_dumps.py

python3 test/analysis/generate_imported_fixture_registry.py \
  --clienthello-root test/analysis/fixtures/imported/clienthello \
  --serverhello-root test/analysis/fixtures/imported/serverhello \
  --manifest test/analysis/fixtures/imported/import_manifest.json \
  --out test/analysis/profiles_imported.json \
  --route-mode non_ru_egress
```

Do not patch `capture_provenance` by hand unless you are repairing a broken generator under test.

### Symptom: transport-coherence extraction ignores fragmented first flights

Likely cause:

1. ClientHello fixtures were generated before `record_lengths` and `record_count` were added.
2. A custom artifact still carries only an incorrect aggregate `record_length`.

Fix:

1. Re-extract the ClientHello fixtures from the original PCAP/PCAPng captures.
2. Confirm the sample contains both `record_count` and `record_lengths`.
3. Re-run `extract_tcp_transport_signatures.py` and the downstream status builders.

### Symptom: imported lane fails but reviewed lane passes

Likely cause:

1. Imported candidate artifacts and imported registry are out of sync.

Fix:

```bash
python3 test/analysis/generate_imported_fixture_registry.py
python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_imported.json \
  --fixtures-root test/analysis/fixtures/imported/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/imported/serverhello
```

### Symptom: route-mode mismatches in smoke report

Likely cause:

1. Mixed route metadata in artifacts.

Fix:

1. Re-extract with canonical --route-mode.
2. Regenerate imported registry with explicit --route-mode non_ru_egress for imported lane.

### Symptom: extractor rejects imported manifest path with "must stay inside repo root"

Likely cause:

1. Imported manifest contains a traversal path (for example ../) or an absolute path outside repository root.

Fix:

1. Update test/analysis/fixtures/imported/import_manifest.json so each artifacts.clienthello path is repository-relative and resolves under this repository.
2. Re-run imported registry generation and imported-lane smoke.

### Symptom: serverhello matrix mismatch

Likely cause:

1. ServerHello corpus stale after client fixture updates.

Fix:

```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello
```

### Symptom: tests pass locally, fail in nightly

Likely cause:

1. TD_NIGHTLY_CORPUS exposes full-iteration statistical drift.

Fix:

1. Re-run locally with TD_NIGHTLY_CORPUS=1.
2. Check corpus-specific suites under test/stealth/test_tls_corpus_*_1k.cpp.
3. Verify multi-dump baselines are regenerated.

---

## Maintenance Cadence

### Weekly

1. Run `run_corpus_smoke.py` for reviewed lane.
2. If new reviewed clienthello fixtures were added: run `refresh_reviewed_profiles.py` to sync policy fields, then regenerate the ServerHello corpus.
3. If imported captures were updated: re-run `import_traffic_dumps.py` to regenerate imported fixtures and re-run imported lane smoke (informational only).
4. Rebuild transport-coherence observations and regenerate transport/active-probing status artifacts.

### Monthly

1. Regenerate ReviewedClientHelloFixtures.h.
2. Regenerate ReviewedFamilyLaneBaselines.h and ReviewedFingerprintStatBaselines.h.
3. Rebuild and run core corpus slices.

### Before release

1. Confirm reviewed lane smoke passes.
2. Confirm generated tier semantics match documentation and policy summary.
3. Confirm advisory profiles are never counted as Tier2+ release evidence (Tier0/release_gating=false).
4. Confirm imported lane remains non-release informational only.
5. Run nightly corpus mode with TD_NIGHTLY_CORPUS=1.
6. Confirm advisory-profile runtime exclusion telemetry (`advisory_blocked_total`) is enabled in release-mode builds.
7. Confirm docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json is regenerated and included in release evidence.
8. Confirm docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json is regenerated and included in release evidence.

<!-- BEGIN GENERATED TRUST TIER BLOCK -->
Canonical source: test/analysis/fingerprint_trust_tiers.json
Do not edit this block manually; regenerate via render_fingerprint_policy_artifacts.py.

- Tier0 (Advisory-only): captures >= 0, independent sources >= 0, independent sessions >= 0, release_gating=false. No authoritative network-derived evidence; advisory diagnostics only.
- Tier1 (Anchored): captures >= 1, independent sources >= 1, independent sessions >= 1, release_gating=true. Initial authoritative anchoring with structural gates.
- Tier2 (Corroborated): captures >= 3, independent sources >= 2, independent sessions >= 2, release_gating=true. Corroborated release evidence with structural and set-membership gates.
- Tier3 (Distributional): captures >= 15, independent sources >= 3, independent sessions >= 2, release_gating=true. Distributional and classifier-style evidence enabled when sample power qualifies.
- Tier4 (High-confidence): captures >= 200, independent sources >= 3, independent sessions >= 2, release_gating=true. High-confidence long-horizon equivalence tier.
<!-- END GENERATED TRUST TIER BLOCK -->

Release checklist automation consumes docs/Generated/FINGERPRINT_RELEASE_EVIDENCE_POLICY.generated.json.

---

## Advisory Profile Runtime Constraints

Advisory profiles are Tier0 profiles with `release_gating: false`. They are used in development
and diagnostic builds but are **excluded from runtime selection in release-mode builds**.

### Evidence classification

- Tier0 (Advisory-only): no authoritative network-derived evidence. Cannot be counted as Tier2+
  release evidence regardless of how many sessions have been observed.
- Advisory profiles must never appear in release evidence documentation as proof of browser fidelity.

### Release-mode exclusion

When `release_mode_profile_gating: true` is set in `StealthRuntimeParams`, the profile registry
selector (`pick_runtime_profile`) automatically excludes all advisory (Tier0) profiles and
increments the `advisory_blocked_total` telemetry counter on each blocked selection attempt.

Contract tests that enforce this boundary:
- `test/stealth/test_tls_runtime_release_profile_gating_contract.cpp` — pins that release mode
  cannot select Tier0/advisory profiles and that `advisory_blocked_total` increments correctly.

### RU/unknown route ECH fail-closed semantics

Profiles delivered to RU and unknown routes are subject to additional fail-closed constraints
that are independent of their trust tier:

- ECH (0xFE0D) is blocked on RU and unknown routes regardless of profile tier.
- QUIC is blocked on all route-policy entries.
- These constraints are enforced at the `StealthRuntimeParams` validation layer and cannot be
  overridden by per-profile configuration.

Contract tests:
- `test/stealth/test_tls_route_ech_quic_block_matrix.cpp` — pins ECH/QUIC semantics across
  all route lanes and confirms fail-closed behavior for unknown routes.

### Cross-reference

- Contamination constraints: `profiles_validation.json` → `contamination_guard` block.
- Provenance requirements: every fixture must have `source_kind: browser_capture` and a
  non-null `source_sha256` traceable to the original pcap capture.
- Advisory profile migration plan: tracked per workstream F in
  `docs/Plans/FINGERPRINT_HARDENING_MASTER_PLAN_2026-05-24.md`.

### Advisory profile migration tracking

The remaining Tier0/advisory runtime profiles are tracked explicitly until each is replaced by
browser-capture-backed release-grade evidence.

| Advisory profile | Current role | Owner | Target date | Replacement target | Exit criteria |
|---|---|---|---|---|---|
| Safari26_3 | Darwin desktop Apple TLS fallback in non-release mode | telemt community | 2026-06-15 | browser_capture:safari26_macos_reviewed | At least Tier1 browser-capture-backed Darwin Safari family present, release-mode exclusion retained, and migration note removed from this table. |
| IOS14 | iOS Apple TLS fallback in non-release mode | telemt community | 2026-06-15 | browser_capture:ios_apple_tls_reviewed | At least Tier1 browser-capture-backed iOS Apple TLS family present, release-mode exclusion retained, and migration note removed from this table. |
| Android11_OkHttp_Advisory | Android mobile advisory fallback in non-release mode | telemt community | 2026-07-01 | browser_capture:android_okhttp_reviewed | At least Tier1 browser-capture-backed Android OkHttp family present, release-mode exclusion retained, and migration note removed from this table. |

The table above is the authoritative owner/target-date tracker referenced by Workstream F.

---

## CI Notes

Recommended CI split:

1. PR lane: quick smoke + quick/spot C++ suites.
2. Nightly lane: `active_probing_nightly_refresh` scheduled job refreshes active probing observations and uploads the generated evidence artifact.
3. Imported lane CI is optional, non-release, and must remain separate from reviewed release gates.
4. Reviewed smoke check name must stay distinct from imported smoke check name.
5. RU and unknown route lanes remain fail-closed for ECH, and RU->non-RU QUIC is treated as blocked in release policy.

### Active probing nightly refresh

The scheduled CI wrapper executes the same stealth slices used by the release gate, refreshes
the active probing observation JSON from real test output, regenerates the status artifact, and
uploads both as evidence:

```bash
python3 test/analysis/refresh_active_probing_nightly_observations.py \
  --run-all-tests-path ./build/test/run_all_tests \
  --output-path artifacts/active_probing_nightly_observations.json \
  --generated-at-utc <RFC3339_Z>

python3 test/analysis/build_active_probing_status.py \
  --repo-root . \
  --now-utc <RFC3339_Z> \
  --observations-path artifacts/active_probing_nightly_observations.json
```

---

## Related References

1. docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md
2. docs/Plans/FINGERPRINT_CORPUS_STATISTICAL_VALIDATION_PLAN_2026-04-11.md
3. test/analysis/README.md

---

**Document Status:** Updated for current script interfaces and test topology  
**Last Updated:** 2026-04-26  
**Maintainer:** telemt community
