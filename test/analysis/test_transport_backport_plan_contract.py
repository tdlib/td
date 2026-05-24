# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GATING_PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md"
)

TRANSPORT_PASS_B_COMMITS = (
    "28e0d0dbe",
    "00eedc5f9",
    "a82128ab8",
    "bfab03f7a",
    "8921c22f0",
    "e86cd4496",
    "dd78f94a8",
    "990b821c8",
    "691cb6a77",
    "49b3bcbb6",
)


class TransportBackportPlanContractTest(unittest.TestCase):
    def test_gating_plan_has_transport_pass_b_section_contract(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "#### 0.3.11 `W1-T` pass-B value decisions (2026-05-20)",
            gating_plan,
        )
        self.assertIn(
            "Valuable and already present or locally adapted:",
            gating_plan,
        )
        self.assertIn(
            "Assessed but intentionally not adapted in this pass:",
            gating_plan,
        )

    def test_transport_pass_b_section_covers_all_manifest_rows_contract(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        for commit in TRANSPORT_PASS_B_COMMITS:
            self.assertIn(commit, gating_plan)

    def test_transport_pass_b_section_explicitly_accounts_for_cross_lane_overlap(
        self,
    ) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("990b821c8", gating_plan)
        self.assertIn("49b3bcbb6", gating_plan)
        self.assertIn("W5-AI", gating_plan)
        self.assertIn("Section `0.3.6`", gating_plan)


if __name__ == "__main__":
    unittest.main()
