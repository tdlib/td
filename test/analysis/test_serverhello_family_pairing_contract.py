# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream D: ServerHello family pairing contract.

Validates that the reviewed ServerHello artifacts under
``test/analysis/fixtures/serverhello/**`` line up with their ClientHello
counterparts under ``test/analysis/fixtures/clienthello/**`` on the
``(family_id, route_lane)`` tuple:

  * every ClientHello artifact has at least one ServerHello artifact with
    a matching ``(family_id, route_lane)`` pair
  * every ServerHello artifact has at least one ClientHello artifact with
    a matching ``(family_id, route_lane)`` pair
  * both sides report the same canonical ``route_lane`` string

When a pair is missing, the failure message is prefixed with
``REAL GAP:`` so reviewers can distinguish a missing reviewed artifact
from a test harness defect.
"""

from __future__ import annotations

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import (  # noqa: E402
    load_clienthello_artifact,
    load_server_hello_artifact,
)


FIXTURES_ROOT = THIS_DIR / "fixtures"
CLIENTHELLO_ROOT = FIXTURES_ROOT / "clienthello"
SERVERHELLO_ROOT = FIXTURES_ROOT / "serverhello"


def _clienthello_family_key(sample) -> str:
    """Return the canonical family key for a ClientHello sample.

    The reviewed CH artifacts set ``fixture_family_id`` when the extractor
    can determine the fixture-family label, and otherwise fall back to
    the profile identifier (``profile_id`` on the CH side is the same
    string as ``family`` on the SH side for the reviewed corpus).
    """
    family_id = sample.metadata.fixture_family_id
    if family_id:
        return family_id
    return sample.profile


def _collect_pairs_from_clienthellos() -> dict[tuple[str, str], list[pathlib.Path]]:
    """Return every reviewed CH artifact keyed by ``(family_id, route_lane)``."""
    index: dict[tuple[str, str], list[pathlib.Path]] = {}
    for path in sorted(CLIENTHELLO_ROOT.rglob("*.clienthello.json")):
        samples = load_clienthello_artifact(path)
        if not samples:
            continue
        for sample in samples:
            key = (_clienthello_family_key(sample), sample.metadata.route_mode)
            index.setdefault(key, []).append(path)
    return index


def _collect_pairs_from_serverhellos() -> dict[tuple[str, str], list[pathlib.Path]]:
    """Return every reviewed SH artifact keyed by ``(family_id, route_lane)``."""
    index: dict[tuple[str, str], list[pathlib.Path]] = {}
    for path in sorted(SERVERHELLO_ROOT.rglob("*.serverhello.json")):
        samples = load_server_hello_artifact(path)
        if not samples:
            continue
        for sample in samples:
            key = (sample.metadata.fixture_family_id, sample.metadata.route_mode)
            index.setdefault(key, []).append(path)
    return index


class ServerHelloFamilyPairingContractTest(unittest.TestCase):
    """Reviewed ClientHello / ServerHello pairing by ``(family, route)``."""

    @classmethod
    def setUpClass(cls) -> None:
        if not CLIENTHELLO_ROOT.is_dir():
            raise unittest.SkipTest(f"ClientHello fixtures root missing: {CLIENTHELLO_ROOT}")
        if not SERVERHELLO_ROOT.is_dir():
            raise unittest.SkipTest(f"ServerHello fixtures root missing: {SERVERHELLO_ROOT}")
        cls._ch_index = _collect_pairs_from_clienthellos()
        cls._sh_index = _collect_pairs_from_serverhellos()

    def test_every_clienthello_family_has_a_serverhello_peer(self) -> None:
        missing: list[str] = []
        for key, ch_paths in sorted(self._ch_index.items()):
            if key not in self._sh_index:
                family, route = key
                example = ch_paths[0].name
                missing.append(
                    f"REAL GAP: family-lane ({family!r}, {route!r}) has no SH fixtures "
                    f"(CH example: {example})"
                )
        if missing:
            self.fail("\n".join(missing))

    def test_every_serverhello_family_has_a_clienthello_peer(self) -> None:
        missing: list[str] = []
        for key, sh_paths in sorted(self._sh_index.items()):
            if key not in self._ch_index:
                family, route = key
                example = sh_paths[0].name
                missing.append(
                    f"REAL GAP: family-lane ({family!r}, {route!r}) has no CH fixtures "
                    f"(SH example: {example})"
                )
        if missing:
            self.fail("\n".join(missing))

    def test_route_lane_strings_are_canonical_on_both_sides(self) -> None:
        # Both loaders already normalize through common_tls.normalize_route_mode,
        # so any reviewed artifact with a non-canonical route_mode raises on load.
        # This test is a belt-and-braces check that the collected indices only
        # expose the canonical values — any drift means the loader contract was
        # bypassed.
        allowed = {"unknown", "ru_egress", "non_ru_egress"}
        rogue: list[str] = []
        for family, route in self._ch_index:
            if route not in allowed:
                rogue.append(f"CH route {route!r} for family {family!r} not canonical")
        for family, route in self._sh_index:
            if route not in allowed:
                rogue.append(f"SH route {route!r} for family {family!r} not canonical")
        if rogue:
            self.fail("\n".join(rogue))

    def test_at_least_one_reviewed_family_pair_exists(self) -> None:
        # Sanity: if either side collapses to empty, downstream workstreams that
        # depend on "at least one reviewed pair" would silently pass.
        self.assertGreater(
            len(self._ch_index),
            0,
            "no reviewed ClientHello artifacts discovered under fixtures/clienthello",
        )
        self.assertGreater(
            len(self._sh_index),
            0,
            "no reviewed ServerHello artifacts discovered under fixtures/serverhello",
        )


if __name__ == "__main__":
    unittest.main()
