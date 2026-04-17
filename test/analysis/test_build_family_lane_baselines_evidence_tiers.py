# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for evidence-tier assignment in family/lane baseline generation.

Pins Workstream B semantics:
  * advisory-only evidence stays Tier0
  * Tier2/Tier3 require authoritative + independent corroboration
  * stale captures downgrade effective tier
"""

from __future__ import annotations

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import build_family_lane_baselines as baselines  # noqa: E402


def _sample() -> dict:
    return {
        "fixture_id": "fx:1",
        "cipher_suites": ["0x1301", "0x1302"],
        "supported_groups": ["0x001D"],
        "extension_types": ["0x0000", "0x002B", "0x0033"],
        "extensions": [{"type": "0x002B"}],
        "non_grease_extensions_without_padding": ["0x0000", "0x002B", "0x0033"],
        "alpn_protocols": ["h2"],
        "compress_certificate_algorithms": ["0x0002"],
        "key_share_entries": [{"group": "0x001D", "key_length": 32}],
        "record_length": 1200,
        "legacy_version": "0x0303",
        "ech": None,
    }


def _entry(
    fixture_id: str,
    source_kind: str,
    source_path: str,
    source_sha: str,
    capture_date_utc: str,
    scenario_id: str,
) -> dict:
    sample = _sample()
    sample["fixture_id"] = fixture_id
    return {
        "family_id": "chromium_linux_desktop",
        "route_lane": "non_ru_egress",
        "profile_id": "chrome133_linux_desktop",
        "sample": sample,
        "artifact_path": pathlib.Path("/synthetic") / f"{fixture_id}.clienthello.json",
        "source_kind": source_kind,
        "source_path": source_path,
        "source_sha256": source_sha,
        "trust_tier": "verified",
        "scenario_id": scenario_id,
        "capture_date_utc": capture_date_utc,
        "contributor_id": "",
        "device_id": "",
        "os_build": "",
        "browser_build": "",
        "network_asn_country": "",
    }


class BuildFamilyLaneBaselinesEvidenceTiersTest(unittest.TestCase):
    def test_advisory_only_inputs_stay_tier0(self) -> None:
        samples = [
            _entry("fx1", "utls_snapshot", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "s1"),
            _entry("fx2", "advisory_code_sample", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "s2"),
        ]

        result = baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")
        self.assertEqual(3, len(result))
        by_lane = {entry["route_lane"]: entry for entry in result}
        self.assertEqual("Tier0", by_lane["non_ru_egress"]["tier"])
        self.assertEqual(0, by_lane["non_ru_egress"]["authoritative_sample_count"])
        self.assertEqual("Tier0", by_lane["ru_egress"]["tier"])
        self.assertEqual("Tier0", by_lane["unknown"]["tier"])

    def test_tier2_requires_authoritative_independent_and_sessions(self) -> None:
        samples = [
            _entry("fx1", "browser_capture", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "sess_a"),
            _entry("fx2", "browser_capture", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "sess_b"),
            _entry("fx3", "browser_capture", "/captures/c.pcap", "c" * 64, "2026-04-12T00:00:00Z", "sess_c"),
        ]

        result = baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")
        self.assertEqual("Tier2", result[0]["tier"])
        self.assertGreaterEqual(result[0]["num_sources"], 2)
        self.assertGreaterEqual(result[0]["num_sessions"], 2)

    def test_staleness_downgrades_effective_tier_by_one_level(self) -> None:
        samples = []
        for idx in range(15):
            samples.append(
                _entry(
                    fixture_id=f"fx{idx}",
                    source_kind="browser_capture",
                    source_path=f"/captures/{idx}.pcap",
                    source_sha=f"{idx:064x}",
                    capture_date_utc="2025-10-01T00:00:00Z",  # > 180 days stale vs now
                    scenario_id=f"session_{idx}",
                )
            )

        result = baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")
        self.assertEqual("Tier2", result[0]["tier"])
        self.assertEqual("Tier3", result[0]["raw_tier"])
        self.assertTrue(result[0]["stale_over_180_days"])

    def test_non_ru_lane_materializes_fail_closed_ru_and_unknown_lanes(self) -> None:
        samples = [
            _entry("fx1", "browser_capture", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "sess_a"),
            _entry("fx2", "browser_capture", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "sess_b"),
            _entry("fx3", "browser_capture", "/captures/c.pcap", "c" * 64, "2026-04-12T00:00:00Z", "sess_c"),
        ]

        result = baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")
        by_lane = {entry["route_lane"]: entry for entry in result}

        self.assertIn("non_ru_egress", by_lane)
        self.assertIn("ru_egress", by_lane)
        self.assertIn("unknown", by_lane)

        non_ru = by_lane["non_ru_egress"]
        ru = by_lane["ru_egress"]
        unknown = by_lane["unknown"]

        # Synthetic fail-closed lanes must not overclaim corpus evidence.
        self.assertEqual("Tier0", ru["tier"])
        self.assertEqual("Tier0", ru["raw_tier"])
        self.assertEqual(0, ru["sample_count"])
        self.assertEqual(0, ru["authoritative_sample_count"])
        self.assertEqual(0, unknown["sample_count"])
        self.assertEqual(0, unknown["authoritative_sample_count"])

        # RU and unknown lanes are ECH-off and route-fail-closed.
        self.assertFalse(ru["invariants"]["ech_presence_required"])
        self.assertFalse(unknown["invariants"]["ech_presence_required"])
        self.assertEqual([], ru["set_catalog"]["ech_payload_lengths"])
        self.assertEqual([], unknown["set_catalog"]["ech_payload_lengths"])

        # Route-fail-closed lanes must be deterministic mirrors of each other.
        self.assertEqual(ru["invariants"], unknown["invariants"])
        self.assertEqual(ru["set_catalog"], unknown["set_catalog"])

        # The reviewed non-RU lane still preserves empirical evidence.
        self.assertEqual("Tier2", non_ru["tier"])
        self.assertGreater(non_ru["sample_count"], 0)


if __name__ == "__main__":
    unittest.main()
