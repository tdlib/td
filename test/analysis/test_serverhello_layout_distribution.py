# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream D: ServerHello first-flight layout alignment.

For every reviewed ServerHello artifact, this test verifies that the
``record_layout_signature`` (the per-frame sequence of TLS record content
types observed on the wire) is aligned within each
``(family_id, route_lane)`` bucket. Specifically:

  * every sample in a bucket must begin with record type ``0x16``
    (handshake) — the ServerHello itself
  * the set of record-type sequences per bucket must be bounded; the
    envelope is allowed to widen only to variants documented below
  * the selected TLS version must be consistent within a bucket
  * the negotiated cipher suite must come from the TLS 1.3 triplet
    (``0x1301``, ``0x1302``, ``0x1303``) when the selected version is
    ``0x0304``

When the reviewed corpus drifts outside the envelope this test fails
with a ``REAL GAP:`` prefix so reviewers can tell a reviewed-data drift
apart from a test harness defect.
"""

from __future__ import annotations

import pathlib
import sys
import unittest
from collections import defaultdict


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import load_server_hello_artifact  # noqa: E402


FIXTURES_ROOT = THIS_DIR / "fixtures"
SERVERHELLO_ROOT = FIXTURES_ROOT / "serverhello"

# The reviewed corpus legitimately contains these first-flight variants.
# A bucket whose observed set of sequences escapes this allow-list signals
# a drift that needs a reviewer.
ALLOWED_LAYOUT_SEQUENCES: frozenset[tuple[int, ...]] = frozenset(
    {
        (22,),          # bare ServerHello record
        (22, 20),       # ServerHello followed by ChangeCipherSpec
        (22, 20, 23),   # ServerHello + CCS + application_data
        (22, 23),       # ServerHello + application_data (no CCS compat byte)
    }
)

TLS13_SELECTED_VERSION = 0x0304
TLS13_CIPHER_SUITES = frozenset({0x1301, 0x1302, 0x1303})


class ServerHelloLayoutDistributionTest(unittest.TestCase):
    """ServerHello first-flight layout envelope, per family-lane bucket."""

    @classmethod
    def setUpClass(cls) -> None:
        if not SERVERHELLO_ROOT.is_dir():
            raise unittest.SkipTest(f"ServerHello fixtures root missing: {SERVERHELLO_ROOT}")

        buckets: dict[tuple[str, str], list[dict]] = defaultdict(list)
        for path in sorted(SERVERHELLO_ROOT.rglob("*.serverhello.json")):
            samples = load_server_hello_artifact(path)
            for sample in samples:
                key = (sample.metadata.fixture_family_id, sample.metadata.route_mode)
                buckets[key].append(
                    {
                        "path": path,
                        "fixture_id": sample.metadata.fixture_id,
                        "layout": tuple(sample.record_layout_signature),
                        "selected_version": sample.selected_version,
                        "cipher_suite": sample.cipher_suite,
                    }
                )
        cls._buckets = buckets

    def test_at_least_one_reviewed_family_lane_is_present(self) -> None:
        self.assertGreater(
            len(self._buckets),
            0,
            "no reviewed ServerHello artifacts discovered under fixtures/serverhello",
        )

    def test_every_first_flight_starts_with_handshake_record(self) -> None:
        rogue: list[str] = []
        for key, samples in sorted(self._buckets.items()):
            family, route = key
            for sample in samples:
                layout = sample["layout"]
                if not layout:
                    rogue.append(
                        f"REAL GAP: family-lane ({family!r}, {route!r}) "
                        f"fixture {sample['fixture_id']!r} has empty record layout"
                    )
                    continue
                if layout[0] != 22:
                    rogue.append(
                        f"REAL GAP: family-lane ({family!r}, {route!r}) "
                        f"fixture {sample['fixture_id']!r} starts with record "
                        f"type {layout[0]} (expected 22 = handshake)"
                    )
        if rogue:
            self.fail("\n".join(rogue))

    def test_every_layout_sequence_is_inside_known_envelope(self) -> None:
        rogue: list[str] = []
        for key, samples in sorted(self._buckets.items()):
            family, route = key
            for sample in samples:
                layout = sample["layout"]
                if layout not in ALLOWED_LAYOUT_SEQUENCES:
                    rogue.append(
                        f"REAL GAP: family-lane ({family!r}, {route!r}) "
                        f"fixture {sample['fixture_id']!r} has layout {list(layout)!r}, "
                        f"outside the reviewed envelope "
                        f"{sorted(seq for seq in ALLOWED_LAYOUT_SEQUENCES)}"
                    )
        if rogue:
            self.fail("\n".join(rogue))

    def test_selected_version_is_stable_within_family_lane(self) -> None:
        rogue: list[str] = []
        for key, samples in sorted(self._buckets.items()):
            family, route = key
            versions = {sample["selected_version"] for sample in samples}
            if len(versions) > 1:
                rogue.append(
                    f"REAL GAP: family-lane ({family!r}, {route!r}) mixes "
                    f"selected_versions {sorted(hex(v) for v in versions)}"
                )
        if rogue:
            self.fail("\n".join(rogue))

    def test_tls13_buckets_use_only_tls13_cipher_suites(self) -> None:
        rogue: list[str] = []
        for key, samples in sorted(self._buckets.items()):
            family, route = key
            for sample in samples:
                if sample["selected_version"] != TLS13_SELECTED_VERSION:
                    continue
                if sample["cipher_suite"] not in TLS13_CIPHER_SUITES:
                    rogue.append(
                        f"REAL GAP: family-lane ({family!r}, {route!r}) "
                        f"fixture {sample['fixture_id']!r} negotiated TLS 1.3 but "
                        f"selected cipher {hex(sample['cipher_suite'])} is not in "
                        f"{sorted(hex(c) for c in TLS13_CIPHER_SUITES)}"
                    )
        if rogue:
            self.fail("\n".join(rogue))

    def test_layout_length_is_bounded(self) -> None:
        # If a reviewed bucket ever reports more than four records in the
        # first flight, we've either widened the envelope or the extractor
        # started leaking post-handshake records into the signature.
        rogue: list[str] = []
        for key, samples in sorted(self._buckets.items()):
            family, route = key
            for sample in samples:
                if len(sample["layout"]) > 4:
                    rogue.append(
                        f"REAL GAP: family-lane ({family!r}, {route!r}) "
                        f"fixture {sample['fixture_id']!r} has "
                        f"{len(sample['layout'])} first-flight records "
                        f"(bounded envelope is 4)"
                    )
        if rogue:
            self.fail("\n".join(rogue))


if __name__ == "__main__":
    unittest.main()
