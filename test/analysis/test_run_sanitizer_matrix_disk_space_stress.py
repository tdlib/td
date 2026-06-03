# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class RunSanitizerMatrixDiskSpaceStressTest(unittest.TestCase):
    def test_repeated_preflight_cleanup_is_idempotent_after_first_reclaim(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo = pathlib.Path(temp_dir)
            for lane in run_sanitizer_matrix.LANES:
                lane_dir = repo / lane.build_dir
                lane_dir.mkdir(parents=True, exist_ok=True)
                (lane_dir / f"{lane.name}.o").write_bytes(b"compiled")

            first_cleaned = run_sanitizer_matrix.cleanup_sanitizer_build_dirs(
                repo, run_sanitizer_matrix.LANES, "stress preflight"
            )
            self.assertGreater(len(first_cleaned), 0)

            for _ in range(200):
                self.assertEqual(
                    [],
                    run_sanitizer_matrix.cleanup_sanitizer_build_dirs(
                        repo, run_sanitizer_matrix.LANES, "stress preflight repeat"
                    ),
                )


if __name__ == "__main__":
    unittest.main()
