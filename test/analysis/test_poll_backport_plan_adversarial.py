# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md"
)

ADAPTED_COMMITS = (
    "1eaf2481e",
    "04498cfbb",
    "d6ef00fa9",
    "1574780ca",
    "1f68a4a84",
    "c6411b9c9",
)
REJECTED_COMMITS = (
    "978979edb",
    "ca82791de",
    "aaea672ae",
)


def get_manifest_line(manifest: str, commit: str) -> str:
    for line in manifest.splitlines():
        if f"`{commit}`" in line:
            return line
    raise AssertionError(f"manifest row for {commit} not found")


class PollBackportPlanAdversarialTest(unittest.TestCase):
    def test_reviewed_rows_must_not_keep_generic_pass_a_note(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in ADAPTED_COMMITS + REJECTED_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertNotIn(
                "Pass-A path+subject; full diff pending unless deep-reviewed",
                line,
            )
            self.assertIn("Section `0.3.3.a`", line)

    def test_rejected_rows_must_stay_explicitly_policy_restrictive(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in REJECTED_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("not adapted", line)
            self.assertIn("policy", line)


if __name__ == "__main__":
    unittest.main()
