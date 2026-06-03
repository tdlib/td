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


class RunSanitizerMatrixDiskSpaceContractTest(unittest.TestCase):
    def test_cli_default_disk_space_mode_is_auto(self) -> None:
        with mock.patch.object(sys, "argv", ["run_sanitizer_matrix.py"]):
            self.assertEqual("auto", run_sanitizer_matrix.parse_args().disk_space_mode)

    def test_auto_disk_space_mode_is_ephemeral_for_new_runs(self) -> None:
        self.assertEqual(
            "ephemeral-builds",
            run_sanitizer_matrix.resolve_effective_disk_space_mode(
                "auto", resume_run_dir=None
            ),
        )

    def test_auto_disk_space_mode_preserves_resume_contract(self) -> None:
        self.assertEqual(
            "off",
            run_sanitizer_matrix.resolve_effective_disk_space_mode(
                "auto", resume_run_dir="artifacts/sast/sanitizer_matrix/previous"
            ),
        )

    def test_explicit_disk_space_modes_are_not_rewritten(self) -> None:
        for mode in ("off", "fresh-builds", "ephemeral-builds"):
            with self.subTest(mode=mode):
                self.assertEqual(
                    mode,
                    run_sanitizer_matrix.resolve_effective_disk_space_mode(
                        mode, resume_run_dir=None
                    ),
                )

    def test_resume_rejects_explicit_cleanup_modes(self) -> None:
        for mode in ("fresh-builds", "ephemeral-builds"):
            with self.subTest(mode=mode):
                with self.assertRaisesRegex(
                    ValueError, "resume requires preserved build directories"
                ):
                    run_sanitizer_matrix.resolve_effective_disk_space_mode(
                        mode, resume_run_dir="saved-run"
                    )


if __name__ == "__main__":
    unittest.main()
