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
LINK_MANAGER_PATH = REPO_ROOT / "td" / "telegram" / "LinkManager.cpp"
LINK_TEST_PATH = REPO_ROOT / "test" / "link.cpp"

TRANSPORT_DIRECT_PASS_B_COMMITS = (
    "28e0d0dbe",
    "00eedc5f9",
    "a82128ab8",
    "bfab03f7a",
    "8921c22f0",
    "e86cd4496",
    "dd78f94a8",
    "691cb6a77",
)
TRANSPORT_CROSS_WAVE_COMMITS = (
    "990b821c8",
    "49b3bcbb6",
)


def get_manifest_line(manifest: str, commit: str) -> str:
    for line in manifest.splitlines():
        if f"`{commit}`" in line:
            return line
    raise AssertionError(f"manifest row for {commit} not found")


class TransportBackportPlanIntegrationTest(unittest.TestCase):
    def test_manifest_rows_reference_transport_pass_b_anchor(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in TRANSPORT_DIRECT_PASS_B_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("Section `0.3.11`", line)

    def test_cross_lane_rows_reference_transport_and_text_composition_anchors(
        self,
    ) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in TRANSPORT_CROSS_WAVE_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("`0.3.11`", line)
            self.assertIn("`0.3.6`", line)

    def test_local_link_surfaces_match_transport_accounting(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        link_manager = LINK_MANAGER_PATH.read_text(encoding="utf-8")
        link_tests = LINK_TEST_PATH.read_text(encoding="utf-8")

        self.assertIn("8921c22f0", gating_plan)
        self.assertIn('copy_arg("task")', link_manager)
        self.assertIn('copy_arg("option")', link_manager)
        self.assertIn(
            "is_valid_managed_bot_username_candidate(new_bot_username)",
            link_manager,
        )
        self.assertIn(
            "static bool is_valid_managed_bot_username_candidate(Slice bot_username)",
            link_manager,
        )
        self.assertIn(
            '(path.size() == 2u || path.size() == 3u) && path[0] == "newbot"',
            link_manager,
        )
        self.assertIn("%2BSj5pyZnaXRodWIuY29t", link_tests)
        self.assertIn("%2FSj5pyZnaXRodWIuY29t", link_tests)
        self.assertIn("t.me/newbot/manager", link_tests)


if __name__ == "__main__":
    unittest.main()
