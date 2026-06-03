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
PLAN_ANALYSIS_TESTS = (
    "test_wave5_activation_plan_adversarial.py",
    "test_wave5_activation_plan_contract.py",
    "test_wave5_activation_plan_integration.py",
    "test_wave5_activation_plan_light_fuzz.py",
    "test_wave5_activation_plan_stress.py",
)
LOCAL_ANCHORS = {
    "td/generate/scheme/td_api.tl": (
        "composeTextWithAi",
        "fixTextWithAi",
        "updateTextCompositionStyles",
        "internalLinkTypeTextCompositionStyle",
    ),
    "td/telegram/TranslationManager.cpp": (
        "is_valid_text_composition_style_slug",
        "get_update_text_composition_styles",
    ),
    "td/telegram/OptionManager.cpp": (
        'set_default_integer_option("text_composition_style_example_count", 7)',
        'set_default_integer_option("text_composition_style_title_length_max", 12)',
        'set_default_integer_option("text_composition_style_prompt_length_max", 1024)',
        'set_option_integer("added_text_composition_style_max"',
    ),
    "td/telegram/LinkManager.cpp": ("is_valid_text_composition_style_name",),
    "test/text_composition_control_plane_contract.cpp": (
        "TextCompositionControlPlaneContract",
    ),
    "test/text_composition_updates_manager_contract.cpp": (
        "TextCompositionUpdatesManagerContract",
    ),
    "test/text_composition_reload_contract.cpp": ("TextCompositionReloadContract",),
    "test/text_composition_link_contract.cpp": ("TextCompositionLinkContract",),
}


def extract_section(text: str, header: str, next_header: str) -> str:
    section_start = text.find(header)
    if section_start == -1:
        raise AssertionError(f"section header not found: {header}")
    section_end = text.find(next_header, section_start + len(header))
    if section_end == -1:
        return text[section_start:]
    return text[section_start:section_end]


class Wave5ActivationPlanIntegrationTest(unittest.TestCase):
    def test_local_baseline_provenance_map_links_upstream_rows_to_repository_anchors(
        self,
    ) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        provenance = extract_section(
            plan, "### 4.5 Local baseline provenance map", "## 5. Operating Principles"
        )

        for upstream_hash in (
            "528988dd9",
            "0c6ea7e09",
            "9571c262f",
            "ff051c4dc",
            "df4bfee0d",
            "c96e67c38",
            "58d72a0e8",
            "a26ccb8c5",
            "990b821c8",
        ):
            self.assertIn(upstream_hash, provenance)

        for local_path in LOCAL_ANCHORS:
            self.assertIn(local_path, provenance)

    def test_plan_provenance_anchors_exist_in_current_repository(self) -> None:
        for local_path, needles in LOCAL_ANCHORS.items():
            source = (REPO_ROOT / local_path).read_text(encoding="utf-8")
            for needle in needles:
                self.assertIn(needle, source)

    def test_upstream_commit_scope_notes_record_lifecycle_outliers(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        upstream_scope = extract_section(
            plan, "### 4.4 Upstream delta audit snapshot", "### 4.5"
        )

        self.assertIn(
            "`b77099227` touches `UpdatesManager.cpp`, `TranslationManager.*`, and `AiComposeTone.h`, but",
            upstream_scope,
        )
        self.assertIn(
            "`6113d3822` touches only `TranslationManager.cpp`", upstream_scope
        )
        self.assertIn(
            "`3678c2d42` is documentation-only in `td_api.tl`", upstream_scope
        )

    def test_plan_audit_suite_references_match_repository_files(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        analysis_dir = REPO_ROOT / "test" / "analysis"

        self.assertEqual(
            PLAN_ANALYSIS_TESTS,
            tuple(
                sorted(
                    path.name
                    for path in analysis_dir.glob("test_wave5_activation_plan_*.py")
                )
            ),
        )
        for test_name in PLAN_ANALYSIS_TESTS:
            self.assertIn(f"test/analysis/{test_name}", plan)
        self.assertIn(
            "python3 -m unittest discover -s test/analysis -p 'test_wave5_activation_plan_*.py' -v",
            plan,
        )

    def test_phase3_scope_guard_keeps_requests_touches_local_only(self) -> None:
        plan = PLAN_PATH.read_text(encoding="utf-8")
        phase_3 = extract_section(
            plan,
            "### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence",
            "### Phase 4",
        )

        self.assertIn(
            "If `Requests.*` is touched in this phase, it is strictly local-adaptation-only; upstream lifecycle rows do not",
            phase_3,
        )
        self.assertIn(
            "authorize new CRUD/public API surface here.",
            phase_3,
        )


if __name__ == "__main__":
    unittest.main()
