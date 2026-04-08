# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from check_fixture_registry_complete import validate_registry_completeness
from common_tls import read_sha256


def make_registry(base_dir: pathlib.Path) -> dict:
    network_path = base_dir / "fx_ch133_net.json"
    network_path.write_text("network-fixture\n", encoding="utf-8")
    corroborating_path = base_dir / "fx_ch133_utls.txt"
    corroborating_path.write_text("utls-fixture\n", encoding="utf-8")

    return {
        "contamination_guard": {
            "allow_mixed_source_kind_per_profile": False,
            "allow_mixed_family_per_profile": False,
            "allow_advisory_code_sample_per_profile": False,
        },
        "fixtures": {
            "fx_ch133_net": {
                "source_kind": "browser_capture",
                "trust_tier": "verified",
                "family": "chromium_44cd_mlkem_linux_desktop",
                "transport": "tcp",
                "platform_class": "desktop",
                "tls_gen": "tls13",
                "source_path": str(network_path),
                "source_sha256": read_sha256(network_path),
            },
            "fx_ch133_utls": {
                "source_kind": "utls_snapshot",
                "trust_tier": "verified",
                "family": "chromium_44cd_mlkem_linux_desktop",
                "transport": "tcp",
                "platform_class": "desktop",
                "tls_gen": "tls13",
                "source_path": str(corroborating_path),
                "source_sha256": read_sha256(corroborating_path),
            },
        },
        "profiles": {
            "Chrome133": {
                "release_gating": True,
                "include_fixture_ids": ["fx_ch133_net", "fx_ch133_utls"],
            }
        },
    }


class CheckFixtureRegistryIndependenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_dir = pathlib.Path(self.temp_dir.name)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_rejects_duplicate_fixture_ids_as_independent_corroboration(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ch133_net"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 has no independent corroborating fixture", failures)

    def test_rejects_same_source_reused_under_different_fixture_ids(self) -> None:
        registry = make_registry(self.base_dir)
        registry["fixtures"]["fx_ch133_clone"] = dict(registry["fixtures"]["fx_ch133_net"])
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ch133_clone"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 has no independent corroborating fixture", failures)


if __name__ == "__main__":
    unittest.main()
