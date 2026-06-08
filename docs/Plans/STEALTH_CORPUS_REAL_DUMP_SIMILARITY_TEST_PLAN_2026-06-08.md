<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Stealth Corpus Real-Dump Similarity Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make stealth fingerprint statistical tests prove that generated browser TLS fingerprints are constrained by, and measurably similar to, reviewed real browser dumps under `docs/Samples/Traffic dumps/` and `test/analysis/fixtures/clienthello/`.

**Architecture:** Add a fixture-derived oracle layer that separates exact reviewed fields, observed catalogs, statistical diagnostics, and unavailable evidence. Runtime generator tests must consume this oracle instead of self-calibrating from generated output. Existing entropy, fuzz, and adversarial tests remain useful, but release-facing similarity claims move to fail-closed fixture-derived gates.

**Tech Stack:** C++ test binary `run_all_tests`, Python unittest-based analysis tests, CMake test registration, generated C++ fixture headers, reviewed ClientHello JSON fixtures, SocratiCode and context-mode for discovery and evidence capture.

**Project Principles:** Before any implementation, agents must read `AGENTS.md` and the repository principle files under `.github/instructions/`, especially the TDD, architecture, C++ rules, security requirements, and any instruction file that applies to touched paths.

**Execution Order for Agents:**

1. Critically assess this plan against the real code and fixtures. The plan can contain deviations, bugs, stale assumptions, or missing constraints, so compare it to the current implementation before trusting it.
2. Assess the plan against the project's principles in `AGENTS.md` and `.github/instructions/`. Resolve any mismatch before writing code or editing generated artifacts. Fix the plan if needed.
3. Implement the plan only after the code and principle reviews are complete and the plan still holds up.

---

## Core Finding This Plan Addresses

The current corpus tests are broad, but several tests prove generator stability or entropy rather than similarity to real traffic dumps:

1. Files named as `1k` sometimes run only 3 or 64 iterations unless `TD_NIGHTLY_CORPUS=1`.
2. Some wire baseline tests calibrate envelopes and expected counts from the generator under test.
3. Several reviewed family-lane baselines leave exact invariant fields empty; some tests return early when those fields are empty.
4. Chrome shuffle tests assert permutation diversity and legality, but do not clearly distinguish real-corpus evidence from synthetic seed stress.
5. Wire-length tolerance tests use broad percent envelopes that can admit byte lengths not seen in reviewed dumps.

This plan makes those states explicit and testable. It does not say all current tests are wrong. It says only fixture-derived tests may support release-facing claims about "similar to real browsers."

## Non-Negotiable Rules For Implementers

1. Write failing tests before changing production, scripts, generated artifacts, or docs.
2. Do not hand-edit generated headers. Modify the generator script and regenerate in topological order.
3. Do not use generated seeds as independent browser evidence. Seeds test runtime variability only.
4. Missing reviewed evidence is `unavailable` and must fail release-facing gates for that field.
5. Imported fixtures are diagnostic only. They may appear in reports, but not in release-gating denominators.
6. Exact reviewed fields outrank statistical summaries. If a field has an exact reviewed value for a cohort, tests must enforce it exactly.
7. Cohorts must be generation-aware when exact fields diverge. Do not pool iOS 17.2, iOS 18.7, and iOS 26 Apple TLS if a release-critical field differs.
8. Keep all new tests in separate files. Do not add inline tests to production code.
9. Preserve existing adversarial tests; do not weaken them to make new similarity tests pass.
10. When touching stealth TLS, read these instructions first:
    - `.github/instructions/TDD_approach.instructions.md`
    - `.github/instructions/Security_Requirements.instructions.md`
    - `.github/instructions/c++_rules.instructions.md`
    - `.github/instructions/architecture.instructions.md`

## Evidence To Re-Verify At Current HEAD

Before implementation, the first agent must re-run these checks and record results in a handoff artifact under `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/`.

- `codebase_status` must be green.
- `docs/Samples/Traffic dumps/` must contain raw packet captures.
- `test/analysis/fixtures/clienthello/` must contain reviewed ClientHello JSON fixtures with `source_kind`, `source_sha256`, `scenario_id`, `route_mode`, and `parser_version`.
- `test/stealth/ReviewedFamilyLaneBaselines.h` must be treated as generated.
- `test/stealth/CorpusIterationTiers.h` defines quick, spot, and full iteration tiers.

Recommended evidence script:

```bash
python3 - <<'PY'
import json
import pathlib

root = pathlib.Path('.')
raw = list((root / 'docs' / 'Samples' / 'Traffic dumps').rglob('*.pcap')) + list(
    (root / 'docs' / 'Samples' / 'Traffic dumps').rglob('*.pcapng')
)
fixtures = list((root / 'test' / 'analysis' / 'fixtures' / 'clienthello').rglob('*.json'))
sample_count = 0
missing = []
for fixture in fixtures:
    data = json.loads(fixture.read_text(encoding='utf-8'))
    sample_count += len(data.get('samples', []))
    for field in ('source_kind', 'source_sha256', 'scenario_id', 'route_mode', 'parser_version'):
        if not data.get(field):
            missing.append(f'{fixture}:{field}')

print(f'raw_capture_count={len(raw)}')
print(f'reviewed_fixture_count={len(fixtures)}')
print(f'reviewed_sample_count={sample_count}')
print(f'missing_required_metadata={len(missing)}')
for item in missing[:20]:
    print(item)
PY
```

Expected result shape:

```text
raw_capture_count=<positive integer>
reviewed_fixture_count=<positive integer>
reviewed_sample_count=<positive integer>
missing_required_metadata=0
```

If `missing_required_metadata` is nonzero, stop and fix provenance before implementing similarity gates.

## Files To Create Or Modify

### New Python Tests

- Create: `test/analysis/test_family_lane_oracle_generation.py`
  - Verifies exact-field availability, field status, extension-count histograms, wire-length catalogs, and route-lane fail-closed behavior in the generated oracle.

- Create: `test/analysis/test_corpus_iteration_tier_naming_contract.py`
  - Prevents tests named `1k` from silently using quick or spot iteration budgets.

- Create: `test/analysis/test_similarity_release_gate_contract.py`
  - Verifies that release-facing similarity predicates cannot pass when exact reviewed fields are unavailable.

### Python Script Changes

- Modify: `test/analysis/build_family_lane_baselines.py`
  - Extend generated baseline data with per-field availability status and exact-field histograms.
  - Preserve extension-count histograms and observed wire-length catalogs by cohort.
  - Emit `unavailable`, `exact`, or `mixed` status for each release-critical field.

- Modify if needed: `test/analysis/check_fingerprint.py`
  - Align fixture policy diagnostics with the new field availability vocabulary.

- Modify if needed: `test/analysis/merge_client_hello_fixture_summary.py`
  - Ensure merged summaries preserve sample-level extension count, extension order, ECH payload length, ALPS type, SNI length, record length, and handshake length.

### Generated C++ Headers

- Regenerate only through scripts:
  - `test/stealth/ReviewedFamilyLaneBaselines.h`
  - Add generated header if the existing one becomes too crowded: `test/stealth/ReviewedGeneratorSimilarityOracle.h`

Do not hand-edit these files.

### New C++ Tests

- Create: `test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp`
  - Verifies generated ClientHello fields against exact reviewed fields where available.
  - Fails when a release profile has unavailable exact fields for a release-critical claim.

- Create: `test/stealth/test_tls_generator_extension_count_similarity.cpp`
  - Verifies generated non-GREASE, non-padding extension counts are contained in reviewed fixture count catalogs for the matching cohort.

- Create: `test/stealth/test_tls_generator_shuffle_similarity.cpp`
  - Separates Chrome anchored-shuffle legality from real-corpus evidence.
  - Rejects degenerate shuffle, pinned positions, duplicated extension types, and illegal extension sets.

- Create: `test/stealth/test_tls_generator_wire_length_fixture_gate.cpp`
  - Replaces broad self-calibrated envelopes with fixture-derived exact lengths or explicit SNI-adjusted length models.

- Create: `test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp`
  - Verifies no release-facing generator similarity gate returns early or silently passes when required reviewed evidence is unavailable.

### Existing C++ Test Changes

- Modify: `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
  - Reclassify as generator stability only, or make it consume fixture-derived oracle data.
  - Remove self-calibrated release-facing language.

- Modify: `test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp`
  - Replace early returns on empty exact fields with explicit unavailable assertions.

- Modify: `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`
  - Replace broad wire-length tolerance with exact or model-based fixture gates.

- Modify: `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp`
  - Stop using `kQuickIterations` under a `1k` name, or rename the suite and add a real full-tier counterpart.

- Modify: `test/stealth/test_tls_corpus_chrome_grease_uniformity_1k.cpp`
  - Keep entropy checks, but label them as seed-stress checks unless a real fixture-derived distribution comparison is added.

- Modify: `test/stealth/test_tls_corpus_chrome_permutation_position_1k.cpp`
  - Keep shuffle diversity tests, but add real-corpus oracle checks for extension set, count, and allowed order policy.

### CMake

- Modify: `test/CMakeLists.txt`
  - Register all new C++ test files in `run_all_tests`.

### Documentation

- Modify: `docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md`
  - Clarify which suites are release-facing real-corpus similarity gates and which are runtime stress diagnostics.

- Modify: `docs/Documentation/Lessons_Learnt.md`
  - Add a short note that self-calibrated generator tests are not real-browser similarity evidence.

## Release-Critical Fields

The oracle must classify these fields for each `(family_id, cohort_id, route_lane, evidence_lane)`:

1. `non_grease_cipher_suites_ordered`
2. `non_grease_extension_set`
3. `non_grease_supported_groups`
4. `non_grease_supported_versions`
5. `alpn_protocols`
6. `compress_certificate_algorithms`
7. `extension_order_policy`
8. `observed_extension_order_templates`
9. `observed_non_grease_extension_count_histogram`
10. `observed_wire_lengths`
11. `observed_handshake_lengths`
12. `observed_record_lengths`
13. `observed_ech_payload_lengths`
14. `observed_alps_types`
15. `ech_presence`
16. `tls_record_version`
17. `client_hello_legacy_version`

Each field status must be one of:

- `exact`: every reviewed sample in the cohort has the same value.
- `catalog`: reviewed samples have multiple observed values and generated output must be in the observed catalog or a documented fixture-derived model.
- `policy`: exact equality would overfit, so the field is checked by an explicit upstream policy plus fixture-derived set membership.
- `mixed`: reviewed samples disagree in a way that requires splitting the cohort before release claims are valid.
- `unavailable`: no reviewed evidence exists for this field and cohort.

`mixed` and `unavailable` must fail release-facing gates. They may pass diagnostic-only tests if the test name and report output say diagnostic.

## Task 1: Add Python Oracle Contract Tests

**Files:**
- Create: `test/analysis/test_family_lane_oracle_generation.py`
- Modify: `test/analysis/build_family_lane_baselines.py`

- [ ] **Step 1: Write failing tests for field status and extension-count histograms**

Create `test/analysis/test_family_lane_oracle_generation.py`:

```python
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import build_family_lane_baselines as baselines


def artifact(
    *,
    profile_id: str,
    fixture_id: str,
    source_sha256: str,
    extensions: list[str],
    cipher_suites: list[str],
    supported_groups: list[str],
    supported_versions: list[str],
    record_length: int,
    handshake_length: int,
    ech_payload_length: int | None,
) -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "profile_id": profile_id,
        "route_mode": "non_ru_egress",
        "source_kind": "browser_capture",
        "source_path": f"docs/Samples/Traffic dumps/Linux, desktop/{profile_id}.pcapng",
        "source_sha256": source_sha256,
        "scenario_id": profile_id,
        "parser_version": "tls-clienthello-parser-v1",
        "capture_date_utc": "2026-04-08T00:00:00Z",
        "os_family": "linux",
        "device_class": "desktop",
        "transport": "tcp",
        "samples": [
            {
                "fixture_id": fixture_id,
                "frame_number": 5,
                "tcp_stream": 0,
                "record_length": record_length,
                "handshake_length": handshake_length,
                "cipher_suites": ["0x0A0A", *cipher_suites],
                "non_grease_cipher_suites": cipher_suites,
                "supported_groups": ["0x1A1A", *supported_groups],
                "non_grease_supported_groups": supported_groups,
                "supported_versions": ["0x2A2A", *supported_versions],
                "non_grease_supported_versions": supported_versions,
                "extension_types": ["0x0A0A", *extensions, "0x1A1A"],
                "non_grease_extensions_without_padding": extensions,
                "alpn_protocols": ["h2", "http/1.1"],
                "compress_certificate_algorithms": ["0x0002"],
                "key_share_entries": [
                    {"group": "0x11EC", "key_exchange_length": 1216, "is_grease_group": False},
                    {"group": "0x001D", "key_exchange_length": 32, "is_grease_group": False},
                ],
                "ech": None
                if ech_payload_length is None
                else {
                    "type": "0xFE0D",
                    "payload_length": ech_payload_length,
                    "kdf_id": "0x0001",
                    "aead_id": "0x0001",
                },
            }
        ],
    }


class FamilyLaneOracleGenerationTest(unittest.TestCase):
    def test_exact_fields_and_histograms_are_emitted(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            (fixtures / "chrome_a.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_a",
                        fixture_id="chrome_a:frame5",
                        source_sha256="a" * 64,
                        extensions=["0x0000", "0x002B", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1800,
                        handshake_length=1795,
                        ech_payload_length=176,
                    )
                ),
                encoding="utf-8",
            )
            (fixtures / "chrome_b.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_b",
                        fixture_id="chrome_b:frame5",
                        source_sha256="b" * 64,
                        extensions=["0x002B", "0x0000", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1832,
                        handshake_length=1827,
                        ech_payload_length=208,
                    )
                ),
                encoding="utf-8",
            )

            oracle = baselines.build_family_lane_oracle_for_tests(fixtures)

        lane = oracle[("chromium_linux_desktop", "non_ru_egress")]
        self.assertEqual("exact", lane["fields"]["non_grease_cipher_suites_ordered"]["status"])
        self.assertEqual(["0x1301", "0x1302"], lane["fields"]["non_grease_cipher_suites_ordered"]["value"])
        self.assertEqual("exact", lane["fields"]["non_grease_extension_set"]["status"])
        self.assertEqual(
            ["0x0000", "0x0010", "0x002B", "0x44CD", "0xFE0D"],
            lane["fields"]["non_grease_extension_set"]["value"],
        )
        self.assertEqual({"5": 2}, lane["fields"]["non_grease_extension_count_histogram"]["value"])
        self.assertEqual([1800, 1832], lane["fields"]["record_lengths"]["value"])
        self.assertEqual([1795, 1827], lane["fields"]["handshake_lengths"]["value"])
        self.assertEqual([176, 208], lane["fields"]["ech_payload_lengths"]["value"])

    def test_mixed_exact_field_is_not_silently_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            (fixtures / "one.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_one",
                        fixture_id="chrome_one:frame5",
                        source_sha256="c" * 64,
                        extensions=["0x0000", "0x002B", "0x0010"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=100,
                        handshake_length=95,
                        ech_payload_length=None,
                    )
                ),
                encoding="utf-8",
            )
            (fixtures / "two.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_two",
                        fixture_id="chrome_two:frame5",
                        source_sha256="d" * 64,
                        extensions=["0x0000", "0x002B", "0x0010"],
                        cipher_suites=["0x1301", "0x1303"],
                        supported_groups=["0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=100,
                        handshake_length=95,
                        ech_payload_length=None,
                    )
                ),
                encoding="utf-8",
            )

            oracle = baselines.build_family_lane_oracle_for_tests(fixtures)

        lane = oracle[("chromium_linux_desktop", "non_ru_egress")]
        self.assertEqual("mixed", lane["fields"]["non_grease_cipher_suites_ordered"]["status"])
        self.assertEqual(
            [
                ["0x1301", "0x1302"],
                ["0x1301", "0x1303"],
            ],
            lane["fields"]["non_grease_cipher_suites_ordered"]["observed_values"],
        )
```

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```bash
python3 -m unittest test.analysis.test_family_lane_oracle_generation -v
```

Expected failure:

```text
AttributeError: module 'build_family_lane_baselines' has no attribute 'build_family_lane_oracle_for_tests'
```

- [ ] **Step 3: Implement the oracle helper**

In `test/analysis/build_family_lane_baselines.py`, add a public helper with this behavior:

```python
def build_family_lane_oracle_for_tests(fixtures_root: pathlib.Path) -> dict[tuple[str, str], dict]:
    artifacts = load_reviewed_clienthello_artifacts(fixtures_root)
    return build_family_lane_oracle(artifacts)
```

Add `build_family_lane_oracle(artifacts)` using these exact rules:

```python
def field_status(values: list[object]) -> dict:
    if not values:
        return {"status": "unavailable", "value": None}
    unique = []
    for value in values:
        if value not in unique:
            unique.append(value)
    if len(unique) == 1:
        return {"status": "exact", "value": unique[0]}
    return {"status": "mixed", "observed_values": unique}
```

For catalog fields, use sorted unique values and `status = "catalog"` when at least one value exists:

```python
def catalog_status(values: list[object]) -> dict:
    if not values:
        return {"status": "unavailable", "value": []}
    unique = []
    for value in values:
        if value not in unique:
            unique.append(value)
    return {"status": "catalog", "value": sorted(unique)}
```

For extension-count histograms:

```python
def histogram_status(values: list[int]) -> dict:
    if not values:
        return {"status": "unavailable", "value": {}}
    histogram: dict[str, int] = {}
    for value in values:
        histogram[str(value)] = histogram.get(str(value), 0) + 1
    return {"status": "catalog", "value": dict(sorted(histogram.items()))}
```

The helper must group by `(family_id, route_lane)`. If the current script lacks a reusable family classifier, create a narrow classifier that matches the existing `ReviewedFamilyLaneBaselines.h` family IDs:

```python
def classify_family(profile_id: str, os_family: str) -> str:
    p = profile_id.lower()
    if "firefox" in p or "librewolf" in p or "zen" in p or "ironfox" in p:
        if os_family == "android":
            return "firefox_android"
        if os_family == "macos":
            return "firefox_macos"
        if os_family == "windows":
            return "firefox_windows"
        return "firefox_linux_desktop"
    if "safari" in p or profile_id.lower().startswith("ios"):
        return "apple_ios_tls" if os_family == "ios" else "apple_macos_tls"
    if os_family == "android":
        return "android_chromium"
    if os_family == "ios":
        return "ios_chromium"
    if os_family == "macos":
        return "chromium_macos"
    if os_family == "windows":
        return "chromium_windows"
    return "chromium_linux_desktop"
```

- [ ] **Step 4: Run the tests and verify they pass**

Run:

```bash
python3 -m unittest test.analysis.test_family_lane_oracle_generation -v
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add test/analysis/test_family_lane_oracle_generation.py test/analysis/build_family_lane_baselines.py
git commit -m "test: add fixture-derived family lane oracle contracts"
```

## Task 2: Enforce Iteration-Tier Naming Truth

**Files:**
- Create: `test/analysis/test_corpus_iteration_tier_naming_contract.py`
- Modify: C++ test files whose names or suite names claim `1k` while using quick or spot tiers.

- [ ] **Step 1: Write the failing naming contract**

Create `test/analysis/test_corpus_iteration_tier_naming_contract.py`:

```python
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STEALTH_TEST_ROOT = REPO_ROOT / "test" / "stealth"


class CorpusIterationTierNamingContract(unittest.TestCase):
    def test_files_named_1k_do_not_use_quick_iterations(self) -> None:
        offenders: list[str] = []
        for path in STEALTH_TEST_ROOT.glob("*1k*.cpp"):
            text = path.read_text(encoding="utf-8")
            if "kQuickIterations" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)

    def test_1k_suite_names_are_full_tier_or_explicit_nightly(self) -> None:
        offenders: list[str] = []
        for path in STEALTH_TEST_ROOT.glob("*.cpp"):
            text = path.read_text(encoding="utf-8")
            if "1k" not in path.name.lower() and not re.search(r"TEST\\([^,]*1k", text):
                continue
            if "kFullIterations" in text or "is_nightly_corpus_enabled()" in text:
                continue
            offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
python3 -m unittest test.analysis.test_corpus_iteration_tier_naming_contract -v
```

Expected failure includes at least:

```text
test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp
```

- [ ] **Step 3: Fix naming or iteration tiers**

For each offender, choose one of these two exact fixes:

1. If the suite is intended as quick smoke, rename the file and suite from `1k` to `quick` or `spot`.
2. If the suite is intended as statistical evidence, use `spot_or_full_corpus_iterations()` and make the test name disclose `SpotOrFull`.

For `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp`, prefer this change:

```cpp
const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
```

Add the missing include:

```cpp
#include "test/stealth/CorpusIterationTiers.h"
```

This keeps the file eligible for full 1024-seed nightly execution and 64-seed spot execution.

- [ ] **Step 4: Run the naming contract again**

Run:

```bash
python3 -m unittest test.analysis.test_corpus_iteration_tier_naming_contract -v
```

Expected:

```text
OK
```

- [ ] **Step 5: Build the touched C++ tests**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
cmake --build build --target run_all_tests --parallel 4
```

Expected:

```text
[100%] Built target run_all_tests
```

- [ ] **Step 6: Commit**

```bash
git add test/analysis/test_corpus_iteration_tier_naming_contract.py test/stealth
git commit -m "test: make corpus iteration tier names truthful"
```

## Task 3: Generate Field Availability Into C++ Baselines

**Files:**
- Modify: `test/analysis/build_family_lane_baselines.py`
- Modify generated: `test/stealth/ReviewedFamilyLaneBaselines.h`
- Test: `test/analysis/test_family_lane_oracle_generation.py`

- [ ] **Step 1: Add Python tests for generated C++ field status**

Append to `test/analysis/test_family_lane_oracle_generation.py`:

```python
    def test_generated_cpp_contains_field_status_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            output = root / "ReviewedFamilyLaneBaselines.h"
            (fixtures / "chrome.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome146_177_linux_desktop",
                        fixture_id="chrome146_177_linux_desktop:frame5",
                        source_sha256="e" * 64,
                        extensions=["0x0000", "0x002B", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1800,
                        handshake_length=1795,
                        ech_payload_length=176,
                    )
                ),
                encoding="utf-8",
            )

            baselines.generate_family_lane_baselines_for_tests(fixtures, output)
            text = output.read_text(encoding="utf-8")

        self.assertIn("enum class EvidenceFieldStatus", text)
        self.assertIn("non_grease_cipher_suites_status", text)
        self.assertIn("non_grease_extension_set_status", text)
        self.assertIn("non_grease_extension_count_histogram", text)
        self.assertIn("observed_handshake_lengths", text)
        self.assertIn("observed_record_lengths", text)
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
python3 -m unittest test.analysis.test_family_lane_oracle_generation -v
```

Expected failure:

```text
AttributeError: module 'build_family_lane_baselines' has no attribute 'generate_family_lane_baselines_for_tests'
```

- [ ] **Step 3: Add generated C++ data structures**

In the generator output, add:

```cpp
enum class EvidenceFieldStatus : td::uint8 {
  Unavailable = 0,
  Exact = 1,
  Catalog = 2,
  Policy = 3,
  Mixed = 4,
};

struct ExtensionCountBucket final {
  size_t count{0};
  size_t observed_samples{0};
};
```

Extend `ExactInvariants` or `FamilyLaneBaseline` with explicit statuses:

```cpp
EvidenceFieldStatus non_grease_cipher_suites_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus non_grease_extension_set_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus non_grease_supported_groups_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus non_grease_supported_versions_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus alpn_protocols_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus compress_cert_algorithms_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus extension_order_templates_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus wire_lengths_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus ech_payload_lengths_status{EvidenceFieldStatus::Unavailable};
EvidenceFieldStatus alps_types_status{EvidenceFieldStatus::Unavailable};
vector<ExtensionCountBucket> non_grease_extension_count_histogram;
vector<size_t> observed_handshake_lengths;
vector<size_t> observed_record_lengths;
```

- [ ] **Step 4: Regenerate the header**

Run the repository's existing generation command. If the command is not documented, inspect `test/analysis/build_family_lane_baselines.py --help` and use the script's canonical output path. The command must write `test/stealth/ReviewedFamilyLaneBaselines.h`.

Expected:

```text
generated test/stealth/ReviewedFamilyLaneBaselines.h
```

- [ ] **Step 5: Run Python tests**

Run:

```bash
python3 -m unittest test.analysis.test_family_lane_oracle_generation -v
```

Expected:

```text
OK
```

- [ ] **Step 6: Build C++ tests**

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
```

Expected:

```text
[100%] Built target run_all_tests
```

- [ ] **Step 7: Commit**

```bash
git add test/analysis/build_family_lane_baselines.py test/analysis/test_family_lane_oracle_generation.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: expose reviewed evidence availability in family baselines"
```

## Task 4: Add C++ Fail-Closed Availability Gates

**Files:**
- Create: `test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing C++ test**

Create `test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;

void assert_release_critical_field_available(EvidenceFieldStatus status) {
  ASSERT_TRUE(status == EvidenceFieldStatus::Exact || status == EvidenceFieldStatus::Catalog ||
              status == EvidenceFieldStatus::Policy);
}

TEST(TlsReleaseSimilarityUnavailableFailClosed, ChromiumWindowsNonRuDoesNotPretendEmptyExactFieldsPass) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  assert_release_critical_field_available(baseline->non_grease_cipher_suites_status);
  assert_release_critical_field_available(baseline->non_grease_extension_set_status);
  assert_release_critical_field_available(baseline->non_grease_supported_groups_status);
}

TEST(TlsReleaseSimilarityUnavailableFailClosed, UnknownRouteLanesRemainUnavailable) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("unknown"));
  ASSERT_TRUE(baseline != nullptr);

  ASSERT_EQ(EvidenceFieldStatus::Unavailable, baseline->non_grease_cipher_suites_status);
  ASSERT_EQ(EvidenceFieldStatus::Unavailable, baseline->non_grease_extension_set_status);
  ASSERT_EQ(EvidenceFieldStatus::Unavailable, baseline->wire_lengths_status);
}

}  // namespace
```

- [ ] **Step 2: Register the test**

Add to `test/CMakeLists.txt` in the same section as the other stealth tests:

```cmake
stealth/test_tls_release_similarity_unavailable_fail_closed.cpp
```

- [ ] **Step 3: Build and run the failing test**

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsReleaseSimilarityUnavailableFailClosed
```

Expected failure:

```text
TlsReleaseSimilarityUnavailableFailClosed.ChromiumWindowsNonRuDoesNotPretendEmptyExactFieldsPass
```

- [ ] **Step 4: Fix baseline generation or cohort classification**

Make one of these changes, based on fixture truth:

1. If reviewed Windows Chromium fixture samples have exact cipher suites and supported groups, populate those exact values in the generated baseline.
2. If reviewed Windows Chromium samples are mixed across browser generations, split into generation-aware cohorts and update the runtime profile mapping to test the matching cohort.
3. If evidence is truly missing, keep the status `Unavailable` and mark the profile as non-release-gating for that field in the release predicate registry.

Do not change the test to accept an unavailable release-critical field.

- [ ] **Step 5: Run the test again**

Run:

```bash
./build/test/run_all_tests --filter TlsReleaseSimilarityUnavailableFailClosed
```

Expected:

```text
OK
```

- [ ] **Step 6: Commit**

```bash
git add test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp test/CMakeLists.txt test/analysis/build_family_lane_baselines.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: fail closed on unavailable release similarity evidence"
```

## Task 5: Add Generator Exact-Field Fixture Gates

**Files:**
- Create: `test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing exact-field gate**

Create `test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint64 kSeeds = 64;

void assert_status_is_enforceable(EvidenceFieldStatus status) {
  ASSERT_TRUE(status == EvidenceFieldStatus::Exact || status == EvidenceFieldStatus::Catalog ||
              status == EvidenceFieldStatus::Policy);
}

void run_exact_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  assert_status_is_enforceable(baseline->non_grease_cipher_suites_status);
  assert_status_is_enforceable(baseline->non_grease_extension_set_status);
  assert_status_is_enforceable(baseline->non_grease_supported_versions_status);

  FamilyLaneMatcher matcher(*baseline);
  for (td::uint64 seed = 0; seed < kSeeds; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed.ok_ref()));
  }
}

TEST(TlsGeneratorFixtureExactFieldsGate, Chrome133MatchesChromiumLinuxReviewedExactFields) {
  run_exact_gate(Slice("chromium_linux_desktop"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorFixtureExactFieldsGate, Firefox148MatchesFirefoxLinuxReviewedExactFields) {
  run_exact_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorFixtureExactFieldsGate, IOS14MatchesAppleIosReviewedExactFields) {
  run_exact_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled);
}

}  // namespace
```

- [ ] **Step 2: Register and run**

Add to `test/CMakeLists.txt`:

```cmake
stealth/test_tls_generator_fixture_exact_fields_gate.cpp
```

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsGeneratorFixtureExactFieldsGate
```

Expected: at least one failure if a currently empty exact status is treated as unavailable.

- [ ] **Step 3: Fix exact-field availability or split cohorts**

Use the fixture truth:

- If all samples agree, emit `Exact`.
- If samples differ by browser generation or OS generation, split the cohort.
- If samples differ because the field is intentionally variable, emit `Catalog` or `Policy` and add a policy-specific matcher.

- [ ] **Step 4: Run exact-field gates**

Run:

```bash
./build/test/run_all_tests --filter TlsGeneratorFixtureExactFieldsGate
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp test/CMakeLists.txt test/analysis/build_family_lane_baselines.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: compare generated TLS fields to reviewed fixture invariants"
```

## Task 6: Add Extension-Count Similarity Gates

**Files:**
- Create: `test/stealth/test_tls_generator_extension_count_similarity.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write extension-count gate**

Create `test/stealth/test_tls_generator_extension_count_similarity.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::extension_set_non_grease_no_padding;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint64 kSeeds = 128;

bool histogram_contains_count(const decltype(get_baseline(Slice(), Slice())->non_grease_extension_count_histogram) &histogram,
                              size_t count) {
  for (const auto &bucket : histogram) {
    if (bucket.count == count) {
      return true;
    }
  }
  return false;
}

void run_extension_count_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_EQ(EvidenceFieldStatus::Catalog, baseline->non_grease_extension_count_histogram_status);
  ASSERT_FALSE(baseline->non_grease_extension_count_histogram.empty());

  for (td::uint64 seed = 0; seed < kSeeds; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto count = extension_set_non_grease_no_padding(parsed.ok_ref()).size();
    ASSERT_TRUE(histogram_contains_count(baseline->non_grease_extension_count_histogram, count));
  }
}

TEST(TlsGeneratorExtensionCountSimilarity, Chrome133CountsAppearInReviewedChromiumLinuxCatalog) {
  run_extension_count_gate(Slice("chromium_linux_desktop"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorExtensionCountSimilarity, Firefox148CountsAppearInReviewedFirefoxLinuxCatalog) {
  run_extension_count_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorExtensionCountSimilarity, IOS14CountsAppearInReviewedAppleIosCatalog) {
  run_extension_count_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled);
}

}  // namespace
```

- [ ] **Step 2: Register and run**

Add to `test/CMakeLists.txt`:

```cmake
stealth/test_tls_generator_extension_count_similarity.cpp
```

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsGeneratorExtensionCountSimilarity
```

Expected: failure until the generated histogram field exists and is populated.

- [ ] **Step 3: Fix generator and matcher types**

If the test does not compile because `non_grease_extension_count_histogram_status` is missing, add it in Task 3's generated structures and regenerate.

- [ ] **Step 4: Run and commit**

Run:

```bash
./build/test/run_all_tests --filter TlsGeneratorExtensionCountSimilarity
```

Expected:

```text
OK
```

Commit:

```bash
git add test/stealth/test_tls_generator_extension_count_similarity.cpp test/CMakeLists.txt test/analysis/build_family_lane_baselines.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: gate generated extension counts against reviewed fixture catalogs"
```

## Task 7: Replace Self-Calibrated Wire Baselines

**Files:**
- Create: `test/stealth/test_tls_generator_wire_length_fixture_gate.cpp`
- Modify: `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write a fixture-derived wire-length gate**

Create `test/stealth/test_tls_generator_wire_length_fixture_gate.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;

constexpr td::int32 kUnixTime = 1712345678;

void run_exact_or_modeled_wire_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode, Slice sni) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->wire_lengths_status == EvidenceFieldStatus::Catalog ||
              baseline->wire_lengths_status == EvidenceFieldStatus::Policy);
  ASSERT_FALSE(baseline->set_catalog.observed_wire_lengths.empty());

  FamilyLaneMatcher matcher(*baseline);
  for (td::uint64 seed = 0; seed < 128; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile(sni, "0123456789secret", kUnixTime, profile, ech_mode, rng);
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), 0.0));
  }
}

TEST(TlsGeneratorWireLengthFixtureGate, Firefox148WireLengthIsExactlyReviewedForNormalizedSni) {
  run_exact_or_modeled_wire_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer,
                                 Slice("www.google.com"));
}

TEST(TlsGeneratorWireLengthFixtureGate, IOS14WireLengthIsExactlyReviewedForNormalizedSni) {
  run_exact_or_modeled_wire_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled,
                                 Slice("www.apple.com"));
}

}  // namespace
```

- [ ] **Step 2: Register and run**

Add to `test/CMakeLists.txt`:

```cmake
stealth/test_tls_generator_wire_length_fixture_gate.cpp
```

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsGeneratorWireLengthFixtureGate
```

Expected failure if current generator lengths are only within loose percent tolerance.

- [ ] **Step 3: Add SNI-adjusted model only where needed**

If exact wire lengths fail solely because fixture SNI differs from generated SNI, add a fixture-derived model:

```cpp
struct WireLengthModel final {
  size_t base_wire_length{0};
  size_t fixture_sni_length{0};
};

size_t expected_wire_length_for_sni(const WireLengthModel &model, size_t generated_sni_length) {
  return model.base_wire_length - model.fixture_sni_length + generated_sni_length;
}
```

Only use the model when the fixture artifacts preserve the fixture SNI length. If SNI length is unavailable, keep the status `Unavailable`.

- [ ] **Step 4: Reclassify nightly Monte Carlo**

In `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`, replace comments claiming baseline similarity with this wording:

```cpp
// Nightly-scale generator stability Monte Carlo. This suite is diagnostic:
// it verifies that generated profiles stay internally stable over many seeds.
// It is not release-facing real-browser similarity evidence; fixture-derived
// similarity gates live in test_tls_generator_wire_length_fixture_gate.cpp.
```

Do not remove the diagnostic suite unless a separate stability suite already covers the same behavior.

- [ ] **Step 5: Run focused tests**

Run:

```bash
./build/test/run_all_tests --filter 'TlsGeneratorWireLengthFixtureGate|TLS_NightlyWireBaselineMonteCarlo'
```

Expected:

```text
OK
```

- [ ] **Step 6: Commit**

```bash
git add test/stealth/test_tls_generator_wire_length_fixture_gate.cpp test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp test/CMakeLists.txt test/analysis/build_family_lane_baselines.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: use reviewed fixture wire lengths for similarity gates"
```

## Task 8: Harden Chrome Shuffle Similarity Without Overfitting

**Files:**
- Create: `test/stealth/test_tls_generator_shuffle_similarity.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write shuffle tests that separate policy from evidence**

Create `test/stealth/test_tls_generator_shuffle_similarity.cpp`:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/UpstreamRuleVerifiers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

#include <set>

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::extension_set_non_grease_no_padding;
using td::mtproto::test::non_grease_extension_sequence;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::verifiers::ExtensionOrderVerifier;

constexpr td::int32 kUnixTime = 1712345678;

std::string order_key(const std::vector<td::uint16> &order) {
  std::string result;
  for (auto value : order) {
    if (!result.empty()) {
      result.push_back(',');
    }
    result += td::mtproto::test::hex_u16(value);
  }
  return result;
}

TEST(TlsGeneratorShuffleSimilarity, ChromiumLinuxReviewedCorpusHasMultipleObservedTemplates) {
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->set_catalog.observed_extension_order_templates.size() > 1u);
}

TEST(TlsGeneratorShuffleSimilarity, Chrome133GeneratedOrdersAreLegalAndUseReviewedExtensionSet) {
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_FALSE(baseline->invariants.non_grease_extension_set.empty());

  const auto &verifier = ExtensionOrderVerifier::get_for_family(Slice("chromium_linux_desktop"));
  std::set<std::string> distinct_orders;
  for (td::uint64 seed = 0; seed < 512; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto order = non_grease_extension_sequence(parsed.ok_ref());
    ASSERT_TRUE(verifier.is_legal_permutation(order));

    auto observed_set = extension_set_non_grease_no_padding(parsed.ok_ref());
    std::vector<td::uint16> observed(observed_set.begin(), observed_set.end());
    std::sort(observed.begin(), observed.end());
    auto expected = baseline->invariants.non_grease_extension_set;
    std::sort(expected.begin(), expected.end());
    ASSERT_EQ(expected, observed);
    distinct_orders.insert(order_key(order));
  }
  ASSERT_TRUE(distinct_orders.size() >= 256u);
}

TEST(TlsGeneratorShuffleSimilarity, FixedOrderFamiliesMatchReviewedTemplateInsteadOfShufflePolicy) {
  const auto *baseline = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_EQ(1u, baseline->set_catalog.observed_extension_order_templates.size());

  std::set<std::string> distinct_orders;
  for (td::uint64 seed = 0; seed < 64; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto order = non_grease_extension_sequence(parsed.ok_ref());
    ASSERT_EQ(baseline->set_catalog.observed_extension_order_templates[0], order);
    distinct_orders.insert(order_key(order));
  }
  ASSERT_EQ(1u, distinct_orders.size());
}

}  // namespace
```

- [ ] **Step 2: Register and run**

Add to `test/CMakeLists.txt`:

```cmake
stealth/test_tls_generator_shuffle_similarity.cpp
```

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsGeneratorShuffleSimilarity
```

Expected: failure until baseline extension set status is populated for Chromium Linux.

- [ ] **Step 3: Fix oracle generation**

Ensure Chromium `non_grease_extension_set` is `Exact` when all reviewed samples have the same set and variable order.

- [ ] **Step 4: Run and commit**

Run:

```bash
./build/test/run_all_tests --filter TlsGeneratorShuffleSimilarity
```

Expected:

```text
OK
```

Commit:

```bash
git add test/stealth/test_tls_generator_shuffle_similarity.cpp test/CMakeLists.txt test/analysis/build_family_lane_baselines.py test/stealth/ReviewedFamilyLaneBaselines.h
git commit -m "test: gate Chrome shuffle against reviewed corpus policy"
```

## Task 9: Replace Early Returns In Multi-Dump Tests

**Files:**
- Modify: `test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp`
- Modify: `test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp`

- [ ] **Step 1: Write a source-scanning contract**

Add this test to `test/analysis/test_similarity_release_gate_contract.py`:

```python
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class SimilarityReleaseGateContract(unittest.TestCase):
    def test_release_similarity_tests_do_not_return_on_empty_baselines(self) -> None:
        checked_files = [
            REPO_ROOT / "test" / "stealth" / "test_tls_multi_dump_windows_chrome_stats.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_multi_dump_ios_apple_tls_stats.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_generator_fixture_exact_fields_gate.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_generator_wire_length_fixture_gate.cpp",
        ]
        offenders: list[str] = []
        for path in checked_files:
            if not path.exists():
                continue
            text = path.read_text(encoding="utf-8")
            if "return;  // Corpus not yet reviewed" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
            if "return;  // Linux baseline not yet populated" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
            if "Baseline review still in progress" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)
```

- [ ] **Step 2: Run and verify failure**

Run:

```bash
python3 -m unittest test.analysis.test_similarity_release_gate_contract -v
```

Expected failure includes:

```text
test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp
```

- [ ] **Step 3: Replace returns with status assertions**

Replace early-return logic like:

```cpp
if (baseline->invariants.non_grease_cipher_suites_ordered.empty()) {
  return;
}
```

with:

```cpp
ASSERT_NE(EvidenceFieldStatus::Unavailable, baseline->non_grease_cipher_suites_status);
ASSERT_NE(EvidenceFieldStatus::Mixed, baseline->non_grease_cipher_suites_status);
```

Replace wire-length empty checks like:

```cpp
if (baseline->set_catalog.observed_wire_lengths.empty()) {
  return;
}
```

with:

```cpp
ASSERT_NE(EvidenceFieldStatus::Unavailable, baseline->wire_lengths_status);
ASSERT_FALSE(baseline->set_catalog.observed_wire_lengths.empty());
```

- [ ] **Step 4: Run Python and C++ checks**

Run:

```bash
python3 -m unittest test.analysis.test_similarity_release_gate_contract -v
./build/test/run_all_tests --filter 'TLS_MultiDumpWindowsChromeStats|TLS_MultiDumpIosAppleTlsStats'
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add test/analysis/test_similarity_release_gate_contract.py test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp test/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp
git commit -m "test: remove silent skips from release similarity tests"
```

## Task 10: Update Documentation And Release Evidence Language

**Files:**
- Modify: `docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md`
- Modify: `docs/Documentation/Lessons_Learnt.md`
- Modify if present: operator-facing release evidence docs that mention statistical corpus gates.

- [ ] **Step 1: Write documentation contract**

Add to `test/analysis/test_similarity_release_gate_contract.py`:

```python
    def test_docs_separate_similarity_gates_from_seed_stress(self) -> None:
        pipeline = (REPO_ROOT / "docs" / "Documentation" / "FINGERPRINT_GENERATION_PIPELINE.md").read_text(
            encoding="utf-8"
        )
        lessons = (REPO_ROOT / "docs" / "Documentation" / "Lessons_Learnt.md").read_text(encoding="utf-8")
        combined = pipeline + "\n" + lessons

        self.assertIn("real-corpus similarity gate", combined)
        self.assertIn("seed-stress diagnostic", combined)
        self.assertIn("self-calibrated generator tests are not real-browser similarity evidence", combined)
```

- [ ] **Step 2: Run and verify failure**

Run:

```bash
python3 -m unittest test.analysis.test_similarity_release_gate_contract -v
```

Expected failure:

```text
'real-corpus similarity gate' not found
```

- [ ] **Step 3: Update docs with exact language**

Add this paragraph to `docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md` under validation topology:

```markdown
### Real-Corpus Similarity Gates vs Seed-Stress Diagnostics

A real-corpus similarity gate compares generated TLS ClientHello fields against reviewed browser-capture evidence for the same `(family_id, cohort_id, route_lane, evidence_lane)`. These gates consume `test/analysis/fixtures/clienthello/` through generated reviewed baselines and fail closed when exact release-critical evidence is unavailable or mixed.

A seed-stress diagnostic exercises runtime variability across deterministic seeds. Seed-stress diagnostics are valuable for detecting degenerate RNG behavior, duplicate wire images, weak GREASE diversity, and pinned shuffle positions, but generated seeds are not independent browser evidence and may not be used as release-facing denominators.

Self-calibrated generator tests are not real-browser similarity evidence. A test that derives expected wire lengths, extension counts, or envelopes from the generator under test can only prove internal stability.
```

Add this paragraph to `docs/Documentation/Lessons_Learnt.md`:

```markdown
## Real-Corpus Similarity Evidence

Self-calibrated generator tests are not real-browser similarity evidence. Release-facing fingerprint claims must use reviewed fixture evidence from real packet captures, disclose the cohort denominator, and fail closed when exact release-critical fields are unavailable or mixed. Seed-stress diagnostics remain useful, but they prove generator diversity and stability rather than similarity to browser dumps.
```

- [ ] **Step 4: Run docs contract**

Run:

```bash
python3 -m unittest test.analysis.test_similarity_release_gate_contract -v
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md docs/Documentation/Lessons_Learnt.md test/analysis/test_similarity_release_gate_contract.py
git commit -m "docs: distinguish real-corpus gates from seed-stress diagnostics"
```

## Task 11: Full Verification

**Files:**
- No new files.
- Verify all touched Python, C++, generated, CMake, and docs changes.

- [ ] **Step 1: Configure**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
```

Expected:

```text
-- Build files have been written to: <repo>/build
```

- [ ] **Step 2: Build**

Run:

```bash
cmake --build build --target run_all_tests --parallel 4
```

Expected:

```text
[100%] Built target run_all_tests
```

- [ ] **Step 3: Run Python analysis tests**

Run:

```bash
python3 -m unittest \
  test.analysis.test_family_lane_oracle_generation \
  test.analysis.test_corpus_iteration_tier_naming_contract \
  test.analysis.test_similarity_release_gate_contract \
  -v
```

Expected:

```text
OK
```

- [ ] **Step 4: Run focused C++ similarity tests**

Run:

```bash
./build/test/run_all_tests --filter 'TlsReleaseSimilarityUnavailableFailClosed|TlsGeneratorFixtureExactFieldsGate|TlsGeneratorExtensionCountSimilarity|TlsGeneratorWireLengthFixtureGate|TlsGeneratorShuffleSimilarity'
```

Expected:

```text
OK
```

- [ ] **Step 5: Run legacy corpus slice**

Run:

```bash
./build/test/run_all_tests --filter 'ChromeCorpusExtensionSet|ChromeGreaseUniformity|ChromePermutationPosition|TLS_MultiDumpWindowsChromeStats|TLS_MultiDumpIosAppleTlsStats'
```

Expected:

```text
OK
```

- [ ] **Step 6: Run full nightly corpus gate**

Run:

```bash
TD_NIGHTLY_CORPUS=1 ./build/test/run_all_tests --filter 'TlsGenerator|ChromeCorpus|TLS_Nightly|TLS_MultiDump'
```

Expected:

```text
OK
```

If this command exceeds local time budget, record the timeout and run the narrower full-tier tests that touch the changed files. Do not claim full nightly evidence unless it completed.

- [ ] **Step 7: Run full CTest only after focused tests pass**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed
```

- [ ] **Step 8: Capture final status**

Run:

```bash
git status --short
```

Expected: only intentional files are modified.

- [ ] **Step 9: Commit**

If any verification-only doc or generated-artifact update remains uncommitted:

```bash
git add docs test
git commit -m "test: harden stealth corpus similarity gates"
```

## Task 12: Final Agent Gate Artifact

**Files:**
- Create: `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/stealth_corpus_similarity_<agent>_<date>.json`

- [ ] **Step 1: Write closeout artifact**

Create a JSON artifact with this exact shape:

```json
{
  "phase_id": "stealth-corpus-real-dump-similarity",
  "status": "pass",
  "timestamp_utc": "YYYY-MM-DDTHH:MM:SSZ",
  "plan_ref": "docs/Plans/STEALTH_CORPUS_REAL_DUMP_SIMILARITY_TEST_PLAN_2026-06-08.md",
  "summary": {
    "real_corpus_similarity_gates": [
      "TlsReleaseSimilarityUnavailableFailClosed",
      "TlsGeneratorFixtureExactFieldsGate",
      "TlsGeneratorExtensionCountSimilarity",
      "TlsGeneratorWireLengthFixtureGate",
      "TlsGeneratorShuffleSimilarity"
    ],
    "seed_stress_diagnostics_reclassified": [
      "TLS_NightlyWireBaselineMonteCarlo"
    ],
    "release_evidence_rule": "Generated seeds are runtime stress inputs, not independent browser evidence."
  },
  "commands": [
    {
      "command": "python3 -m unittest test.analysis.test_family_lane_oracle_generation test.analysis.test_corpus_iteration_tier_naming_contract test.analysis.test_similarity_release_gate_contract -v",
      "status": "pass"
    },
    {
      "command": "./build/test/run_all_tests --filter 'TlsReleaseSimilarityUnavailableFailClosed|TlsGeneratorFixtureExactFieldsGate|TlsGeneratorExtensionCountSimilarity|TlsGeneratorWireLengthFixtureGate|TlsGeneratorShuffleSimilarity'",
      "status": "pass"
    },
    {
      "command": "ctest --test-dir build --output-on-failure",
      "status": "pass"
    }
  ],
  "changed_files": [
    "test/analysis/test_family_lane_oracle_generation.py",
    "test/analysis/test_corpus_iteration_tier_naming_contract.py",
    "test/analysis/test_similarity_release_gate_contract.py",
    "test/analysis/build_family_lane_baselines.py",
    "test/stealth/ReviewedFamilyLaneBaselines.h",
    "test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp",
    "test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp",
    "test/stealth/test_tls_generator_extension_count_similarity.cpp",
    "test/stealth/test_tls_generator_wire_length_fixture_gate.cpp",
    "test/stealth/test_tls_generator_shuffle_similarity.cpp",
    "test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp",
    "test/CMakeLists.txt",
    "docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md",
    "docs/Documentation/Lessons_Learnt.md"
  ],
  "residual_risks": [
    "Statistical distribution tests remain limited by reviewed corpus size.",
    "Imported fixtures remain diagnostic until promoted through reviewed-lane provenance rules."
  ]
}
```

Replace `YYYY-MM-DDTHH:MM:SSZ` and `<agent>` in the filename with actual values. Do not omit failed, skipped, timed-out, or unavailable commands; record them honestly.

- [ ] **Step 2: Commit**

```bash
git add docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs
git commit -m "docs: add stealth corpus similarity closeout evidence"
```

## Success Criteria

The implementation is complete only when all of these are true:

1. Tests named or documented as `1k` actually run 1024 iterations in the full/nightly lane or are renamed.
2. Real-corpus similarity tests consume reviewed fixture-derived baseline data.
3. No release-facing similarity test self-calibrates expected values from generated output.
4. No release-facing similarity test returns early because exact reviewed fields are empty.
5. Extension-count, extension-set, ECH payload, ALPS, cipher, supported-groups, supported-versions, and wire-length gates are field-status aware.
6. `mixed` and `unavailable` field statuses fail release-facing gates.
7. Seed-stress diagnostics remain available but are documented as diagnostics.
8. Full focused verification passes.
9. A closeout handoff artifact records exact commands and results.

## Explicit Non-Goals

1. Do not collect new packet captures in this plan.
2. Do not promote imported fixtures to reviewed evidence.
3. Do not weaken existing adversarial GREASE, shuffle, JA3, JA4, fuzz, or stress tests.
4. Do not change runtime generator behavior before a failing fixture-derived test proves the defect.
5. Do not make broad product claims such as "statistically indistinguishable" unless the artifact includes denominator, cohort, method, and unavailable fields.

## Suggested Implementation Order

1. Task 1: Python oracle contracts.
2. Task 2: iteration-tier naming truth.
3. Task 3: generated C++ field availability.
4. Task 4: fail-closed unavailable gates.
5. Task 5: exact-field generator gates.
6. Task 6: extension-count gates.
7. Task 7: wire-length gates.
8. Task 8: shuffle similarity gates.
9. Task 9: remove release-test early returns.
10. Task 10: docs and release evidence language.
11. Task 11: full verification.
12. Task 12: closeout artifact.

Do not batch Tasks 1-4 into one commit. Those tasks define the evidence model and should be reviewable independently.
