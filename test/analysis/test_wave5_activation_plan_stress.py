# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_WAVE_5_ACTIVATION_PLAN_2026-05-24.md"
)

EXPECTED_PHASE_ROWS = {
    "### Phase 1: Introduce owner model and structured persistence": (
        "d72be7609",
        "176915344",
        "a05aeeb9c",
        "fc903aab3",
        "3ba1e630b",
        "327531a54",
        "ee93de50b",
    ),
    "### Phase 2: Request surface and CRUD APIs": (
        "23971a844",
        "8a7d707ee",
        "27b1ee8cd",
        "64972181c",
        "86d375553",
        "3e10a17e6",
        "36e726f93",
    ),
    "### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence": (
        "b77099227",
        "6113d3822",
    ),
    "### Phase 4: Link and preview parity": (
        "f5b5a6e11",
        "3678c2d42",
    ),
    "### Phase 5: Auxiliary compile-closeout and release readiness": ("49b3bcbb6",),
}


def extract_rows(plan: str, phase_header: str) -> tuple[str, ...]:
    phase_start = plan.find(phase_header)
    if phase_start == -1:
        raise AssertionError(f"phase header not found: {phase_header}")
    next_phase = plan.find("\n### Phase ", phase_start + len(phase_header))
    phase = plan[phase_start:] if next_phase == -1 else plan[phase_start:next_phase]
    rows_start = phase.find("Rows:\n")
    if rows_start == -1:
        raise AssertionError(f"Rows block not found for {phase_header}")
    rows_end = phase.find("\nPrimary files:", rows_start)
    if rows_end == -1:
        raise AssertionError(f"Primary files block not found for {phase_header}")
    return tuple(re.findall(r"`([0-9a-f]{9})`", phase[rows_start:rows_end]))


class Wave5ActivationPlanStressTest(unittest.TestCase):
    def test_repeated_reads_keep_phase_commit_accounting_stable(self) -> None:
        for _ in range(2000):
            plan = PLAN_PATH.read_text(encoding="utf-8")
            for phase_header, expected_rows in EXPECTED_PHASE_ROWS.items():
                self.assertEqual(expected_rows, extract_rows(plan, phase_header))

    def test_each_activation_commit_has_one_phase_owner(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        phase_owner_count: dict[str, int] = {}

        for phase_header in EXPECTED_PHASE_ROWS:
            for upstream_hash in extract_rows(plan, phase_header):
                phase_owner_count[upstream_hash] = (
                    phase_owner_count.get(upstream_hash, 0) + 1
                )

        self.assertEqual(
            {
                upstream_hash: 1
                for rows in EXPECTED_PHASE_ROWS.values()
                for upstream_hash in rows
            },
            phase_owner_count,
        )


if __name__ == "__main__":
    unittest.main()
