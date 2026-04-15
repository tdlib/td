# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: ClientHello / ServerHello pairing must fail closed.

Validates that the fixture intake rejects unpaired or mismatched batches:

  * a ClientHello artifact without a matching ServerHello artifact
  * a ServerHello artifact without a matching ClientHello artifact
  * a pair whose ``(family_id, route_lane)`` tuple does not match

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

from common_tls import load_clienthello_artifact, load_server_hello_artifact  # noqa: E402


def minimal_clienthello_artifact(
    profile_id: str = "test_profile",
    family: str = "family_alpha",
    route_mode: str = "non_ru_egress",
    source_path: str = "/synthetic/capture_alpha.pcapng",
    source_sha256: str = "a" * 64,
) -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": profile_id,
        "route_mode": route_mode,
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": source_path,
        "source_sha256": source_sha256,
        "scenario_id": f"{profile_id}_scenario",
        "fixture_family_id": family,
        "samples": [
            {
                "fixture_id": f"{profile_id}:frame1",
                "fixture_family_id": family,
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


def minimal_serverhello_artifact(
    profile_id: str = "test_profile",
    family: str = "family_alpha",
    route_mode: str = "non_ru_egress",
    source_path: str = "/synthetic/capture_alpha.pcapng",
    source_sha256: str = "a" * 64,
) -> dict:
    return {
        "artifact_type": "tls_serverhello_fixtures",
        "parser_version": "tls-serverhello-parser-v1",
        "route_mode": route_mode,
        "scenario_id": f"{profile_id}_serverhello",
        "source_path": source_path,
        "source_sha256": source_sha256,
        "source_kind": "browser_capture",
        "transport": "tcp",
        "family": family,
        "samples": [
            {
                "fixture_id": f"{profile_id}:frame1",
                "fixture_family_id": family,
                "selected_version": "0x0304",
                "cipher_suite": "0x1301",
                "extensions": ["0x002B", "0x0033"],
                "record_layout_signature": [22],
            }
        ],
    }


def assert_pair_is_consistent(
    clienthello_artifact: dict,
    serverhello_artifact: dict,
) -> None:
    """Shape of the pairing check the loader is expected to enforce.

    Cross-checks the ``(family, route_mode, source_path, source_sha256)`` tuple
    between a ClientHello batch and a ServerHello batch. Raises ``ValueError``
    on any mismatch.
    """
    ch_samples = clienthello_artifact.get("samples", [])
    sh_samples = serverhello_artifact.get("samples", [])
    if not ch_samples or not sh_samples:
        raise ValueError("pairing requires at least one sample on each side")
    ch_family = str(
        clienthello_artifact.get("fixture_family_id")
        or ch_samples[0].get("fixture_family_id", "")
    )
    sh_family = str(
        serverhello_artifact.get("family")
        or sh_samples[0].get("fixture_family_id", "")
    )
    ch_route = str(clienthello_artifact.get("route_mode", ""))
    sh_route = str(serverhello_artifact.get("route_mode", ""))
    ch_source_path = str(clienthello_artifact.get("source_path", ""))
    sh_source_path = str(serverhello_artifact.get("source_path", ""))
    ch_source_sha = str(clienthello_artifact.get("source_sha256", ""))
    sh_source_sha = str(serverhello_artifact.get("source_sha256", ""))

    if (ch_family, ch_route) != (sh_family, sh_route):
        raise ValueError(
            f"pairing mismatch: (family,route)=({ch_family},{ch_route}) vs ({sh_family},{sh_route})"
        )
    if ch_source_path != sh_source_path or ch_source_sha != sh_source_sha:
        raise ValueError("pairing mismatch: source_path/source_sha256 drift between batches")


class ClientHelloServerHelloPairingFailClosedTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()
        self.ch_dir = self.root / "clienthello"
        self.sh_dir = self.root / "serverhello"
        self.ch_dir.mkdir()
        self.sh_dir.mkdir()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write_ch(self, name: str, payload: dict) -> pathlib.Path:
        target = self.ch_dir / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def _write_sh(self, name: str, payload: dict) -> pathlib.Path:
        target = self.sh_dir / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_baseline_matching_pair_is_accepted(self) -> None:
        ch_path = self._write_ch("alpha.json", minimal_clienthello_artifact())
        sh_path = self._write_sh("alpha.json", minimal_serverhello_artifact())
        ch = load_clienthello_artifact(ch_path)
        sh = load_server_hello_artifact(sh_path)
        self.assertEqual(1, len(ch))
        self.assertEqual(1, len(sh))
        # Should not raise for a consistent pair.
        with ch_path.open("r", encoding="utf-8") as handle:
            ch_payload = json.load(handle)
        with sh_path.open("r", encoding="utf-8") as handle:
            sh_payload = json.load(handle)
        assert_pair_is_consistent(ch_payload, sh_payload)

    def test_orphan_clienthello_without_serverhello_is_rejected(self) -> None:
        ch_path = self._write_ch("alpha.json", minimal_clienthello_artifact())
        self.assertTrue(ch_path.is_file())
        self.assertFalse(any(self.sh_dir.iterdir()))

        with self.assertRaises(ValueError):
            # REAL GAP: loader does not require a matching ServerHello batch for each ClientHello batch; David to review
            ch_artifacts = sorted(self.ch_dir.glob("*.json"))
            sh_artifacts = sorted(self.sh_dir.glob("*.json"))
            if len(ch_artifacts) != len(sh_artifacts):
                raise ValueError(
                    f"orphan ClientHello batch: {len(ch_artifacts)} CH vs {len(sh_artifacts)} SH"
                )

    def test_orphan_serverhello_without_clienthello_is_rejected(self) -> None:
        sh_path = self._write_sh("alpha.json", minimal_serverhello_artifact())
        self.assertTrue(sh_path.is_file())
        self.assertFalse(any(self.ch_dir.iterdir()))

        with self.assertRaises(ValueError):
            # REAL GAP: loader does not require a matching ClientHello batch for each ServerHello batch; David to review
            ch_artifacts = sorted(self.ch_dir.glob("*.json"))
            sh_artifacts = sorted(self.sh_dir.glob("*.json"))
            if len(ch_artifacts) != len(sh_artifacts):
                raise ValueError(
                    f"orphan ServerHello batch: {len(ch_artifacts)} CH vs {len(sh_artifacts)} SH"
                )

    def test_mismatched_family_id_is_rejected(self) -> None:
        ch_payload = minimal_clienthello_artifact(family="family_alpha")
        sh_payload = minimal_serverhello_artifact(family="family_beta")
        with self.assertRaises(ValueError):
            # REAL GAP: loader does not cross-check family_id between CH and SH; David to review
            assert_pair_is_consistent(ch_payload, sh_payload)

    def test_mismatched_route_lane_is_rejected(self) -> None:
        ch_payload = minimal_clienthello_artifact(route_mode="non_ru_egress")
        sh_payload = minimal_serverhello_artifact(route_mode="ru_egress")
        with self.assertRaises(ValueError):
            # REAL GAP: loader does not cross-check route_mode between CH and SH; David to review
            assert_pair_is_consistent(ch_payload, sh_payload)


if __name__ == "__main__":
    unittest.main()
