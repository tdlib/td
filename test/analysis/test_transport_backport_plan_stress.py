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


class TransportBackportPlanStressTest(unittest.TestCase):
    def test_repeated_source_reads_keep_transport_accounting_stable(self) -> None:
        iterations = 2000

        for _ in range(iterations):
            manifest = MANIFEST_PATH.read_text(encoding="utf-8")
            gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

            self.assertEqual(
                1,
                gating_plan.count(
                    "#### 0.3.11 `W1-T` pass-B value decisions (2026-05-20)"
                ),
            )
            self.assertIn(
                "Closed by pass-B review; no standalone `W1-T` execution branch authorized",
                gating_plan,
            )
            self.assertIn(
                "Pass-B reviewed: task / option propagation already present locally; see gating Section `0.3.11`",
                manifest,
            )
            self.assertIn(
                "Pass-B reviewed: valuable only inside W5-AI exact scope; already accounted in gating Sections `0.3.6` and `0.3.11`",
                manifest,
            )
            self.assertIn("990b821c8", gating_plan)
            self.assertIn("49b3bcbb6", gating_plan)


if __name__ == "__main__":
    unittest.main()
