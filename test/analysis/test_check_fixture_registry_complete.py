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
    utls_path = base_dir / "fx_ch133_utls.txt"
    utls_path.write_text("utls-fixture\n", encoding="utf-8")
    ios_path = base_dir / "fx_ios_utls.txt"
    ios_path.write_text("ios-utls-fixture\n", encoding="utf-8")

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
                "source_kind": "browser_capture",
                "trust_tier": "verified",
                "family": "chromium_44cd_mlkem_linux_desktop",
                "transport": "tcp",
                "platform_class": "desktop",
                "tls_gen": "tls13",
                "source_path": str(utls_path),
                "source_sha256": read_sha256(utls_path),
            },
            "fx_ios_utls": {
                "source_kind": "utls_snapshot",
                "trust_tier": "advisory",
                "family": "ios14_like",
                "transport": "tcp",
                "platform_class": "mobile",
                "tls_gen": "tls13",
                "source_path": str(ios_path),
                "source_sha256": read_sha256(ios_path),
            },
        },
        "profiles": {
            "Chrome133": {
                "release_gating": True,
                "include_fixture_ids": ["fx_ch133_net", "fx_ch133_utls"],
            },
            "IOS14": {
                "release_gating": False,
                "include_fixture_ids": ["fx_ios_utls"],
            },
        },
    }


class CheckFixtureRegistryCompleteTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_dir = pathlib.Path(self.temp_dir.name)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_accepts_complete_registry(self) -> None:
        failures = validate_registry_completeness(make_registry(self.base_dir))

        self.assertEqual([], failures)

    def test_rejects_placeholder_fixture_id(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["<explicit-fixture-id>"]

        failures = validate_registry_completeness(registry)

        self.assertIn("profile Chrome133 contains placeholder fixture id <explicit-fixture-id>", failures)

    def test_rejects_unknown_fixture_reference(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_missing"]

        failures = validate_registry_completeness(registry)

        self.assertIn("profile Chrome133 references unknown fixture id fx_missing", failures)

    def test_rejects_release_gating_profile_without_network_fixture(self) -> None:
        registry = make_registry(self.base_dir)
        snapshot_path = self.base_dir / "fx_ch133_snapshot.txt"
        snapshot_path.write_text("snapshot-fixture\n", encoding="utf-8")
        registry["fixtures"]["fx_ch133_snapshot"] = {
            "source_kind": "utls_snapshot",
            "trust_tier": "verified",
            "family": "chromium_44cd_mlkem_linux_desktop",
            "source_path": str(snapshot_path),
            "source_sha256": read_sha256(snapshot_path),
        }
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_snapshot"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 has no network-derived fixture", failures)

    def test_rejects_release_gating_profile_without_independent_corroboration(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 has no independent corroborating fixture", failures)

    def test_rejects_missing_source_path(self) -> None:
        registry = make_registry(self.base_dir)
        registry["fixtures"]["fx_ch133_net"]["source_path"] = "/path/does/not/exist.pcapng"

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net source_path does not exist: /path/does/not/exist.pcapng", failures)

    def test_rejects_source_hash_mismatch(self) -> None:
        registry = make_registry(self.base_dir)
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture_path = pathlib.Path(temp_dir) / "fixture.json"
            fixture_path.write_text("fixture-body\n", encoding="utf-8")
            registry["fixtures"]["fx_ch133_net"]["source_path"] = str(fixture_path)
            registry["fixtures"]["fx_ch133_net"]["source_sha256"] = "0" * 64

            failures = validate_registry_completeness(registry)

        self.assertIn(
            f"fixture fx_ch133_net source_sha256 mismatch for {fixture_path}",
            failures,
        )

    def test_rejects_missing_source_kind(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["source_kind"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag source_kind", failures)

    def test_rejects_missing_family(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["family"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag family", failures)

    def test_rejects_missing_trust_tier(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["trust_tier"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag trust_tier", failures)

    def test_rejects_missing_transport(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["transport"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag transport", failures)

    def test_rejects_missing_platform_class(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["platform_class"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag platform_class", failures)

    def test_rejects_missing_tls_gen(self) -> None:
        registry = make_registry(self.base_dir)
        del registry["fixtures"]["fx_ch133_net"]["tls_gen"]

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net is missing required tag tls_gen", failures)

    def test_rejects_noncanonical_transport(self) -> None:
        registry = make_registry(self.base_dir)
        registry["fixtures"]["fx_ch133_net"]["transport"] = "udp"

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net has invalid transport udp", failures)

    def test_rejects_noncanonical_platform_class(self) -> None:
        registry = make_registry(self.base_dir)
        registry["fixtures"]["fx_ch133_net"]["platform_class"] = "desktop-linux"

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net has invalid platform_class desktop-linux", failures)

    def test_rejects_noncanonical_tls_gen(self) -> None:
        registry = make_registry(self.base_dir)
        registry["fixtures"]["fx_ch133_net"]["tls_gen"] = "TLS13"

        failures = validate_registry_completeness(registry)

        self.assertIn("fixture fx_ch133_net has invalid tls_gen TLS13", failures)

    def test_rejects_mixed_family_in_release_gating_profile(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ios_utls"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 mixes fixture families", failures)

    def test_rejects_mixed_source_kind_in_release_gating_profile(self) -> None:
        registry = make_registry(self.base_dir)
        synthetic_path = self.base_dir / "fx_ch133_advisory.txt"
        synthetic_path.write_text("synthetic-fixture\n", encoding="utf-8")
        registry["fixtures"]["fx_ch133_advisory"] = {
            "source_kind": "advisory_code_sample",
            "trust_tier": "advisory",
            "family": "chromium_44cd_mlkem_linux_desktop",
            "source_path": str(synthetic_path),
            "source_sha256": read_sha256(synthetic_path),
        }
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ch133_advisory"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 mixes source_kind values", failures)

    def test_rejects_advisory_code_sample_even_when_mixed_source_kind_is_allowed(self) -> None:
        registry = make_registry(self.base_dir)
        registry["contamination_guard"]["allow_mixed_source_kind_per_profile"] = True
        synthetic_path = self.base_dir / "fx_ch133_advisory.txt"
        synthetic_path.write_text("synthetic-fixture\n", encoding="utf-8")
        registry["fixtures"]["fx_ch133_advisory"] = {
            "source_kind": "advisory_code_sample",
            "trust_tier": "verified",
            "family": "chromium_44cd_mlkem_linux_desktop",
            "source_path": str(synthetic_path),
            "source_sha256": read_sha256(synthetic_path),
        }
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ch133_advisory"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 includes advisory_code_sample fixture fx_ch133_advisory", failures)

    def test_rejects_non_verified_fixture_in_release_gating_profile(self) -> None:
        registry = make_registry(self.base_dir)
        registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_ch133_net", "fx_ios_utls"]

        failures = validate_registry_completeness(registry)

        self.assertIn("release-gating profile Chrome133 includes non-verified fixture fx_ios_utls", failures)


if __name__ == "__main__":
    unittest.main()