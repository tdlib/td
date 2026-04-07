# TLS Fixture Refresh Helpers

These helpers make PR-2 capture expectations mechanically refreshable from future `pcap` and `pcapng` inputs instead of being maintained by manual `tshark` triage.

Files:

- `extract_client_hello_fixtures.py`: parser-driven offline extractor for TCP TLS ClientHello captures.
- `render_client_hello_expectations.py`: renderer that turns extracted JSON into stable summaries or ready-to-paste C++ literal blocks.
- `diff_client_hello_fixtures.py`: provenance and payload delta reporter for fixture refresh review.
- `merge_client_hello_fixture_summary.py`: merge step that turns the frozen JSON corpus into one reviewed C++ summary header for PR-2 tests.

Checked-in corpus:

- `fixtures/clienthello/batch1/*.json`: frozen artifacts extracted from the current batch-1 browser captures.
- `../stealth/ReviewedClientHelloFixtures.h`: generated summary header consumed by PR-2 capture-driven tests.

## Requirements

- `python3`
- `tshark` in `PATH`

The extractor uses `tshark` only for frame selection and `tcp.reassembled.data`; all ClientHello parsing is done by repo-local Python code so the JSON schema is pinned by `parser_version`.

## Browser Capture Workflow

Example for a Chrome family refresh from `pcapng`:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/batch 1/clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng" \
  --profile-id chrome144_batch1 \
  --source-kind browser_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id batch1_ubuntu24_clienthello \
  --route-mode non_ru \
  --out artifacts/chrome144_batch1.clienthello.json
```

Render the extracted artifact into test-facing constants:

```bash
python3 test/analysis/render_client_hello_expectations.py \
  --artifact artifacts/chrome144_batch1.clienthello.json \
  --format cpp
```

Or render a review summary:

```bash
python3 test/analysis/render_client_hello_expectations.py \
  --artifact artifacts/chrome144_batch1.clienthello.json \
  --format markdown
```

Refresh a checked-in artifact and compare the result with the frozen corpus:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/batch 1/clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng" \
  --profile-id chrome144_batch1 \
  --source-kind browser_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id batch1_ubuntu24_clienthello \
  --route-mode non_ru \
  --out /tmp/chrome144_batch1.clienthello.json

python3 test/analysis/diff_client_hello_fixtures.py \
  --old test/analysis/fixtures/clienthello/batch1/chrome144_batch1.clienthello.json \
  --new /tmp/chrome144_batch1.clienthello.json

python3 test/analysis/merge_client_hello_fixture_summary.py \
  --input-dir test/analysis/fixtures/clienthello/batch1 \
  --out-header test/stealth/ReviewedClientHelloFixtures.h
```

## curl_cffi Capture Workflow

For programmatic BoringSSL captures exported into `pcap` by an intercept workflow, the extractor accepts the extra provenance fields required by PR-2:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap artifacts/chrome133_curlcffi_capture.pcapng \
  --profile-id chrome133 \
  --source-kind curl_cffi_capture \
  --capture-date-utc 2026-04-07T00:00:00Z \
  --scenario-id curl_cffi_refresh \
  --route-mode non_ru \
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

- Each artifact in `fixtures/clienthello/batch1` is immutable and tied to a single input `pcap/pcapng`.
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