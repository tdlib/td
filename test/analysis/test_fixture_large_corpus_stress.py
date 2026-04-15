# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: fixture loader hygiene under a large synthetic corpus.

Generates a sizable synthetic ClientHello corpus in a temp dir and asserts the
loader stays inside generous time and memory bounds. If the loader's cost grows
non-linearly with corpus size, this test will fail; do not weaken the bounds
to paper over a regression -- route it to David for review.
"""

from __future__ import annotations

import gc
import json
import pathlib
import sys
import tempfile
import time
import unittest

try:
    import resource  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - Windows dev hosts
    resource = None  # type: ignore[assignment]


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import load_clienthello_artifact  # noqa: E402


CORPUS_SIZE = 500
LOAD_TIME_CEILING_SECONDS = 10.0
MEMORY_CEILING_MB = 100.0


def synthetic_artifact(index: int) -> dict:
    profile_id = f"synthetic_profile_{index:04d}"
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": profile_id,
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": f"/synthetic/captures/{profile_id}.pcapng",
        "source_sha256": f"{index:064x}",
        "scenario_id": f"synthetic_{index:04d}",
        "samples": [
            {
                "fixture_id": f"{profile_id}:frame1",
                "cipher_suites": ["0x1301", "0x1302", "0x1303"],
                "supported_groups": ["0x001D", "0x0017"],
                "extensions": [
                    {"type": "0x0000", "length": 0, "is_grease": False, "body_hex": ""},
                    {"type": "0x002B", "length": 0, "is_grease": False, "body_hex": "0304"},
                ],
                "non_grease_extensions_without_padding": ["0x0000", "0x002B"],
                "alpn_protocols": ["h2", "http/1.1"],
                "key_share_entries": [{"group": "0x001D"}],
                "ech": None,
            }
        ],
    }


def _current_rss_mb() -> float:
    """Best-effort resident-set-size snapshot in MB.

    On Linux ``ru_maxrss`` is reported in KB; on macOS it is in bytes. We err on
    the generous side so a noisy CI box does not flake the assertion.
    """
    if resource is None:
        return 0.0
    usage = resource.getrusage(resource.RUSAGE_SELF)
    raw = float(usage.ru_maxrss)
    if sys.platform == "darwin":
        return raw / (1024.0 * 1024.0)
    return raw / 1024.0


class FixtureLargeCorpusStressTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _emit_corpus(self, count: int) -> list[pathlib.Path]:
        paths: list[pathlib.Path] = []
        for index in range(count):
            target = self.root / f"artifact_{index:04d}.json"
            with target.open("w", encoding="utf-8") as handle:
                json.dump(synthetic_artifact(index), handle)
            paths.append(target)
        return paths

    def test_500_artifact_corpus_load_time_and_memory(self) -> None:
        paths = self._emit_corpus(CORPUS_SIZE)
        self.assertEqual(CORPUS_SIZE, len(paths))

        gc.collect()
        rss_before = _current_rss_mb()
        start = time.monotonic()

        total_samples = 0
        for path in paths:
            samples = load_clienthello_artifact(path)
            total_samples += len(samples)

        elapsed = time.monotonic() - start
        gc.collect()
        rss_after = _current_rss_mb()
        rss_delta = max(0.0, rss_after - rss_before)

        self.assertEqual(CORPUS_SIZE, total_samples)
        self.assertLess(
            elapsed,
            LOAD_TIME_CEILING_SECONDS,
            msg=f"loader took {elapsed:.2f}s for {CORPUS_SIZE} artifacts; ceiling {LOAD_TIME_CEILING_SECONDS}s",
        )
        # REAL GAP: loader may retain per-artifact state that scales with corpus size; David to review
        self.assertLess(
            rss_delta,
            MEMORY_CEILING_MB,
            msg=f"loader grew RSS by {rss_delta:.2f}MB over {CORPUS_SIZE} artifacts; ceiling {MEMORY_CEILING_MB}MB",
        )


if __name__ == "__main__":
    unittest.main()
