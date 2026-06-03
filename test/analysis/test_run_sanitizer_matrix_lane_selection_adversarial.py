# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest
from unittest import mock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class _StopAfterPreflight(Exception):
    pass


class RunSanitizerMatrixLaneSelectionAdversarialTest(unittest.TestCase):
    def test_chain_core_preflight_cleanup_excludes_fast_lane_build_dirs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            repo_root = root / "repo"
            output_root = root / "artifacts"
            repo_root.mkdir()
            output_root.mkdir()

            observed_calls: list[tuple[str, ...]] = []

            def stop_after_preflight(
                _repo_root: pathlib.Path,
                lanes: object,
                _reason: str,
            ) -> list[pathlib.Path]:
                observed_calls.append(tuple(lane.name for lane in lanes))
                raise _StopAfterPreflight()

            with mock.patch.object(
                run_sanitizer_matrix,
                "cleanup_sanitizer_build_dirs",
                side_effect=stop_after_preflight,
            ), mock.patch.object(
                run_sanitizer_matrix,
                "set_latest_pointer",
            ), mock.patch.object(
                run_sanitizer_matrix,
                "emit_status",
            ), mock.patch.object(
                sys,
                "argv",
                [
                    "run_sanitizer_matrix.py",
                    "--repo-root",
                    str(repo_root),
                    "--output-root",
                    str(output_root),
                    "--chain-core-sanitizers",
                ],
            ), self.assertRaises(
                _StopAfterPreflight
            ):
                run_sanitizer_matrix.main()

            self.assertEqual(
                [run_sanitizer_matrix.CORE_SANITIZER_LANE_NAMES],
                observed_calls,
            )
            self.assertNotIn("asan-fast", observed_calls[0])
            self.assertNotIn("ubsan-fast", observed_calls[0])
            self.assertNotIn("tsan-fast", observed_calls[0])


if __name__ == "__main__":
    unittest.main()
