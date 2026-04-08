# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import read_sha256
from run_corpus_smoke import run_corpus_smoke


def write_clienthello_artifact(
    artifact_path: pathlib.Path,
    *,
    profile_id: str,
    route_mode: str,
    source_path: pathlib.Path,
    source_sha256: str,
) -> None:
    artifact = {
        "profile_id": profile_id,
        "route_mode": route_mode,
        "scenario_id": f"{profile_id}-scenario",
        "source_path": str(source_path),
        "source_sha256": source_sha256,
        "source_kind": "browser_capture",
        "fixture_family_id": "chromium_44cd_mlkem_linux_desktop",
        "tls_gen": "tls13",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "samples": [
            {
                "cipher_suites": ["0x1301"],
                "supported_groups": ["0x001d"],
                "key_share_entries": [{"group": "0x001d"}],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": ["h2"],
                "extensions": [],
            }
        ],
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


class RunCorpusSmokeIndependenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_dir = pathlib.Path(self.temp_dir.name)
        self.capture_path = self.base_dir / "capture.pcapng"
        self.capture_path.write_text("capture-bytes\n", encoding="utf-8")
        self.fixtures_root = self.base_dir / "fixtures" / "clienthello" / "linux_desktop"
        self.fixtures_root.mkdir(parents=True)
        self.registry_path = self.base_dir / "profiles_validation.json"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_reports_duplicate_provenance_as_registry_failure(self) -> None:
        source_sha256 = read_sha256(self.capture_path)
        registry = {
            "contamination_guard": {
                "fail_on_missing_required_tag": True,
                "allow_mixed_source_kind_per_profile": False,
                "allow_mixed_family_per_profile": False,
                "allow_advisory_code_sample_per_profile": False,
            },
            "fixtures": {
                "fx_chrome133_a": {
                    "source_kind": "browser_capture",
                    "trust_tier": "verified",
                    "family": "chromium_44cd_mlkem_linux_desktop",
                    "transport": "tcp",
                    "platform_class": "desktop",
                    "tls_gen": "tls13",
                    "source_path": str(self.capture_path),
                    "source_sha256": source_sha256,
                },
                "fx_chrome133_b": {
                    "source_kind": "browser_capture",
                    "trust_tier": "verified",
                    "family": "chromium_44cd_mlkem_linux_desktop",
                    "transport": "tcp",
                    "platform_class": "desktop",
                    "tls_gen": "tls13",
                    "source_path": str(self.capture_path),
                    "source_sha256": source_sha256,
                },
            },
            "profiles": {
                "Chrome133": {
                    "release_gating": True,
                    "include_fixture_ids": ["fx_chrome133_a", "fx_chrome133_b"],
                    "ech_type": None,
                }
            },
        }
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")
        write_clienthello_artifact(
            self.fixtures_root / "chrome133.clienthello.json",
            profile_id="Chrome133",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=source_sha256,
        )

        report = run_corpus_smoke(self.registry_path, self.fixtures_root.parent)

        self.assertFalse(report["ok"])
        self.assertIn(
            "release-gating profile Chrome133 has no independent corroborating fixture",
            report["registry_failures"],
        )


if __name__ == "__main__":
    unittest.main()
