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
GATING_PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md"
)

POLL_PASS_B_COMMITS = (
    "1eaf2481e",
    "04498cfbb",
    "d6ef00fa9",
    "1574780ca",
    "1f68a4a84",
    "c6411b9c9",
    "978979edb",
    "ca82791de",
    "aaea672ae",
)


def extract_poll_pass_b_section(gating_plan: str) -> str:
    section_header = "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)"
    section_start = gating_plan.find(section_header)
    if section_start == -1:
        raise AssertionError("W3 pass-B section header not found")

    next_section_start = gating_plan.find("\n#### 0.3.4 ", section_start)
    if next_section_start == -1:
        return gating_plan[section_start:]
    return gating_plan[section_start:next_section_start]


def get_manifest_line(manifest: str, commit: str) -> str:
    for line in manifest.splitlines():
        if f"`{commit}`" in line:
            return line
    raise AssertionError(f"manifest row for {commit} not found")


class PollBackportPlanStressTest(unittest.TestCase):
    def test_repeated_source_reads_keep_poll_accounting_stable(self) -> None:
        iterations = 2000

        for _ in range(iterations):
            manifest = MANIFEST_PATH.read_text(encoding="utf-8")
            gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
            section = extract_poll_pass_b_section(gating_plan)

            self.assertEqual(
                1,
                gating_plan.count(
                    "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)"
                ),
            )
            self.assertIn("Valuable and adapted in this pass:", gating_plan)
            self.assertIn(
                "Assessed but intentionally not adapted in this pass:", gating_plan
            )

            for commit in POLL_PASS_B_COMMITS:
                self.assertIn(commit, section)
                line = get_manifest_line(manifest, commit)
                self.assertIn("Section `0.3.3.a`", line)


if __name__ == "__main__":
    unittest.main()
