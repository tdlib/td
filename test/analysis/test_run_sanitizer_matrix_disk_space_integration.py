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


class RunSanitizerMatrixDiskSpaceIntegrationTest(unittest.TestCase):
    def test_preflight_cleanup_removes_all_known_sanitizer_build_dirs_once(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo = pathlib.Path(temp_dir)
            unique_build_dirs = {lane.build_dir for lane in run_sanitizer_matrix.LANES}
            for build_dir in unique_build_dirs:
                lane_dir = repo / build_dir
                lane_dir.mkdir(parents=True)
                (lane_dir / "object.o").write_bytes(b"compiled")
            unrelated = repo / "build"
            unrelated.mkdir()
            (unrelated / "must_survive.txt").write_text("keep", encoding="utf-8")

            cleaned = run_sanitizer_matrix.cleanup_sanitizer_build_dirs(
                repo, run_sanitizer_matrix.LANES, "matrix preflight"
            )

            self.assertEqual(
                sorted(str(repo / build_dir) for build_dir in unique_build_dirs),
                sorted(str(path) for path in cleaned),
            )
            for build_dir in unique_build_dirs:
                self.assertFalse((repo / build_dir).exists())
            self.assertEqual(
                "keep", (unrelated / "must_survive.txt").read_text(encoding="utf-8")
            )


if __name__ == "__main__":
    unittest.main()
