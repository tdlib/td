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

POLL_PASS_B_ADAPTED_COMMITS = (
    "1eaf2481e",
    "04498cfbb",
    "d6ef00fa9",
    "1574780ca",
    "1f68a4a84",
    "c6411b9c9",
)
POLL_PASS_B_REJECTED_COMMITS = (
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


class PollBackportPlanContractTest(unittest.TestCase):
    def test_gating_plan_has_poll_pass_b_section_contract(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)",
            gating_plan,
        )
        self.assertIn("Valuable and adapted in this pass:", gating_plan)
        self.assertIn(
            "Assessed but intentionally not adapted in this pass:", gating_plan
        )

    def test_poll_pass_b_section_covers_reviewed_commit_set_contract(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        section = extract_poll_pass_b_section(gating_plan)

        for commit in POLL_PASS_B_ADAPTED_COMMITS + POLL_PASS_B_REJECTED_COMMITS:
            self.assertIn(commit, section)

    def test_manifest_declares_poll_pass_b_anchor_contract(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        self.assertIn("Wave 3 pass-B decisions are now recorded", manifest)
        self.assertIn("Section `0.3.3.a`", manifest)


if __name__ == "__main__":
    unittest.main()
