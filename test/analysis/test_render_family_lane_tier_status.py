#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for family-lane tier-status reporting deliverables.

These tests enforce the Workstream G/Section 16 requirement that nightly
artifacts include explicit tier status per (family, lane), active gates,
and a concrete next-tier delta. The reporting module must stay pure stdlib
and deterministic because it is intended for CI evidence emission.
"""

from __future__ import annotations

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import build_family_lane_baselines as baselines  # noqa: E402
import render_family_lane_tier_status as tier_status  # noqa: E402


def _sample() -> dict:
    return {
        "fixture_id": "fx:1",
        "cipher_suites": ["0x1301", "0x1302"],
        "supported_groups": ["0x11EC", "0x001D"],
        "extension_types": ["0x0000", "0x002B", "0x0033", "0xFE0D"],
        "extensions": [{"type": "0xFE0D"}],
        "non_grease_extensions_without_padding": ["0x0000", "0x002B", "0x0033", "0xFE0D"],
        "alpn_protocols": ["h2", "http/1.1"],
        "compress_certificate_algorithms": ["0x0002"],
        "key_share_entries": [{"group": "0x001D", "key_length": 32}],
        "record_length": 1779,
        "legacy_version": "0x0303",
        "ech": {"payload_length": 144},
    }


def _entry(
    fixture_id: str,
    source_kind: str,
    source_path: str,
    source_sha: str,
    capture_date_utc: str,
    scenario_id: str,
    profile_id: str = "chrome147_ios_chromium",
    os_family: str = "ios",
) -> dict:
    sample = _sample()
    sample["fixture_id"] = fixture_id
    return {
        "family_id": baselines.classify_family_id(profile_id, os_family),
        "route_lane": "non_ru_egress",
        "profile_id": profile_id,
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


class RenderFamilyLaneTierStatusTest(unittest.TestCase):
    def test_markdown_includes_required_columns_and_family_rows(self) -> None:
        samples = [
            _entry("fx1", "browser_capture", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "sess_a"),
            _entry("fx2", "browser_capture", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "sess_b"),
            _entry("fx3", "browser_capture", "/captures/c.pcap", "c" * 64, "2026-04-12T00:00:00Z", "sess_c"),
        ]
        report = tier_status.render_markdown_report(
            baselines=baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z"),
            generated_at_utc="2026-04-17T00:00:00Z",
        )

        self.assertIn("| Family | Route Lane | Tier |", report)
        self.assertIn("Authoritative Captures", report)
        self.assertIn("Independent Sources", report)
        self.assertIn("Active Gates", report)
        self.assertIn("Next-Tier Delta", report)
        self.assertIn("ios_chromium", report)
        self.assertIn("ru_egress", report)
        self.assertIn("unknown", report)

    def test_next_tier_delta_is_actionable_for_tier2_lane(self) -> None:
        samples = [
            _entry("fx1", "browser_capture", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "sess_a"),
            _entry("fx2", "browser_capture", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "sess_b"),
            _entry("fx3", "browser_capture", "/captures/c.pcap", "c" * 64, "2026-04-12T00:00:00Z", "sess_c"),
        ]
        tier2_lane = baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")[0]
        delta = tier_status.next_tier_delta(tier2_lane)

        self.assertIn("Tier3", delta)
        self.assertIn("authoritative captures", delta)
        self.assertIn("independent sources", delta)

    def test_tier0_fail_closed_lanes_are_marked_non_release(self) -> None:
        samples = [
            _entry("fx1", "browser_capture", "/captures/a.pcap", "a" * 64, "2026-04-10T00:00:00Z", "sess_a"),
            _entry("fx2", "browser_capture", "/captures/b.pcap", "b" * 64, "2026-04-11T00:00:00Z", "sess_b"),
            _entry("fx3", "browser_capture", "/captures/c.pcap", "c" * 64, "2026-04-12T00:00:00Z", "sess_c"),
        ]
        by_lane = {
            entry["route_lane"]: entry
            for entry in baselines.build_baselines(samples, now_utc="2026-04-17T00:00:00Z")
        }

        ru = by_lane["ru_egress"]
        unknown = by_lane["unknown"]
        self.assertEqual("Tier0", ru["tier"])
        self.assertEqual("Tier0", unknown["tier"])

        ru_gates = tier_status.active_gates_for_lane(ru)
        unknown_gates = tier_status.active_gates_for_lane(unknown)
        self.assertIn("non_release_fail_closed", ru_gates)
        self.assertIn("non_release_fail_closed", unknown_gates)


if __name__ == "__main__":
    unittest.main()
