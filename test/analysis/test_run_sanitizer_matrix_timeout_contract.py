# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import unittest
from unittest import mock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class RunSanitizerMatrixTimeoutContractTest(unittest.TestCase):
    def test_cli_default_ctest_timeout_is_effectively_unbounded(self) -> None:
        with mock.patch.object(sys, "argv", ["run_sanitizer_matrix.py"]):
            self.assertEqual(0, run_sanitizer_matrix.parse_args().ctest_timeout)

    def test_cli_preserves_explicit_ctest_timeout(self) -> None:
        with mock.patch.object(
            sys,
            "argv",
            ["run_sanitizer_matrix.py", "--ctest-timeout", "1200"],
        ):
            self.assertEqual(1200, run_sanitizer_matrix.parse_args().ctest_timeout)

    def test_chain_mode_keeps_unbounded_timeout_without_override(self) -> None:
        with mock.patch.object(
            sys,
            "argv",
            ["run_sanitizer_matrix.py", "--chain-core-sanitizers"],
        ):
            self.assertEqual(0, run_sanitizer_matrix.parse_args().ctest_timeout)


if __name__ == "__main__":
    unittest.main()
