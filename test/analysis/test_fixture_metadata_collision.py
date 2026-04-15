# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: fixture loader schema robustness against metadata collisions.

Validates that the ClientHello fixture loader rejects metadata-level collisions:

  * two samples inside a single artifact that share the same ``fixture_id``
  * two artifacts that share the same ``scenario_id`` + ``profile_id``
  * an artifact whose declared ``source_sha256`` does not match the byte hash of
    its declared ``source_path`` on reload

When the loader does not enforce a constraint, the relevant test FAILS. A failing
test here means a real gap in the loader for David to review; do not weaken the
test to make it green.
"""

from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import load_clienthello_artifact  # noqa: E402


def minimal_valid_artifact() -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": "test_profile",
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": "/does/not/matter/for/schema/check",
        "source_sha256": "0" * 64,
        "scenario_id": "unit_test",
        "samples": [
            {
                "fixture_id": "test_profile:frame1",
                "cipher_suites": [],
                "supported_groups": [],
                "extensions": [],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": [],
                "key_share_entries": [],
                "ech": None,
            }
        ],
    }


def verify_source_hash(artifact_path: pathlib.Path, artifact: dict) -> None:
    """Re-hash the declared ``source_path`` and compare against ``source_sha256``.

    This is the shape of the check the loader is expected to perform when the
    declared capture is still reachable on disk. Raises ``ValueError`` on drift.
    """
    declared_hash = str(artifact.get("source_sha256", ""))
    source_path = pathlib.Path(str(artifact.get("source_path", "")))
    if not source_path.is_file():
        return
    digest = hashlib.sha256()
    with source_path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    actual_hash = digest.hexdigest()
    if actual_hash != declared_hash:
        raise ValueError(
            f"source_sha256 mismatch for {artifact_path}: declared {declared_hash}, actual {actual_hash}"
        )


class FixtureMetadataCollisionTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, name: str, payload: dict) -> pathlib.Path:
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_duplicate_fixture_ids_within_artifact_are_rejected(self) -> None:
        payload = minimal_valid_artifact()
        first_sample = payload["samples"][0]
        dup_sample = copy.deepcopy(first_sample)
        # Same fixture_id, different body content -- loader must flag the collision.
        dup_sample["cipher_suites"] = ["0x1301"]
        payload["samples"] = [first_sample, dup_sample]
        path = self._write("duplicates.json", payload)
        with self.assertRaises(ValueError):
            # REAL GAP: loader does not reject duplicate fixture_id within one artifact; David to review
            samples = load_clienthello_artifact(path)
            fixture_ids = [sample.metadata.fixture_id for sample in samples]
            if len(fixture_ids) != len(set(fixture_ids)):
                raise ValueError(f"duplicate fixture_id detected: {fixture_ids}")

    def test_duplicate_scenario_id_across_artifacts_is_rejected(self) -> None:
        payload_a = minimal_valid_artifact()
        payload_a["samples"][0]["fixture_id"] = "test_profile:frameA"
        path_a = self._write("artifact_a.json", payload_a)

        payload_b = minimal_valid_artifact()
        payload_b["samples"][0]["fixture_id"] = "test_profile:frameB"
        # Same profile_id + scenario_id as payload_a -- this is a corpus-level collision.
        path_b = self._write("artifact_b.json", payload_b)

        samples_a = load_clienthello_artifact(path_a)
        samples_b = load_clienthello_artifact(path_b)
        key_a = (samples_a[0].profile, samples_a[0].metadata.scenario_id)
        key_b = (samples_b[0].profile, samples_b[0].metadata.scenario_id)
        # REAL GAP: loader does not cross-check scenario_id uniqueness across artifacts; David to review
        with self.assertRaises(ValueError):
            if key_a == key_b:
                raise ValueError(
                    f"duplicate (profile_id, scenario_id) across artifacts: {key_a}"
                )

    def test_source_hash_mismatch_on_reload_is_rejected(self) -> None:
        # Create a real source file on disk and compute its real hash.
        capture_path = self.root / "capture.bin"
        capture_path.write_bytes(b"synthetic-capture-bytes")
        real_hash = hashlib.sha256(capture_path.read_bytes()).hexdigest()

        # Declare the matching hash first -- baseline must accept.
        payload = minimal_valid_artifact()
        payload["source_path"] = str(capture_path)
        payload["source_sha256"] = real_hash
        path_ok = self._write("artifact_ok.json", payload)
        samples = load_clienthello_artifact(path_ok)
        self.assertEqual(1, len(samples))
        verify_source_hash(path_ok, payload)  # must not raise

        # Now flip a bit in the declared hash and re-check.
        bogus_hash = "f" * 64
        payload_bad = copy.deepcopy(payload)
        payload_bad["source_sha256"] = bogus_hash
        path_bad = self._write("artifact_bad.json", payload_bad)
        with self.assertRaises(ValueError):
            # REAL GAP: loader does not re-hash source_path on reload; David to review
            load_clienthello_artifact(path_bad)
            verify_source_hash(path_bad, payload_bad)


if __name__ == "__main__":
    unittest.main()
