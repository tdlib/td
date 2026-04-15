# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: fixture intake must fail closed on malformed input.

Validates that the ClientHello fixture loader rejects:
  * malformed JSON
  * missing required fields (profile_id, samples, parser_version)
  * wrong / unexpected artifact_type or parser_version
  * route_mode drifted from the canonical allow-list
  * device_class / os_family tags outside the canonical allow-list

When the loader does not enforce a constraint, the relevant test FAILS. A failing
test here means a real gap in the loader for David to review; do not weaken the
test to make it green.
"""

from __future__ import annotations

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
    """Construct a minimal artifact that the current loader accepts."""
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


def write_artifact(payload: object) -> pathlib.Path:
    handle = tempfile.NamedTemporaryFile(  # noqa: SIM115 - deliberate non-context creation for the tmp file
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    try:
        if isinstance(payload, (dict, list)):
            json.dump(payload, handle)
        else:
            handle.write(str(payload))
    finally:
        handle.close()
    return pathlib.Path(handle.name)


class FixtureIntakeFailClosedTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp_paths: list[pathlib.Path] = []

    def tearDown(self) -> None:
        for path in self._tmp_paths:
            try:
                path.unlink(missing_ok=True)
            except OSError:
                pass

    def _write(self, payload: object) -> pathlib.Path:
        path = write_artifact(payload)
        self._tmp_paths.append(path)
        return path

    def test_malformed_json_is_rejected(self) -> None:
        path = self._write("{ this is not valid json")
        with self.assertRaises((ValueError, json.JSONDecodeError)):
            load_clienthello_artifact(path)

    def test_baseline_minimal_artifact_is_accepted(self) -> None:
        # If the baseline is not accepted, something is misaligned with the loader
        # contract and every downstream failure mode assertion becomes ambiguous.
        path = self._write(minimal_valid_artifact())
        samples = load_clienthello_artifact(path)
        self.assertEqual(1, len(samples))

    def test_missing_profile_id_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        del payload["profile_id"]
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_missing_samples_list_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        del payload["samples"]
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_empty_samples_list_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["samples"] = []
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_samples_not_a_list_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["samples"] = {"frame1": "not a list"}
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_unknown_route_mode_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["route_mode"] = "mars_egress"
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_forbidden_device_class_tag_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["device_class"] = "toaster"
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_forbidden_os_family_tag_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["os_family"] = "beos"
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_wrong_artifact_type_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["artifact_type"] = "some_other_schema"
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)

    def test_wrong_parser_version_is_rejected(self) -> None:
        payload = minimal_valid_artifact()
        payload["parser_version"] = "tls-clienthello-parser-v0-legacy"
        path = self._write(payload)
        with self.assertRaises(ValueError):
            load_clienthello_artifact(path)


if __name__ == "__main__":
    unittest.main()
