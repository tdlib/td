#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Nightly-scale stress test for the family/lane baseline generator.

Feeds a large synthetic corpus (5000 reviewed-shape capture artifacts)
into ``build_family_lane_baselines`` and asserts:

  * The generator completes in under 60 seconds on a typical dev box.
  * Running it twice on the same input produces byte-identical output
    (the emitted header is a deterministic function of the corpus).

If the generator module is not importable yet (another workstream is
still bringing it up) the test is skipped rather than red, so it never
blocks the PR-scope matrix.

Pure stdlib. No extra deps.
"""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import time
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

try:
    import build_family_lane_baselines  # type: ignore[import-not-found]
    _IMPORT_ERROR: Exception | None = None
except Exception as exc:  # pragma: no cover - exercised only when generator is absent
    build_family_lane_baselines = None  # type: ignore[assignment]
    _IMPORT_ERROR = exc


CORPUS_SIZE = 5000
RUNTIME_CEILING_SECONDS = 60.0


def _synthetic_sample(index: int) -> dict:
    """Return a synthetic ClientHello sample shaped like the reviewed artifacts.

    The shape is the minimal one the generator needs: cipher_suites,
    extension_types, supported_groups, alpn_protocols, compress_certificate
    algorithms, ECH presence, record_size_limit presence, ALPS type,
    key_share entries, and a stable content hash.
    """
    return {
        "fixture_id": f"synthetic:frame:{index:05d}",
        "client_hello_sha256": f"{index:064x}",
        "cipher_suites": ["0x1301", "0x1302", "0x1303"],
        "supported_groups": ["0x001D", "0x0017", "0x0018"],
        "extension_types": [
            "0x0000",
            "0x000A",
            "0x000B",
            "0x000D",
            "0x0010",
            "0x0017",
            "0x001B",
            "0x0023",
            "0x002B",
            "0x002D",
            "0x0033",
            "0xFE0D",
        ],
        "extensions": [
            {"type": "0x0000", "length": 0, "is_grease": False, "body_hex": ""},
            {"type": "0x002B", "length": 4, "is_grease": False, "body_hex": "02030403"},
        ],
        "non_grease_extensions_without_padding": [
            "0x0000",
            "0x000A",
            "0x000B",
            "0x000D",
            "0x0010",
            "0x0017",
            "0x001B",
            "0x0023",
            "0x002B",
            "0x002D",
            "0x0033",
            "0xFE0D",
        ],
        "alpn_protocols": ["h2", "http/1.1"],
        "compress_certificate_algorithms": ["0x0002"],
        "compression_methods": ["0x00"],
        "key_share_entries": [{"group": "0x001D", "key_length": 32}],
        "ech": {
            "kdf_id": "0x0001",
            "aead_id": "0x0001",
            "outer_type": "0x00",
            "config_id": "0x00",
            "enc_length": 32,
            "payload_length": 144,
            "enc_sha256": f"{index:064x}",
            "payload_sha256": f"{index:064x}",
        },
    }


def _synthetic_artifact(index: int) -> dict:
    # Rotate through a small set of reviewed profile_id / os_family
    # labels so the generator groups samples into multiple (family,
    # route_lane) cells. This exercises the heavy branch of the
    # pipeline, not a degenerate single-bucket path.
    rotation = [
        ("chrome133_linux_desktop", "linux"),
        ("firefox148_linux_desktop", "linux"),
        ("safari26_3_1_ios26_3_1_a", "ios"),
        ("chrome133_android14", "android"),
        ("chrome131_macos26", "macos"),
    ]
    profile_id, os_family = rotation[index % len(rotation)]
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": profile_id,
        "os_family": os_family,
        "device_class": "mobile" if os_family in ("ios", "android") else "desktop",
        "route_mode": "non_ru_egress",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": f"/synthetic/captures/{profile_id}_{index:05d}.pcapng",
        "source_sha256": f"{index:064x}",
        "scenario_id": f"synthetic_{index:05d}",
        "samples": [_synthetic_sample(index)],
    }


@unittest.skipIf(
    build_family_lane_baselines is None,
    f"Workstream B generator not yet available: {_IMPORT_ERROR!r}",
)
class BuildFamilyLaneBaselinesStressTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()
        self.input_dir = self.root / "clienthello"
        self.input_dir.mkdir(parents=True, exist_ok=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _emit_corpus(self, count: int) -> None:
        for index in range(count):
            # Shard across a few subdirectories so the discovery
            # traversal exercises its directory walk path.
            subdir = self.input_dir / ["a", "b", "c", "d"][index % 4]
            subdir.mkdir(parents=True, exist_ok=True)
            target = subdir / f"synthetic_{index:05d}.clienthello.json"
            with target.open("w", encoding="utf-8") as handle:
                json.dump(_synthetic_artifact(index), handle)

    def test_large_synthetic_corpus_completes_quickly_and_is_deterministic(self) -> None:
        assert build_family_lane_baselines is not None  # for type checkers
        self._emit_corpus(CORPUS_SIZE)

        first_output = self.root / "first.h"
        second_output = self.root / "second.h"

        # Run 1: measure runtime.
        t0 = time.monotonic()
        first = build_family_lane_baselines.generate_for(self.input_dir, first_output)
        elapsed = time.monotonic() - t0
        self.assertLess(
            elapsed,
            RUNTIME_CEILING_SECONDS,
            msg=(
                f"generator took {elapsed:.2f}s for {CORPUS_SIZE} artifacts; "
                f"ceiling {RUNTIME_CEILING_SECONDS:.1f}s"
            ),
        )

        # Run 2: byte-identical output.
        second = build_family_lane_baselines.generate_for(self.input_dir, second_output)
        self.assertEqual(first, second, "two runs produced different header text")
        self.assertEqual(
            first_output.read_bytes(),
            second_output.read_bytes(),
            "two runs produced different header bytes on disk",
        )


if __name__ == "__main__":
    unittest.main()
