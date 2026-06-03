# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class RunSanitizerMatrixNonTestEnvContractTest(unittest.TestCase):
    def test_non_test_phase_preserves_asan_env(self) -> None:
        lane_env = {
            "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1:check_initialization_order=1"
        }

        run_sanitizer_matrix.configure_lane_non_test_phase_env(lane_env, "asan")

        self.assertEqual(
            "halt_on_error=1:detect_leaks=1:check_initialization_order=1",
            lane_env["ASAN_OPTIONS"],
        )

    def test_non_test_phase_preserves_lsan_env(self) -> None:
        lane_env = {"LSAN_OPTIONS": "detect_leaks=1:halt_on_error=1"}

        run_sanitizer_matrix.configure_lane_non_test_phase_env(lane_env, "lsan")

        self.assertEqual("detect_leaks=1:halt_on_error=1", lane_env["LSAN_OPTIONS"])

    def test_non_test_phase_preserves_msan_env(self) -> None:
        lane_env = {"MSAN_OPTIONS": "halt_on_error=1:symbolize=1:poison_in_dtor=1"}

        run_sanitizer_matrix.configure_lane_non_test_phase_env(lane_env, "msan")

        self.assertEqual(
            "halt_on_error=1:symbolize=1:poison_in_dtor=1",
            lane_env["MSAN_OPTIONS"],
        )


if __name__ == "__main__":
    unittest.main()
