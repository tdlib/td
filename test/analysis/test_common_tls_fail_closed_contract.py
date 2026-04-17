# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract + adversarial checks for fail-closed common TLS artifact loaders.

These tests pin the enforcement boundary in ``common_tls.py`` itself (not only
in higher-level smoke wrappers): malformed metadata, duplicate sample keys,
and schema drift must raise ``ValueError``.
"""

from __future__ import annotations

import copy
import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import load_clienthello_artifact, load_server_hello_artifact  # noqa: E402


def minimal_clienthello_artifact() -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": "test_profile",
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": "/synthetic/capture.pcapng",
        "source_sha256": "1" * 64,
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


def minimal_serverhello_artifact() -> dict:
    return {
        "artifact_type": "tls_serverhello_fixtures",
        "parser_version": "tls-serverhello-parser-v1",
        "route_mode": "non_ru_egress",
        "scenario_id": "scenario_alpha",
        "source_path": "/synthetic/capture.pcapng",
        "source_sha256": "2" * 64,
        "source_kind": "browser_capture",
        "transport": "tcp",
        "family": "family_alpha",
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


class CommonTlsFailClosedContractTest(unittest.TestCase):
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

    def test_clienthello_rejects_duplicate_fixture_ids(self) -> None:
        payload = minimal_clienthello_artifact()
        duplicate = copy.deepcopy(payload["samples"][0])
        duplicate["cipher_suites"] = ["0x1303"]
        payload["samples"].append(duplicate)

        with self.assertRaises(ValueError):
            load_clienthello_artifact(self._write("dup.clienthello.json", payload))

    def test_clienthello_rejects_missing_sample_fixture_id(self) -> None:
        payload = minimal_clienthello_artifact()
        del payload["samples"][0]["fixture_id"]

        with self.assertRaises(ValueError):
            load_clienthello_artifact(self._write("missing_fixture_id.clienthello.json", payload))

    def test_clienthello_rejects_noncanonical_source_sha256(self) -> None:
        payload = minimal_clienthello_artifact()
        payload["source_sha256"] = "abc123"

        with self.assertRaises(ValueError):
            load_clienthello_artifact(self._write("bad_sha.clienthello.json", payload))

    def test_serverhello_rejects_wrong_artifact_type(self) -> None:
        payload = minimal_serverhello_artifact()
        payload["artifact_type"] = "tls_clienthello_fixtures"

        with self.assertRaises(ValueError):
            load_server_hello_artifact(self._write("wrong_type.serverhello.json", payload))

    def test_serverhello_rejects_duplicate_fixture_ids(self) -> None:
        payload = minimal_serverhello_artifact()
        duplicate = copy.deepcopy(payload["samples"][0])
        payload["samples"].append(duplicate)

        with self.assertRaises(ValueError):
            load_server_hello_artifact(self._write("dup.serverhello.json", payload))


if __name__ == "__main__":
    unittest.main()
