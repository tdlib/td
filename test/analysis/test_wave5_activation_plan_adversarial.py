# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_5_ACTIVATION_PLAN_2026-05-24.md"
)


class Wave5ActivationPlanAdversarialTest(unittest.TestCase):
    def test_transport_gate_must_not_be_conditional_on_suspected_code_touch(
        self,
    ) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "Cross-lane transport fixture gate is mandatory for every release candidate",
            plan,
        )
        self.assertNotIn(
            "if a Wave 5 change is suspected of touching network-observable behavior",
            plan,
        )

    def test_real_fixture_provenance_must_defend_against_synthetic_greenwashing(
        self,
    ) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("docs/Samples/Traffic dumps/", plan)
        self.assertIn("test/analysis/fixtures/", plan)
        self.assertIn(
            "Generated seeds are runtime stress inputs, not independent browser evidence.",
            plan,
        )
        self.assertIn("Real traffic dump corpus must be the evidence source", plan)

    def test_censorship_environment_constraints_are_explicit_non_goals_for_this_lane(
        self,
    ) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("ECH is blocked in the target censorship environment", plan)
        self.assertIn(
            "QUIC Ru-to-non-Ru blocking is treated as a transport non-regression sentinel",
            plan,
        )
        self.assertIn("must not rely on ECH availability as an evasion primitive", plan)

    def test_cross_lane_transport_regression_has_a_named_risk_and_gate(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("`W5R-13` | Cross-lane transport safety", plan)
        self.assertIn("TLS/ECH/QUIC browser-mimic fixture behavior", plan)
        self.assertIn("fixture smoke, real-dump provenance audit", plan)


if __name__ == "__main__":
    unittest.main()
