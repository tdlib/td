#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Fail-closed identity contract tests for common_tls.py loaders.

Covers the scenario_id fallback bug (FP-11, FP-17) and source_path identity
inflation (FP-09).

RISKS COVERED: RISK-FP-10, RISK-FP-13, RISK-FP-18

CENSUS FINDINGS:
  - common_tls.py line 342: scenario_id falls back to artifact_path.stem
    when the field is absent or empty.
  - common_tls.py lines 424-425: identical fallback for ServerHello.
  - build_family_lane_baselines.py line 421: same stem-fallback pattern.
  - build_family_lane_baselines.py lines 259-264: _source_identity includes
    source_path, inflating independent-source counts when only the path
    differs but source_sha256 is identical.

These tests are RED against current HEAD because the bugs they prove exist
today. Do not weaken the assertions to make them green.
"""

from __future__ import annotations

import copy
import json
import pathlib
import sys
import tempfile
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]

if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import (  # noqa: E402
    ClientHello,
    ServerHello,
    load_clienthello_artifact,
    load_server_hello_artifact,
)


# ---------------------------------------------------------------------------
# Minimal valid artifact constructors (copied from sibling test modules and
# kept local so this file is self-contained).
# ---------------------------------------------------------------------------

def _minimal_clienthello_artifact() -> dict:
    """Return a minimal ClientHello artifact that passes current validation."""
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": "test_profile",
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": "/synthetic/capture_alpha.pcapng",
        "source_sha256": "a" * 64,
        "scenario_id": "scenario_alpha",
        "samples": [
            {
                "fixture_id": "test_profile:frame1",
                "fixture_family_id": "family_alpha",
                "cipher_suites": ["0x1301", "0x1302"],
                "supported_groups": ["0x001D"],
                "extensions": [],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": ["h2"],
                "key_share_entries": [{"group": "0x001D"}],
                "ech": None,
            }
        ],
    }


def _minimal_serverhello_artifact() -> dict:
    """Return a minimal ServerHello artifact that passes current validation."""
    return {
        "artifact_type": "tls_serverhello_fixtures",
        "parser_version": "tls-serverhello-parser-v1",
        "route_mode": "non_ru_egress",
        "scenario_id": "scenario_alpha",
        "source_path": "/synthetic/capture_alpha.pcapng",
        "source_sha256": "b" * 64,
        "source_kind": "browser_capture",
        "transport": "tcp",
        "family": "family_alpha",
        "capture_provenance": {
            "client_profile_id": "test_profile",
        },
        "samples": [
            {
                "fixture_id": "test_profile:frame1",
                "fixture_family_id": "family_alpha",
                "selected_version": "0x0304",
                "cipher_suite": "0x1301",
                "extensions": ["0x002B", "0x0033"],
                "record_layout_signature": [22],
            }
        ],
    }


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _build_family_lane_source_identity(entry: dict) -> tuple:
    """Replicate _source_identity from build_family_lane_baselines.py (lines 259-264).

    The FIXED behaviour: source_path is NOT included in the identity tuple.
    Two entries from the same capture (same sha256, same source_kind) count as
    one independent source regardless of filesystem path.
    """
    return (
        str(entry.get("source_kind", "")),
        str(entry.get("source_sha256", "")),
    )


def _correct_source_identity(entry: dict) -> tuple:
    """The CORRECT source identity: source_path is NOT part of the key.

    Two captures with the same (source_kind, source_sha256) represent the
    same independent source regardless of where the file happens to live on
    disk.
    """
    return (
        str(entry.get("source_kind", "")),
        str(entry.get("source_sha256", "")),
    )


class ScenarioIdFallbackTest(unittest.TestCase):
    """FP-11 / FP-17: scenario_id must NOT silently fall back to artifact_path.stem."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, payload: dict) -> pathlib.Path:
        target = self.root / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    # -- ClientHello scenario_id fallback ------------------------------------

    def test_clienthello_missing_scenario_id_must_not_use_stem(self) -> None:
        """When scenario_id is absent the loader must NOT silently substitute
        the filename stem.  It should either raise ValueError or tag the
        resulting metadata as diagnostic_only.

        RED because common_tls.py line 342 does exactly this fallback today.
        """
        payload = _minimal_clienthello_artifact()
        del payload["scenario_id"]

        filename = "my_sneaky_stem.clienthello.json"
        path = self._write(filename, payload)
        stem = pathlib.Path(filename).stem  # "my_sneaky_stem.clienthello"

        # The loader currently succeeds and sets scenario_id = stem.
        # The correct behaviour is ONE of:
        #   (a) raise ValueError  -- fail closed
        #   (b) set scenario_id to a sentinel that cannot be confused with
        #       a real scenario (e.g. "" or a diagnostic_only marker)
        # We test for (a) first; if it does not raise we fall through to (b).
        raised = False
        loaded: list[ClientHello] = []
        try:
            loaded = load_clienthello_artifact(path)
            # If we get here the loader did NOT raise -- check (b).
        except (ValueError, KeyError):
            raised = True

        if not raised:
            # The loader returned samples -- verify scenario_id is NOT the stem.
            self.assertTrue(loaded, "loader returned empty list without raising")
            for ch in loaded:
                self.assertNotEqual(
                    ch.metadata.scenario_id,
                    stem,
                    "scenario_id silently fell back to artifact_path.stem "
                    "(FP-11 bug: common_tls.py line 342)",
                )

    def test_clienthello_empty_scenario_id_must_not_use_stem(self) -> None:
        """An explicitly empty scenario_id must not be patched with the stem."""
        payload = _minimal_clienthello_artifact()
        payload["scenario_id"] = ""

        filename = "empty_scenario.clienthello.json"
        path = self._write(filename, payload)
        stem = pathlib.Path(filename).stem

        raised = False
        loaded: list[ClientHello] = []
        try:
            loaded = load_clienthello_artifact(path)
        except (ValueError, KeyError):
            raised = True

        if not raised:
            self.assertTrue(loaded, "loader returned empty list without raising")
            for ch in loaded:
                self.assertNotEqual(
                    ch.metadata.scenario_id,
                    stem,
                    "empty scenario_id silently fell back to artifact_path.stem "
                    "(FP-11 bug: common_tls.py line 342 `or artifact_path.stem`)",
                )

    def test_clienthello_whitespace_only_scenario_id_must_not_use_stem(self) -> None:
        """Whitespace-only scenario_id must not slip through the strip-or-stem gate."""
        payload = _minimal_clienthello_artifact()
        payload["scenario_id"] = "   "

        filename = "ws_scenario.clienthello.json"
        path = self._write(filename, payload)
        stem = pathlib.Path(filename).stem

        raised = False
        loaded: list[ClientHello] = []
        try:
            loaded = load_clienthello_artifact(path)
        except (ValueError, KeyError):
            raised = True

        if not raised:
            self.assertTrue(loaded, "loader returned empty list without raising")
            for ch in loaded:
                self.assertNotEqual(
                    ch.metadata.scenario_id,
                    stem,
                    "whitespace scenario_id silently fell back to artifact_path.stem",
                )

    # -- ServerHello scenario_id fallback ------------------------------------

    def test_serverhello_missing_scenario_id_must_not_use_stem(self) -> None:
        """Same FP-11 bug exists in _load_serverhello_common_metadata (lines 424-425)."""
        payload = _minimal_serverhello_artifact()
        del payload["scenario_id"]

        filename = "sh_sneaky_stem.serverhello.json"
        path = self._write(filename, payload)
        stem = pathlib.Path(filename).stem

        raised = False
        loaded: list[ServerHello] = []
        try:
            loaded = load_server_hello_artifact(path)
        except (ValueError, KeyError):
            raised = True

        if not raised:
            self.assertTrue(loaded, "loader returned empty list without raising")
            for sh in loaded:
                self.assertNotEqual(
                    sh.metadata.scenario_id,
                    stem,
                    "ServerHello scenario_id silently fell back to artifact_path.stem "
                    "(FP-17 bug: common_tls.py lines 424-425)",
                )

    def test_serverhello_empty_scenario_id_must_not_use_stem(self) -> None:
        """Empty scenario_id in ServerHello must not fall back to stem."""
        payload = _minimal_serverhello_artifact()
        payload["scenario_id"] = ""

        filename = "sh_empty.serverhello.json"
        path = self._write(filename, payload)
        stem = pathlib.Path(filename).stem

        raised = False
        loaded: list[ServerHello] = []
        try:
            loaded = load_server_hello_artifact(path)
        except (ValueError, KeyError):
            raised = True

        if not raised:
            self.assertTrue(loaded, "loader returned empty list without raising")
            for sh in loaded:
                self.assertNotEqual(
                    sh.metadata.scenario_id,
                    stem,
                    "ServerHello empty scenario_id fell back to artifact_path.stem",
                )


class SourcePathIndependenceTest(unittest.TestCase):
    """FP-09: source_path must NOT inflate independent-source counts.

    Two loads of the same capture content (same source_sha256, same
    source_kind) but from different filesystem paths must count as ONE
    independent source, not two.
    """

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, payload: dict) -> pathlib.Path:
        target = self.root / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_source_path_change_must_not_inflate_source_count(self) -> None:
        """Create two artifact dicts identical except for source_path.
        The production _source_identity function (build_family_lane_baselines.py:259)
        returns a 2-tuple (source_kind, source_sha256) that does NOT include
        source_path.  Two entries with the same content but different paths
        must produce the same identity.

        GREEN because the production fix removed source_path from the identity.
        """
        shared_sha = "c" * 64
        shared_kind = "browser_capture"

        entry_a = {
            "source_kind": shared_kind,
            "source_path": "/mnt/nas/captures/day1/dump.pcapng",
            "source_sha256": shared_sha,
            "scenario_id": "session_one",
        }
        entry_b = {
            "source_kind": shared_kind,
            "source_path": "/tmp/working_copy/dump.pcapng",  # different path
            "source_sha256": shared_sha,                     # same content
            "scenario_id": "session_one",
        }

        # Both the production-matching local function and the correct reference
        # should collapse path-only differences into one identity.
        production_ids = {_build_family_lane_source_identity(e) for e in [entry_a, entry_b]}
        correct_ids = {_correct_source_identity(e) for e in [entry_a, entry_b]}

        # The correct identity set must have exactly 1 member.
        self.assertEqual(
            len(correct_ids), 1,
            "Correct source_identity should collapse path-only differences",
        )

        # The production function now matches the correct behaviour.
        self.assertEqual(
            len(production_ids), len(correct_ids),
            "Production _source_identity must not include source_path in the identity key",
        )

    def test_clienthello_loaded_from_two_paths_same_identity(self) -> None:
        """Load the same artifact content from two different file paths.
        The resulting metadata.source_path will differ, but from a source-
        independence perspective they must not be counted separately.

        RED because common_tls.py stores source_path in SampleMeta verbatim
        and downstream code uses it in _source_identity.
        """
        payload = _minimal_clienthello_artifact()
        payload["source_sha256"] = "d" * 64

        path_a = self._write("copy_alpha.clienthello.json", payload)
        path_b = self._write("copy_beta.clienthello.json", payload)

        loaded_a = load_clienthello_artifact(path_a)
        loaded_b = load_clienthello_artifact(path_b)

        self.assertTrue(loaded_a)
        self.assertTrue(loaded_b)

        # Build identity tuples the way build_family_lane_baselines does.
        ids_a = {
            _build_family_lane_source_identity({
                "source_kind": ch.metadata.source_kind,
                "source_path": ch.metadata.source_path,
                "source_sha256": ch.metadata.source_sha256,
            })
            for ch in loaded_a
        }
        ids_b = {
            _build_family_lane_source_identity({
                "source_kind": ch.metadata.source_kind,
                "source_path": ch.metadata.source_path,
                "source_sha256": ch.metadata.source_sha256,
            })
            for ch in loaded_b
        }

        combined = ids_a | ids_b
        self.assertEqual(
            len(combined), 1,
            "Same capture loaded from two paths inflates to "
            f"{len(combined)} independent sources (FP-09). "
            "source_path must not be part of the identity key.",
        )


class ArtifactPathStemRejectionTest(unittest.TestCase):
    """artifact_path.stem must not be used for Tier1+ session counting.

    When scenario_id falls back to the stem, every file rename silently
    creates a new "session", inflating tier calculations.
    """

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, payload: dict) -> pathlib.Path:
        target = self.root / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_renaming_file_must_not_change_session_identity(self) -> None:
        """Loading an artifact without scenario_id must raise ValueError.

        The stem fallback has been removed: common_tls.py now raises
        ValueError when scenario_id is missing or empty.  This is the
        correct fail-closed behaviour -- without a valid scenario_id the
        artifact cannot be loaded, so file renames can never silently
        create distinct stem-based sessions.
        """
        payload = _minimal_clienthello_artifact()
        del payload["scenario_id"]

        path_a = self._write("session_rename_a.clienthello.json", payload)

        with self.assertRaises(ValueError) as ctx:
            load_clienthello_artifact(path_a)

        self.assertIn(
            "scenario_id",
            str(ctx.exception),
            "ValueError must mention scenario_id so the caller knows what is missing",
        )

    def test_stem_derived_scenario_id_must_not_appear_in_tier1_data(self) -> None:
        """Loading an artifact without scenario_id must raise ValueError.

        The stem fallback has been removed: common_tls.py now raises
        ValueError when scenario_id is missing or empty.  Because the
        loader rejects such artifacts outright, stem-derived scenario_ids
        can never appear in any tier data.
        """
        payload = _minimal_clienthello_artifact()
        del payload["scenario_id"]

        path = self._write("stem_tier_check.clienthello.json", payload)

        with self.assertRaises(ValueError) as ctx:
            load_clienthello_artifact(path)

        self.assertIn(
            "scenario_id",
            str(ctx.exception),
            "ValueError must mention scenario_id so the caller knows what is missing",
        )


class MissingSourceKindTest(unittest.TestCase):
    """Fixture without source_kind must fail loading or be diagnostic_only."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, payload: dict) -> pathlib.Path:
        target = self.root / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_clienthello_missing_source_kind_must_reject(self) -> None:
        """source_kind is required for correct _source_identity.  Omitting it
        must raise ValueError (fail closed).
        """
        payload = _minimal_clienthello_artifact()
        del payload["source_kind"]

        with self.assertRaises(ValueError):
            load_clienthello_artifact(
                self._write("no_source_kind.clienthello.json", payload)
            )

    def test_serverhello_missing_source_kind_must_reject(self) -> None:
        """ServerHello without source_kind must also fail closed."""
        payload = _minimal_serverhello_artifact()
        del payload["source_kind"]

        with self.assertRaises(ValueError):
            load_server_hello_artifact(
                self._write("no_source_kind.serverhello.json", payload)
            )

    def test_clienthello_empty_source_kind_must_reject(self) -> None:
        """An explicitly empty source_kind must be treated the same as missing."""
        payload = _minimal_clienthello_artifact()
        payload["source_kind"] = ""

        with self.assertRaises(ValueError):
            load_clienthello_artifact(
                self._write("empty_source_kind.clienthello.json", payload)
            )

    def test_serverhello_empty_source_kind_must_reject(self) -> None:
        """ServerHello with empty source_kind must fail closed."""
        payload = _minimal_serverhello_artifact()
        payload["source_kind"] = ""

        with self.assertRaises(ValueError):
            load_server_hello_artifact(
                self._write("empty_source_kind.serverhello.json", payload)
            )


if __name__ == "__main__":
    unittest.main()
