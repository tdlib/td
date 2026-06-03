#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Phase 0D gate -- prove that regenerated fixture fields from raw dumps are
byte-identical to checked-in JSON.

Covers RISK-FP-15, RISK-FP-20, RISK-FP-27.

These are RED tests: they must fail against current HEAD because the
reproduction pipeline they validate does not yet exist.  The tests pin
the *contract* that the pipeline must satisfy once implemented.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"

if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

# ---------------------------------------------------------------------------
# Workspace-prefix rewriting: fixture JSON stores absolute source_path values
# rooted at the CI/dev workspace.  We resolve them to the local repo root.
# ---------------------------------------------------------------------------
_WORKSPACE_PREFIX = "/home/david_osipov/tdlib-obf/"


def _resolve_source_path(source_path: str) -> pathlib.Path:
    """Convert an absolute workspace source_path to a local repo-relative path."""
    if source_path.startswith(_WORKSPACE_PREFIX):
        relative = source_path[len(_WORKSPACE_PREFIX):]
    else:
        relative = source_path
    return REPO_ROOT / relative


def _load_json(path: pathlib.Path) -> dict:
    with path.open("r", encoding="utf-8") as infile:
        return json.load(infile)


def _sha256_of_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as infile:
        while True:
            chunk = infile.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _find_extension_body_hex(artifact: dict, ext_type_hex: str) -> str | None:
    """Return the body_hex of the first sample's extension matching ext_type_hex."""
    for sample in artifact.get("samples", []):
        for ext in sample.get("extensions", []):
            if ext.get("type", "").lower() == ext_type_hex.lower():
                return ext.get("body_hex", "")
    return None


# ---------------------------------------------------------------------------
# Anchor fixture catalogue: the four required iOS fixtures
# ---------------------------------------------------------------------------
_ANCHOR_FIXTURES = {
    "safari26_3_1": {
        "json_path": FIXTURES_ROOT / "ios" / "safari26_3_1_ios26_3_1_a.clienthello.json",
        "expected_0x002b_body_hex": "063a3a03040303",
    },
    "chrome147_0_7727_47": {
        "json_path": FIXTURES_ROOT / "ios" / "chrome147_0_7727_47_ios26_4_a.clienthello.json",
        "expected_0x002b_body_hex": "062a2a03040303",
    },
    "safari17_2": {
        "json_path": FIXTURES_ROOT / "ios" / "safari17_2_ios17_2_1_087f3601.clienthello.json",
        "expected_0x002b_body_hex": "0a3a3a0304030303020301",
    },
    "safari18_7_6": {
        "json_path": FIXTURES_ROOT / "ios" / "safari18_7_6_ios18_7_6.clienthello.json",
        "expected_0x002b_body_hex": "0afafa0304030303020301",
    },
}

_REQUIRED_PROVENANCE_FIELDS = (
    "source_kind",
    "source_sha256",
    "scenario_id",
    "route_mode",
    "parser_version",
)

_EXPECTED_PARSER_VERSION = "tls-clienthello-parser-v1"


class TestAnchorFixtureExistence(unittest.TestCase):
    """RISK-FP-15: the four required iOS anchor fixtures must be checked in."""

    def test_all_four_anchor_fixture_json_files_exist(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            self.assertTrue(
                json_path.is_file(),
                msg=f"anchor fixture missing: {label} -> {json_path}",
            )


class TestRawDumpExistence(unittest.TestCase):
    """RISK-FP-15: raw dump files referenced by each anchor fixture must exist."""

    def test_source_path_raw_dumps_exist_for_all_anchors(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            source_path_str = artifact.get("source_path", "")
            self.assertTrue(
                source_path_str,
                msg=f"{label}: source_path is empty",
            )
            resolved = _resolve_source_path(source_path_str)
            self.assertTrue(
                resolved.is_file(),
                msg=f"{label}: raw dump not found at {resolved}",
            )

    def test_source_paths_are_under_ios_traffic_dumps(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            source_path_str = artifact.get("source_path", "")
            self.assertIn(
                "iOS",
                source_path_str,
                msg=f"{label}: source_path does not reference iOS traffic dumps",
            )


class TestSHA256Match(unittest.TestCase):
    """RISK-FP-20: source_sha256 in each fixture must match the actual SHA256 of
    the raw dump file at source_path.
    """

    def test_source_sha256_matches_actual_file_hash(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            source_path_str = artifact.get("source_path", "")
            recorded_sha256 = artifact.get("source_sha256", "").strip().lower()
            self.assertTrue(
                recorded_sha256,
                msg=f"{label}: source_sha256 is empty",
            )
            resolved = _resolve_source_path(source_path_str)
            if not resolved.is_file():
                self.skipTest(f"raw dump not on disk for {label}")
            actual_sha256 = _sha256_of_file(resolved)
            self.assertEqual(
                recorded_sha256,
                actual_sha256,
                msg=(
                    f"{label}: source_sha256 mismatch -- "
                    f"fixture says {recorded_sha256}, "
                    f"file on disk is {actual_sha256}"
                ),
            )


class TestExtension0x002BBodies(unittest.TestCase):
    """RISK-FP-27: the known 0x002B body_hex values must match what is in the
    checked-in fixture JSON.
    """

    def test_safari26_3_1_supported_versions_body_hex(self) -> None:
        entry = _ANCHOR_FIXTURES["safari26_3_1"]
        if not entry["json_path"].is_file():
            self.skipTest("fixture not on disk")
        artifact = _load_json(entry["json_path"])
        actual = _find_extension_body_hex(artifact, "0x002B")
        self.assertIsNotNone(actual, msg="extension 0x002B not found in safari26_3_1")
        self.assertEqual(actual, entry["expected_0x002b_body_hex"])

    def test_chrome147_0_7727_47_supported_versions_body_hex(self) -> None:
        entry = _ANCHOR_FIXTURES["chrome147_0_7727_47"]
        if not entry["json_path"].is_file():
            self.skipTest("fixture not on disk")
        artifact = _load_json(entry["json_path"])
        actual = _find_extension_body_hex(artifact, "0x002B")
        self.assertIsNotNone(actual, msg="extension 0x002B not found in chrome147")
        self.assertEqual(actual, entry["expected_0x002b_body_hex"])

    def test_safari17_2_supported_versions_body_hex(self) -> None:
        entry = _ANCHOR_FIXTURES["safari17_2"]
        if not entry["json_path"].is_file():
            self.skipTest("fixture not on disk")
        artifact = _load_json(entry["json_path"])
        actual = _find_extension_body_hex(artifact, "0x002B")
        self.assertIsNotNone(actual, msg="extension 0x002B not found in safari17_2")
        self.assertEqual(actual, entry["expected_0x002b_body_hex"])

    def test_safari18_7_6_supported_versions_body_hex(self) -> None:
        entry = _ANCHOR_FIXTURES["safari18_7_6"]
        if not entry["json_path"].is_file():
            self.skipTest("fixture not on disk")
        artifact = _load_json(entry["json_path"])
        actual = _find_extension_body_hex(artifact, "0x002B")
        self.assertIsNotNone(actual, msg="extension 0x002B not found in safari18_7_6")
        self.assertEqual(actual, entry["expected_0x002b_body_hex"])


class TestFrameStreamMetadata(unittest.TestCase):
    """RISK-FP-15: each fixture should have stable frame_number and tcp_stream."""

    def test_all_samples_have_frame_number_and_tcp_stream(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            samples = artifact.get("samples", [])
            self.assertTrue(samples, msg=f"{label}: no samples found")
            for idx, sample in enumerate(samples):
                frame_number = sample.get("frame_number")
                tcp_stream = sample.get("tcp_stream")
                self.assertIsNotNone(
                    frame_number,
                    msg=f"{label} sample[{idx}]: frame_number missing",
                )
                self.assertIsInstance(
                    frame_number,
                    int,
                    msg=f"{label} sample[{idx}]: frame_number must be int",
                )
                self.assertGreater(
                    frame_number,
                    0,
                    msg=f"{label} sample[{idx}]: frame_number must be positive",
                )
                self.assertIsNotNone(
                    tcp_stream,
                    msg=f"{label} sample[{idx}]: tcp_stream missing",
                )
                self.assertIsInstance(
                    tcp_stream,
                    int,
                    msg=f"{label} sample[{idx}]: tcp_stream must be int",
                )
                self.assertGreaterEqual(
                    tcp_stream,
                    0,
                    msg=f"{label} sample[{idx}]: tcp_stream must be non-negative",
                )

    def test_frame_numbers_are_unique_within_each_fixture(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            samples = artifact.get("samples", [])
            frame_numbers = [s.get("frame_number") for s in samples]
            self.assertEqual(
                len(frame_numbers),
                len(set(frame_numbers)),
                msg=f"{label}: duplicate frame_numbers in samples",
            )


class TestParserVersion(unittest.TestCase):
    """RISK-FP-27: parser_version field must be present and match the canonical value."""

    def test_parser_version_is_present_and_correct(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            parser_version = artifact.get("parser_version", "")
            self.assertTrue(
                parser_version,
                msg=f"{label}: parser_version is empty",
            )
            self.assertEqual(
                parser_version,
                _EXPECTED_PARSER_VERSION,
                msg=(
                    f"{label}: parser_version mismatch -- "
                    f"expected {_EXPECTED_PARSER_VERSION}, got {parser_version}"
                ),
            )


class TestProvenanceCompleteness(unittest.TestCase):
    """RISK-FP-20 / RISK-FP-27: all required provenance fields must be
    non-empty strings in every anchor fixture.
    """

    def test_all_required_provenance_fields_present_and_nonempty(self) -> None:
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            for field_name in _REQUIRED_PROVENANCE_FIELDS:
                value = artifact.get(field_name, "")
                self.assertIsInstance(
                    value,
                    str,
                    msg=f"{label}.{field_name}: must be a string",
                )
                self.assertTrue(
                    value.strip(),
                    msg=f"{label}.{field_name}: must be a non-empty string",
                )

    def test_source_sha256_is_valid_hex_digest(self) -> None:
        import re
        sha256_re = re.compile(r"^[0-9a-f]{64}$")
        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            artifact = _load_json(json_path)
            source_sha256 = artifact.get("source_sha256", "").strip().lower()
            self.assertTrue(
                sha256_re.match(source_sha256),
                msg=f"{label}: source_sha256 is not a valid 64-char hex digest",
            )


class TestReproductionPipelineContract(unittest.TestCase):
    """RED TESTS -- these fail until the reproduction pipeline is implemented.

    RISK-FP-15 / RISK-FP-20 / RISK-FP-27: verifying that
    ``reproduce_fixture_from_raw`` exists in common_tls and produces
    byte-identical output to checked-in JSON when given the raw dump.
    """

    def test_reproduce_fixture_from_raw_function_exists(self) -> None:
        """The reproduction function must be importable from common_tls."""
        try:
            from common_tls import reproduce_fixture_from_raw  # noqa: F401
        except ImportError:
            self.fail(
                "common_tls.reproduce_fixture_from_raw does not exist yet -- "
                "RISK-FP-15 reproduction pipeline not implemented"
            )

    def test_reproduced_fixture_matches_checked_in_json_for_all_anchors(self) -> None:
        """Each anchor fixture, when regenerated from its raw dump, must
        produce JSON that is byte-identical (after canonical re-serialization)
        to the checked-in artifact.
        """
        try:
            from common_tls import reproduce_fixture_from_raw
        except ImportError:
            self.fail(
                "common_tls.reproduce_fixture_from_raw does not exist yet -- "
                "cannot run reproduction contract"
            )

        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            checked_in = _load_json(json_path)
            source_path_str = checked_in.get("source_path", "")
            resolved = _resolve_source_path(source_path_str)
            if not resolved.is_file():
                self.skipTest(f"raw dump not on disk for {label}")

            reproduced = reproduce_fixture_from_raw(str(resolved))
            # Canonical comparison: sort keys, indent=2, ensure_ascii=False
            checked_in_canon = json.dumps(
                checked_in, sort_keys=True, indent=2, ensure_ascii=False
            )
            reproduced_canon = json.dumps(
                reproduced, sort_keys=True, indent=2, ensure_ascii=False
            )
            self.assertEqual(
                checked_in_canon,
                reproduced_canon,
                msg=(
                    f"{label}: reproduced fixture differs from checked-in JSON -- "
                    f"RISK-FP-20 byte-identity contract violated"
                ),
            )

    def test_reproduced_extension_0x002b_matches_anchor_values(self) -> None:
        """The reproduction pipeline must produce extension 0x002B body_hex
        values that match the pinned expectations.
        """
        try:
            from common_tls import reproduce_fixture_from_raw
        except ImportError:
            self.fail(
                "common_tls.reproduce_fixture_from_raw does not exist yet -- "
                "cannot validate 0x002B bodies from reproduction"
            )

        for label, entry in _ANCHOR_FIXTURES.items():
            json_path = entry["json_path"]
            if not json_path.is_file():
                self.skipTest(f"fixture {label} not on disk")
            checked_in = _load_json(json_path)
            source_path_str = checked_in.get("source_path", "")
            resolved = _resolve_source_path(source_path_str)
            if not resolved.is_file():
                self.skipTest(f"raw dump not on disk for {label}")

            reproduced = reproduce_fixture_from_raw(str(resolved))
            actual_body_hex = _find_extension_body_hex(reproduced, "0x002B")
            self.assertIsNotNone(
                actual_body_hex,
                msg=f"{label}: reproduced artifact missing extension 0x002B",
            )
            self.assertEqual(
                actual_body_hex,
                entry["expected_0x002b_body_hex"],
                msg=(
                    f"{label}: reproduced 0x002B body_hex mismatch -- "
                    f"expected {entry['expected_0x002b_body_hex']}, got {actual_body_hex}"
                ),
            )


if __name__ == "__main__":
    unittest.main()
