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


class TransportBackportPlanAdversarialTest(unittest.TestCase):
    def test_coverage_matrix_must_not_claim_transport_has_no_executed_rows_anymore(
        self,
    ) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertNotIn("no executed `W1-T` rows yet", gating_plan)
        self.assertIn("Closed by pass-B review", gating_plan)

    def test_darwin_tlsinit_pair_must_stay_rejected_as_direct_backport(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("28e0d0dbe", gating_plan)
        self.assertIn("00eedc5f9", gating_plan)
        self.assertIn("per-connection", gating_plan)
        self.assertIn("reject direct backport", gating_plan)

    def test_proxy_comment_expansion_must_not_be_treated_as_transport_hardening(
        self,
    ) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("691cb6a77", gating_plan)
        self.assertIn("maximum length", gating_plan)
        self.assertIn("not a mission-fit transport hardening change", gating_plan)


if __name__ == "__main__":
    unittest.main()
