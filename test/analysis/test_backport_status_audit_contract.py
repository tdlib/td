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
W2_PREFLIGHT_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_2_PREFLIGHT_2026-05-08.md"
)
W3_PREFLIGHT_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_3_PREFLIGHT_2026-05-11.md"
)
W4_PREFLIGHT_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_4_PREFLIGHT_2026-05-14.md"
)
W5_PREFLIGHT_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_5_PREFLIGHT_2026-05-14.md"
)
W6_PREFLIGHT_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_6_PREFLIGHT_2026-05-19.md"
)


def read_normalized_text(path: pathlib.Path) -> str:
    return " ".join(path.read_text(encoding="utf-8").split())


class BackportStatusAuditContractTest(unittest.TestCase):
    def test_gating_plan_reports_zero_wave_level_backlog_contract(self) -> None:
        gating_plan = read_normalized_text(GATING_PLAN_PATH)

        self.assertIn("### 0.1 Repository Audit Snapshot (2026-05-20)", gating_plan)
        self.assertIn(
            "`0` historical waves (`W1-T` through `W8-X`) remain open on a bounded implementation basis",
            gating_plan,
        )
        self.assertIn(
            "`4` follow-on audit waves are open: `W9-R`, `W10-V`, `W11-AI2`, and `W12-M2`",
            gating_plan,
        )
        self.assertIn(
            "`2` follow-on waves still depend on explicit activation or prerequisite approval: `W11-AI2` and `W12-M2`",
            gating_plan,
        )
        self.assertIn(
            "raw upstream-row counters below remain provenance-only and are no longer a live engineering backlog metric",
            gating_plan,
        )

    def test_gating_plan_closes_historical_open_waves_contract(self) -> None:
        gating_plan = read_normalized_text(GATING_PLAN_PATH)

        required_closure_phrases = (
            "Closed by repo audit; repository-resident poll implementation/test families now cover the wave scope",
            "Closed by repo audit; guest-query and business-guest implementation/test families now cover the wave scope",
            "Historical wave closed only as a bounded local-equivalent slice; the deferred exact-scope owner/product backlog now lives in `W11-AI2`",
            "Historical wave closed only as a bounded token/link slice; deferred access-settings activation now lives in `W12-M2`",
            "Closed by repo audit; repository files already contain the tooling/docs deltas",
            "Closed by repo audit; residual rows are now either repository-resident hardening or consumed by the closed W3/W5/W6/W7 slices",
        )

        for phrase in required_closure_phrases:
            self.assertIn(phrase, gating_plan)

    def test_manifest_declares_wave_level_backlog_closed_contract(self) -> None:
        manifest = read_normalized_text(MANIFEST_PATH)

        self.assertIn(
            'did not sustain the older "no remaining wave-level backlog" conclusion',
            manifest,
        )
        self.assertIn(
            "Live follow-on work from that audit is now tracked only in the gating plan as `W9-R`",
            manifest,
        )
        self.assertIn(
            "lane and class columns below remain the historical Pass A intake record for the upstream backlog",
            manifest,
        )

    def test_wave_preflight_annexes_are_marked_as_historical_archives_contract(
        self,
    ) -> None:
        archive_expectations = {
            W2_PREFLIGHT_PATH: (
                "Historical preflight archive; Wave 2 execution is closed",
                "original shortlist/contract/validation record for Wave 2",
            ),
            W3_PREFLIGHT_PATH: (
                "Historical preflight archive; the repository audit recorded",
                "activation rules below are preserved as archival planning criteria, not live blockers",
            ),
            W4_PREFLIGHT_PATH: (
                "Historical preflight archive; the repository audit recorded",
                "activation rules below are preserved as archival planning criteria, not live blockers",
            ),
            W5_PREFLIGHT_PATH: (
                "Historical preflight archive; the repository audit recorded",
                "activation rules below are preserved as archival planning criteria, not live blockers",
            ),
            W6_PREFLIGHT_PATH: (
                "Historical preflight archive; the repository audit recorded",
                "activation rules below are preserved as archival planning criteria, not live blockers",
            ),
        }

        for path, phrases in archive_expectations.items():
            text = read_normalized_text(path)
            for phrase in phrases:
                self.assertIn(phrase, text)


if __name__ == "__main__":
    unittest.main()
