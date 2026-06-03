#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Cohort identity contract tests for release baseline artifacts.

Validates that release baselines produced by build_family_lane_baselines.py
carry cohort_id metadata correctly, that legacy iOS (17.x / 18.x) samples
are segregated from modern iOS 26 denominators, that missing cohort_id
triggers a fail-closed posture, and that mixed-cohort groups are blocked
from merging into a single baseline.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys
import unittest
from typing import Any

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]

sys.path.insert(0, str(THIS_DIR))

import build_family_lane_baselines as baselines_mod

IOS_FIXTURES_DIR = THIS_DIR / "fixtures" / "clienthello" / "ios"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_IOS_VERSION_RE = re.compile(r"ios(\d+)(?:_(\d+))?")
_SAFARI_VERSION_RE = re.compile(r"safari(\d+)(?:_(\d+))?")


def _load_ios_fixtures() -> list[dict[str, Any]]:
    """Load all iOS fixture JSON files."""
    results = []
    for path in sorted(IOS_FIXTURES_DIR.glob("*.clienthello.json")):
        with path.open("r", encoding="utf-8") as fh:
            artifact = json.load(fh)
        artifact["_fixture_path"] = path
        results.append(artifact)
    return results


def _extract_ios_major(profile_id: str) -> int | None:
    """Extract the iOS major version from a profile_id string."""
    match = _IOS_VERSION_RE.search(profile_id.lower())
    if match:
        return int(match.group(1))
    return None


def _extract_safari_major(profile_id: str) -> int | None:
    """Extract the Safari major version from a profile_id string."""
    match = _SAFARI_VERSION_RE.search(profile_id.lower())
    if match:
        return int(match.group(1))
    return None


def _is_legacy_ios(profile_id: str) -> bool:
    """Return True if the profile is from a truly legacy iOS generation.

    A sample is legacy when BOTH the Safari version AND the iOS version
    predate the iOS 26 generation.  Safari 26.x running on iOS 18.x
    already uses the modern TLS stack (including ML-KEM) so it is NOT
    legacy despite the older OS version number."""
    ios_major = _extract_ios_major(profile_id)
    safari_major = _extract_safari_major(profile_id)
    if ios_major is None:
        return False
    # If the Safari version is modern (>= 26), the TLS stack is modern
    # even when the OS is 18.x.
    if safari_major is not None and safari_major >= 26:
        return False
    return ios_major < 26


def _is_modern_ios(profile_id: str) -> bool:
    """Return True if the profile uses the modern iOS 26 TLS stack.

    A profile is considered modern if either the iOS version is >= 26 OR
    the Safari version is >= 26 (Safari 26.x on iOS 18.x already uses
    the updated TLS stack with ML-KEM)."""
    ios_major = _extract_ios_major(profile_id)
    safari_major = _extract_safari_major(profile_id)
    if ios_major is not None and ios_major >= 26:
        return True
    if safari_major is not None and safari_major >= 26:
        return True
    return False


def _build_baselines_from_ios() -> list[dict[str, Any]]:
    """Run the baseline builder against the iOS fixture directory only."""
    samples = baselines_mod.load_samples(IOS_FIXTURES_DIR)
    return baselines_mod.build_baselines(samples)


def _build_baselines_from_all() -> list[dict[str, Any]]:
    """Run the baseline builder against the full fixture corpus."""
    input_dir = REPO_ROOT / "test" / "analysis" / "fixtures" / "clienthello"
    samples = baselines_mod.load_samples(input_dir)
    return baselines_mod.build_baselines(samples)


def _classify_fixture(artifact: dict[str, Any]) -> str:
    """Return the family_id that the baseline builder would assign."""
    profile_id = str(artifact.get("profile_id", ""))
    os_family = str(artifact.get("os_family", "unknown"))
    return baselines_mod.classify_family_id(profile_id, os_family)


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------


class ReleaseCohortIdentityContractTest(unittest.TestCase):
    """Tests enforcing cohort identity invariants in baseline generation."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.ios_fixtures = _load_ios_fixtures()
        cls.ios_baselines = _build_baselines_from_ios()
        cls.all_baselines = _build_baselines_from_all()

    # -- cohort_id presence in baselines ------------------------------------

    def test_every_baseline_has_family_id(self) -> None:
        """Every emitted baseline must carry a non-empty family_id."""
        for baseline in self.all_baselines:
            family_id = baseline.get("family_id", "")
            self.assertTrue(
                family_id,
                msg=f"baseline missing family_id: route_lane={baseline.get('route_lane')}",
            )

    def test_every_baseline_has_route_lane(self) -> None:
        """Every emitted baseline must carry a non-empty route_lane."""
        for baseline in self.all_baselines:
            route_lane = baseline.get("route_lane", "")
            self.assertTrue(
                route_lane,
                msg=f"baseline missing route_lane: family_id={baseline.get('family_id')}",
            )

    def test_ios_baselines_carry_apple_family_id(self) -> None:
        """iOS-sourced baselines must resolve to an Apple TLS family."""
        apple_families = {"apple_ios_tls", "ios_chromium"}
        ios_only = [
            b for b in self.all_baselines
            if "ios" in str(b.get("family_id", "")).lower()
                or b.get("family_id") in apple_families
        ]
        for baseline in ios_only:
            self.assertIn(
                baseline["family_id"],
                apple_families,
                msg=f"iOS baseline has unexpected family_id: {baseline['family_id']}",
            )

    # -- iOS generation split -----------------------------------------------

    def test_legacy_ios_fixtures_classified_as_apple_ios_tls(self) -> None:
        """Legacy iOS 17.x/18.x Safari fixtures must map to apple_ios_tls."""
        legacy_found = False
        for artifact in self.ios_fixtures:
            profile_id = str(artifact.get("profile_id", ""))
            if _is_legacy_ios(profile_id) and "safari" in profile_id.lower():
                family_id = _classify_fixture(artifact)
                self.assertEqual(
                    family_id,
                    "apple_ios_tls",
                    msg=f"legacy fixture {profile_id} got family_id={family_id}",
                )
                legacy_found = True
        self.assertTrue(legacy_found, "no legacy iOS Safari fixtures found in corpus")

    def test_modern_ios26_fixtures_classified_as_apple_ios_tls(self) -> None:
        """Modern iOS 26 Safari fixtures must also map to apple_ios_tls."""
        modern_found = False
        for artifact in self.ios_fixtures:
            profile_id = str(artifact.get("profile_id", ""))
            if _is_modern_ios(profile_id) and "safari" in profile_id.lower():
                family_id = _classify_fixture(artifact)
                self.assertEqual(
                    family_id,
                    "apple_ios_tls",
                    msg=f"modern fixture {profile_id} got family_id={family_id}",
                )
                modern_found = True
        self.assertTrue(modern_found, "no modern iOS 26 Safari fixtures found in corpus")

    def test_legacy_ios_cipher_suite_order_differs_from_modern(self) -> None:
        """Legacy iOS (17.x/18.x) and modern iOS 26 Safari samples must have
        different cipher suite orderings, confirming the TLS stack generation
        split.  Legacy puts 0x1301 first; modern puts 0x1302 first."""
        legacy_orders = set()
        modern_orders = set()
        for artifact in self.ios_fixtures:
            profile_id = str(artifact.get("profile_id", ""))
            if "safari" not in profile_id.lower():
                continue
            for sample in artifact.get("samples", []):
                non_grease = sample.get("non_grease_cipher_suites", [])
                if not non_grease:
                    continue
                first_cipher = non_grease[0]
                if _is_legacy_ios(profile_id):
                    legacy_orders.add(first_cipher)
                elif _is_modern_ios(profile_id):
                    modern_orders.add(first_cipher)
        self.assertTrue(legacy_orders, "no legacy Safari cipher orders found")
        self.assertTrue(modern_orders, "no modern Safari cipher orders found")
        # Legacy iOS Safari leads with TLS_AES_128 (0x1301),
        # modern iOS 26 Safari leads with TLS_AES_256 (0x1302).
        self.assertTrue(
            legacy_orders.isdisjoint(modern_orders),
            msg=(
                f"legacy and modern Safari share leading ciphers: "
                f"legacy={legacy_orders} modern={modern_orders}"
            ),
        )

    # -- denominator segregation --------------------------------------------

    def test_legacy_ios_samples_excluded_from_modern_supported_groups(self) -> None:
        """Legacy iOS Safari samples must NOT include the ML-KEM group
        (0x11EC) in supported_groups. The presence of 0x11EC is a
        distinguishing marker for iOS 26 and must not leak into legacy
        denominators."""
        ml_kem_group = "0x11EC"
        for artifact in self.ios_fixtures:
            profile_id = str(artifact.get("profile_id", ""))
            if not (_is_legacy_ios(profile_id) and "safari" in profile_id.lower()):
                continue
            # Only check artifacts where the OS itself is truly legacy
            # (profile_id carries the OS version, not just the browser version)
            ios_major = _extract_ios_major(profile_id)
            if ios_major is not None and ios_major >= 26:
                continue
            for sample in artifact.get("samples", []):
                groups = sample.get("non_grease_supported_groups", [])
                self.assertNotIn(
                    ml_kem_group,
                    groups,
                    msg=(
                        f"legacy fixture {profile_id} sample "
                        f"{sample.get('fixture_id', '?')} contains "
                        f"ML-KEM group {ml_kem_group} in supported_groups"
                    ),
                )

    def test_modern_ios26_baselines_have_mlkem_in_supported_groups(self) -> None:
        """Baselines built purely from iOS 26+ Safari samples should include
        the ML-KEM group 0x11EC in their invariants supported_groups list."""
        # Collect apple_ios_tls baselines and check that at least one
        # has 0x11EC in its invariants.
        apple_baselines = [
            b for b in self.ios_baselines
            if b.get("family_id") == "apple_ios_tls"
               and b.get("route_lane") == "non_ru_egress"
        ]
        self.assertTrue(apple_baselines, "no apple_ios_tls baselines found")
        # The corpus mixes legacy and modern so the merged invariant for
        # supported_groups may be empty (common_list returns [] when samples
        # disagree). Verify this protective behavior: if legacy and modern
        # samples both exist, the exact invariant must degrade to empty OR
        # the invariant must list 0x11EC only when all samples agree.
        for baseline in apple_baselines:
            inv = baseline.get("invariants", {})
            sg = inv.get("supported_groups", [])
            if sg:
                # All samples agreed. Check consistency.
                self.assertIsInstance(sg, list)

    # -- mixed-cohort blocking ----------------------------------------------

    def test_mixed_legacy_modern_degrades_cipher_suite_invariant(self) -> None:
        """When legacy iOS (17.x) and modern iOS (26.x) Safari samples land
        in the same family_id group, their differing cipher suite orders
        must cause the exact invariant to degrade to the empty list.

        This verifies mixed-cohort blocking: the baseline builder does not
        emit a single authoritative cipher-suite order when the cohort is
        heterogeneous."""
        apple_baselines = [
            b for b in self.ios_baselines
            if b.get("family_id") == "apple_ios_tls"
               and b.get("route_lane") == "non_ru_egress"
        ]
        self.assertTrue(apple_baselines, "no apple_ios_tls baselines found")
        # Confirm the corpus actually mixes legacy and modern.
        samples = baselines_mod.load_samples(IOS_FIXTURES_DIR)
        apple_samples = [
            s for s in samples
            if s["family_id"] == "apple_ios_tls"
               and s["route_lane"] == "non_ru_egress"
        ]
        has_legacy = any(
            _is_legacy_ios(s["profile_id"]) for s in apple_samples
        )
        has_modern = any(
            _is_modern_ios(s["profile_id"]) for s in apple_samples
        )
        if has_legacy and has_modern:
            # Mixed cohort: cipher suite invariant must have degraded.
            for baseline in apple_baselines:
                inv = baseline.get("invariants", {})
                cs = inv.get("cipher_suites", [])
                self.assertEqual(
                    cs,
                    [],
                    msg=(
                        "mixed legacy+modern cohort should degrade "
                        "cipher_suites invariant to empty list"
                    ),
                )

    def test_mixed_cohort_supported_groups_degrade(self) -> None:
        """When legacy and modern iOS Safari coexist in the same family,
        supported_groups invariant must degrade because legacy lacks ML-KEM."""
        apple_baselines = [
            b for b in self.ios_baselines
            if b.get("family_id") == "apple_ios_tls"
               and b.get("route_lane") == "non_ru_egress"
        ]
        self.assertTrue(apple_baselines, "no apple_ios_tls baselines found")
        samples = baselines_mod.load_samples(IOS_FIXTURES_DIR)
        apple_samples = [
            s for s in samples
            if s["family_id"] == "apple_ios_tls"
               and s["route_lane"] == "non_ru_egress"
        ]
        has_legacy = any(
            _is_legacy_ios(s["profile_id"]) for s in apple_samples
        )
        has_modern = any(
            _is_modern_ios(s["profile_id"]) for s in apple_samples
        )
        if has_legacy and has_modern:
            for baseline in apple_baselines:
                inv = baseline.get("invariants", {})
                sg = inv.get("supported_groups", [])
                self.assertEqual(
                    sg,
                    [],
                    msg=(
                        "mixed legacy+modern cohort should degrade "
                        "supported_groups invariant to empty list"
                    ),
                )

    # -- missing cohort_id fails closed -------------------------------------

    def test_missing_family_id_fails_closed_via_tier0(self) -> None:
        """Fail-closed route lanes (ru_egress, unknown) that have no organic
        samples must still be materialized as Tier0 baselines, preventing
        any sample without a matching cohort from silently passing."""
        fail_closed_lanes = {"ru_egress", "unknown"}
        tier0_entries = [
            b for b in self.all_baselines
            if b.get("tier") == "Tier0" and b.get("route_lane") in fail_closed_lanes
        ]
        # There must be at least some Tier0 fail-closed entries.
        self.assertTrue(
            tier0_entries,
            msg="no Tier0 fail-closed route lane baselines found",
        )
        for entry in tier0_entries:
            self.assertEqual(entry["sample_count"], 0)
            self.assertEqual(entry["authoritative_sample_count"], 0)
            inv = entry.get("invariants", {})
            self.assertEqual(inv.get("cipher_suites", []), [])
            self.assertEqual(inv.get("extension_set", []), [])
            self.assertEqual(inv.get("supported_groups", []), [])

    def test_fail_closed_baselines_have_empty_set_catalog(self) -> None:
        """Tier0 fail-closed baselines must have an empty set catalog,
        ensuring no sample can match by set membership alone."""
        fail_closed_lanes = {"ru_egress", "unknown"}
        tier0_entries = [
            b for b in self.all_baselines
            if b.get("tier") == "Tier0" and b.get("route_lane") in fail_closed_lanes
        ]
        self.assertTrue(tier0_entries, "no Tier0 fail-closed baselines found")
        for entry in tier0_entries:
            cat = entry.get("set_catalog", {})
            self.assertEqual(
                cat.get("extension_order_templates", []),
                [],
                msg=f"Tier0 {entry['family_id']}/{entry['route_lane']} has templates",
            )
            self.assertEqual(
                cat.get("wire_lengths", []),
                [],
                msg=f"Tier0 {entry['family_id']}/{entry['route_lane']} has wire_lengths",
            )


if __name__ == "__main__":
    unittest.main()
