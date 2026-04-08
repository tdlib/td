<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# TLS Fixture Refresh Helpers

These helpers make PR-2 capture expectations mechanically refreshable from future `pcap` and `pcapng` inputs instead of being maintained by manual `tshark` triage.

Files:

- `extract_client_hello_fixtures.py`: parser-driven offline extractor for TCP TLS ClientHello captures.
- `render_client_hello_expectations.py`: renderer that turns extracted JSON into stable summaries or ready-to-paste C++ literal blocks.
- `diff_client_hello_fixtures.py`: provenance and payload delta reporter for fixture refresh review.
- `merge_client_hello_fixture_summary.py`: merge step that turns the frozen JSON corpus into one reviewed C++ summary header for PR-2 tests.
- `check_fingerprint.py`: initial PR-9 fail-closed ClientHello smoke gate for route-mode, ECH policy, anti-singleton ECH variance, forbidden exact JA3/JA4 pins, and advisory JA3/JA4 telemetry.
- `check_ipt.py`: PR-9 IPT smoke gate over deterministic JSON artifacts with fail-closed canonical route metadata, QUIC-off enforcement, log-normal fit thresholds, keepalive bypass checks, detector-visible stall checks, and non-finite metric rejection.
- `check_drs.py`: PR-9 DRS smoke gate over TLS record payload size artifacts with fail-closed canonical route metadata, QUIC-off enforcement, anti-2878 regression checks, anti-repeat checks, histogram-distance thresholds, and non-finite metric rejection.
- `check_flow_behavior.py`: PR-9 cadence/reuse smoke gate over connection-flow artifacts with fail-closed canonical route metadata, QUIC-off enforcement, anti-churn checks, destination-share checks, lifetime/reuse thresholds aligned with runtime flow policy, and non-finite policy rejection.
- `check_fixture_registry_complete.py`: fail-closed registry gate for placeholder fixture ids, missing required fixture tags, advisory-code-sample contamination, and incomplete release-gating provenance.
- `run_corpus_smoke.py`: corpus-level PR-9 batch runner that executes registry completeness and per-artifact fingerprint checks across the checked-in ClientHello tree and emits one aggregate telemetry report.

Checked-in corpus:

- `fixtures/clienthello/linux_desktop/*.json`: reviewed Linux desktop corpus consumed by the current C++ differential tests.
- `fixtures/clienthello/android/*.json`: Android browser-capture corpus extracted from the real Android dump directory.
- `fixtures/clienthello/ios/*.json`: iOS browser-capture corpus extracted from the real iOS dump directory.
- `fixtures/clienthello/macos/*.json`: macOS browser-capture corpus extracted from the real macOS dump directory when the capture contains a TCP TLS ClientHello.
- `../stealth/ReviewedClientHelloFixtures.h`: generated summary header consumed by PR-2 capture-driven tests.
- `profiles_validation.json`: PR-9 registry for the reviewed Linux desktop corpus. Mobile/macOS artifacts are collected from the real dump directories but are not release-gated until their profile policies are reviewed.

## Requirements

- `python3`
- `tshark` in `PATH`

The extractor uses `tshark` only for frame selection and `tcp.reassembled.data`; all ClientHello parsing is done by repo-local Python code so the JSON schema is pinned by `parser_version`.

## Browser Capture Workflow

Example for a Chrome family refresh from `pcapng`:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/Linux, desktop/clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng" \
  --profile-id chrome144_linux_desktop \
  --source-kind browser_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id linux_desktop_chrome144_clienthello \
  --device-class desktop \
  --os-family linux \
  --route-mode non_ru_egress \
  --out artifacts/chrome144_linux_desktop.clienthello.json
```

Render the extracted artifact into test-facing constants:

```bash
python3 test/analysis/render_client_hello_expectations.py \
  --artifact artifacts/chrome144_linux_desktop.clienthello.json \
  --format cpp
```

Or render a review summary:

```bash
python3 test/analysis/render_client_hello_expectations.py \
  --artifact artifacts/chrome144_linux_desktop.clienthello.json \
  --format markdown
```

Refresh a checked-in artifact and compare the result with the frozen corpus:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/Linux, desktop/clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng" \
  --profile-id chrome144_linux_desktop \
  --source-kind browser_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id linux_desktop_chrome144_clienthello \
  --device-class desktop \
  --os-family linux \
  --route-mode non_ru_egress \
  --out /tmp/chrome144_linux_desktop.clienthello.json

python3 test/analysis/diff_client_hello_fixtures.py \
  --old test/analysis/fixtures/clienthello/linux_desktop/chrome144_linux_desktop.clienthello.json \
  --new /tmp/chrome144_linux_desktop.clienthello.json

python3 test/analysis/merge_client_hello_fixture_summary.py \
  --input-dir test/analysis/fixtures/clienthello/linux_desktop \
  --out-header test/stealth/ReviewedClientHelloFixtures.h
```

Run the initial PR-9 smoke checks against extracted ClientHello artifacts:

```bash
python3 test/analysis/check_fingerprint.py \
  --artifact test/analysis/fixtures/clienthello/linux_desktop/chrome144_linux_desktop.clienthello.json \
  --registry test/analysis/profiles_validation.json \
  --report-out artifacts/fingerprint_report.json
```

Fail closed if the profile registry still contains placeholder fixture ids or incomplete release-gating provenance:

```bash
python3 test/analysis/check_fixture_registry_complete.py \
  --registry test/analysis/profiles_validation.json \
  --report-out artifacts/fixture_registry_report.json
```

Run the corpus-level PR-9 smoke pass over the checked-in artifact tree:

```bash
python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --report-out artifacts/corpus_smoke_report.json
```

The corpus runner fails closed if the fixture tree is empty, if any artifact cannot be loaded, if the registry completeness gate fails, or if any artifact fails the structural fingerprint policy checks. The report includes per-artifact verdicts plus aggregate JA3/JA4 telemetry across all loaded samples.

The registry completeness gate now treats `source_kind`, `family`, `trust_tier`, `transport`, `platform_class`, and `tls_gen` as required fixture tags when `contamination_guard.fail_on_missing_required_tag` is enabled. Those tags must also stay in canonical form: `transport` is currently limited to `tcp` or `udp_quic_tls`, `platform_class` must stay within the canonical device classes, and `tls_gen` must stay within `tls12` or `tls13`. For release-gating profiles, `advisory_code_sample` fixtures are rejected directly when `allow_advisory_code_sample_per_profile` is `false`, instead of relying on mixed-source or corroboration side effects to catch them.

Run the new PR-9 smoke analyzers against precomputed JSON smoke artifacts:

```bash
python3 test/analysis/check_ipt.py \
  --artifact artifacts/ipt_smoke.json \
  --baseline-distance-threshold 0.10 \
  --report-out artifacts/ipt_report.json

python3 test/analysis/check_drs.py \
  --artifact artifacts/drs_smoke.json \
  --histogram-distance-threshold 0.15 \
  --report-out artifacts/drs_report.json

python3 test/analysis/check_flow_behavior.py \
  --artifact artifacts/flow_behavior_smoke.json \
  --policy-json artifacts/flow_policy.json \
  --report-out artifacts/flow_behavior_report.json
```

These three analyzers currently consume parser-agnostic JSON smoke artifacts rather than raw `pcap` input directly. That keeps the smoke verdict deterministic and testable while the dedicated capture/extraction steps for IPT, DRS, and flow telemetry are still being built out.

Smoke artifact metadata is fail-closed: `active_policy` must already be one of the canonical PR-9 values (`unknown`, `ru_egress`, `non_ru_egress`) and `quic_enabled` must be exactly `false`. Numeric thresholds and measured telemetry must be finite numbers; `NaN` and `inf` inputs are rejected as schema/policy failures instead of being normalized or silently accepted.

## curl_cffi Capture Workflow

For programmatic BoringSSL captures exported into `pcap` by an intercept workflow, the extractor accepts the extra provenance fields required by PR-2:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap artifacts/chrome133_curlcffi_capture.pcapng \
  --profile-id chrome133 \
  --source-kind curl_cffi_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id curl_cffi_refresh \
  --route-mode non_ru_egress \
  --capture-tool capture_chrome131.py \
  --capture-tool-version local-workspace \
  --curl-cffi-version 0.11.4 \
  --browser-type chrome133 \
  --target-host cloudflare.com \
  --intercept-method localhost_proxy \
  --out artifacts/chrome133_curlcffi.clienthello.json
```

## Artifact Contract

The extractor emits one immutable JSON artifact per input `pcap/pcapng` with:

- top-level provenance: `source_path`, `source_sha256`, `source_kind`, `capture_date_utc`, `scenario_id`, `route_mode`, `display_filter`, `transport`, `tls_handshake_type`, `parser_version`
- `route_mode` is stored in canonical PR-9 form: `unknown`, `ru_egress`, or `non_ru_egress`; legacy aliases like `non_ru` are normalized at extraction time and rejected if unknown
- extractor provenance: `tshark_version`
- one `samples[]` entry per matching ClientHello frame, each containing:
  - `fixture_id`, `frame_number`, `frame_time_utc`, `tcp_stream`
  - `cipher_suites`, `non_grease_cipher_suites`
  - `supported_groups`, `non_grease_supported_groups`
  - `key_share_entries`
  - `extension_types`, `non_grease_extensions_without_padding`
  - `alpn_protocols`, `compress_certificate_algorithms`
  - parsed `ech` lengths and suite identifiers when present
  - stable `sha256` digests for the TLS record, ClientHello body, session id, random, key shares, and ECH fields

This is intentionally richer than the current tests require so future PR-2 refreshes can regenerate expectations without re-parsing captures by hand.

## Checked-In Corpus Rules

- Each artifact under `fixtures/clienthello/<platform>` is immutable and tied to a single input `pcap/pcapng`.
- A fixture refresh must regenerate a new artifact, run `diff_client_hello_fixtures.py`, and review every provenance or payload change before replacing the checked-in JSON.
- After the reviewed JSON corpus is updated, `merge_client_hello_fixture_summary.py` must be rerun so PR-2 tests consume the updated corpus through `ReviewedClientHelloFixtures.h` instead of manual literal edits.
- If `parser_version` changes, the refresh must include the diff output in review notes because fixture ids or derived fields may shift even when the raw capture is unchanged.
- A corpus update is incomplete if it changes the checked-in JSON without also preserving `source_sha256`, `capture_date_utc`, and `scenario_id` provenance in the replacement artifact.

## Failure Mode

The extractor fails closed if:

- `tshark` returns no matching frames
- `tcp.reassembled.data` is missing for a selected ClientHello
- TLS length fields are inconsistent
- extension subparsers observe truncated or malformed data

That keeps fixture refreshes provenance-safe and aligned with the PR-2 fail-closed policy.