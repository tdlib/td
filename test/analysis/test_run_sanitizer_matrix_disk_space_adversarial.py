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


class RunSanitizerMatrixDiskSpaceAdversarialTest(unittest.TestCase):
    def test_matrix_cleanup_rejects_lane_build_dir_that_escapes_repo(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            outside = root / "outside"
            outside.mkdir()
            marker = outside / "must_survive.txt"
            marker.write_text("keep", encoding="utf-8")
            repo = root / "repo"
            repo.mkdir()
            hostile_lane = run_sanitizer_matrix.Lane(
                name="hostile",
                configure_preset="hostile-configure",
                build_preset="hostile-build",
                build_dir="../outside",
                env={},
            )

            with self.assertRaisesRegex(ValueError, "escapes repo root"):
                run_sanitizer_matrix.cleanup_sanitizer_build_dirs(
                    repo, [hostile_lane], "adversarial preflight"
                )

            self.assertTrue(marker.exists())
            self.assertEqual("keep", marker.read_text(encoding="utf-8"))

    def test_cleanup_build_dir_unlinks_symlink_without_deleting_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            target = root / "target"
            target.mkdir()
            marker = target / "must_survive.txt"
            marker.write_text("keep", encoding="utf-8")
            link = root / "build-asan"
            link.symlink_to(target, target_is_directory=True)

            run_sanitizer_matrix.cleanup_build_dir(link, "symlink adversarial test")

            self.assertFalse(link.exists())
            self.assertTrue(marker.exists())
            self.assertEqual("keep", marker.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
