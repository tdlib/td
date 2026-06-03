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


def extract_section(text: str, header: str, next_header_prefix: str) -> str:
    section_start = text.find(header)
    if section_start == -1:
        raise AssertionError(f"section header not found: {header}")
    next_section_start = text.find(next_header_prefix, section_start + len(header))
    if next_section_start == -1:
        return text[section_start:]
    return text[section_start:next_section_start]


def extract_phase_rows(text: str, phase_header: str) -> tuple[str, ...]:
    phase = extract_section(text, phase_header, "\n### Phase ")
    rows_start = phase.find("Rows:\n")
    if rows_start == -1:
        raise AssertionError(f"Rows block not found for {phase_header}")
    rows_end = phase.find("\nPrimary files:", rows_start)
    if rows_end == -1:
        raise AssertionError(f"Primary files block not found for {phase_header}")
    return tuple(re.findall(r"`([0-9a-f]{9})`", phase[rows_start:rows_end]))


def extract_phase_primary_files(text: str, phase_header: str) -> tuple[str, ...]:
    phase = extract_section(text, phase_header, "\n### Phase ")
    primary_start = phase.find("Primary files:\n")
    if primary_start == -1:
        raise AssertionError(f"Primary files block not found for {phase_header}")
    end_candidates = []
    for label in ("\n\nLocal baseline rows", "\n\nRequirements:"):
        candidate = phase.find(label, primary_start)
        if candidate != -1:
            end_candidates.append(candidate)
    primary_end = min(end_candidates) if end_candidates else len(phase)
    return tuple(re.findall(r"`([^`]+)`", phase[primary_start:primary_end]))


class Wave5ActivationPlanContractTest(unittest.TestCase):
    def test_backlog_anchor_uses_existing_origin_remote_contract(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("**Backlog anchor:** `origin/master..upstream/master`", plan)
        self.assertNotIn("**Backlog anchor:** `original..upstream/master`", plan)

    def test_upstream_baseline_tag_uses_policy_twelve_character_hash(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("upstream-baseline-2026-05-24-e0943d068ce9", plan)
        self.assertIn("upstream-reference/2026-05-24-e0943d068ce9", plan)

    def test_activation_scope_names_cross_wave_closeout_explicitly(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "18 deferred `W5-AI` rows plus the cross-wave compile-closeout row", plan
        )
        self.assertIn(
            "`49b3bcbb6` is a cross-wave `W1-T`/`W5-AI` compile-closeout row", plan
        )

    def test_lifecycle_commits_are_not_request_surface_rows(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        phase_2_rows = extract_phase_rows(
            plan, "### Phase 2: Request surface and CRUD APIs"
        )
        phase_3_rows = extract_phase_rows(
            plan,
            "### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence",
        )

        self.assertNotIn("b77099227", phase_2_rows)
        self.assertNotIn("6113d3822", phase_2_rows)
        self.assertIn("b77099227", phase_3_rows)
        self.assertIn("6113d3822", phase_3_rows)

    def test_phase_rows_match_upstream_file_scope_contract(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertEqual(
            (
                "d72be7609",
                "176915344",
                "a05aeeb9c",
                "fc903aab3",
                "3ba1e630b",
                "327531a54",
                "ee93de50b",
            ),
            extract_phase_rows(
                plan, "### Phase 1: Introduce owner model and structured persistence"
            ),
        )
        self.assertEqual(
            (
                "23971a844",
                "8a7d707ee",
                "27b1ee8cd",
                "64972181c",
                "86d375553",
                "3e10a17e6",
                "36e726f93",
            ),
            extract_phase_rows(plan, "### Phase 2: Request surface and CRUD APIs"),
        )
        self.assertEqual(
            ("b77099227", "6113d3822"),
            extract_phase_rows(
                plan,
                "### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence",
            ),
        )
        self.assertEqual(
            ("f5b5a6e11", "3678c2d42"),
            extract_phase_rows(plan, "### Phase 4: Link and preview parity"),
        )
        self.assertEqual(
            ("49b3bcbb6",),
            extract_phase_rows(
                plan, "### Phase 5: Auxiliary compile-closeout and release readiness"
            ),
        )

    def test_phase3_primary_files_expose_owner_header_coupling(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertEqual(
            (
                "td/telegram/UpdatesManager.*",
                "td/telegram/TranslationManager.*",
                "td/telegram/AiComposeTone.h",
                "td/telegram/Requests.*",
            ),
            extract_phase_primary_files(
                plan,
                "### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence",
            ),
        )

    def test_plan_analysis_tests_are_explicit_first_class_gates(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "Plan-audit suites that must stay green while this document evolves:",
            plan,
        )
        self.assertIn(
            "test/analysis/test_wave5_activation_plan_contract.py",
            plan,
        )
        self.assertIn(
            "Every added C++ runtime test file must be wired in `test/CMakeLists.txt` in the same patch.",
            plan,
        )
        self.assertIn(
            "Python documentation-analysis tests under `test/analysis/` must stay reachable through repository-standard",
            plan,
        )
        self.assertIn(
            "python3 -m unittest discover -s test/analysis -p 'test_wave5_activation_plan_*.py' -v",
            plan,
        )


if __name__ == "__main__":
    unittest.main()
