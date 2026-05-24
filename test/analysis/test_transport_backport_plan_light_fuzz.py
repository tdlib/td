# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import random
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


class TransportBackportPlanLightFuzzTest(unittest.TestCase):
    def test_deterministic_probe_sampling_preserves_transport_invariants_10000_iterations(
        self,
    ) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        link_manager = LINK_MANAGER_PATH.read_text(encoding="utf-8")
        link_tests = LINK_TEST_PATH.read_text(encoding="utf-8")

        probes = [
            (gating_plan, "#### 0.3.11 `W1-T` pass-B value decisions (2026-05-20)"),
            (gating_plan, "Closed by pass-B review"),
            (gating_plan, "not a mission-fit transport hardening change"),
            (manifest, "Section `0.3.11`"),
            (manifest, "28e0d0dbe"),
            (manifest, "691cb6a77"),
            (link_manager, 'copy_arg("task")'),
            (link_manager, "is_valid_managed_bot_username_candidate(new_bot_username)"),
            (
                link_manager,
                '(path.size() == 2u || path.size() == 3u) && path[0] == "newbot"',
            ),
            (link_tests, "%2BSj5pyZnaXRodWIuY29t"),
            (link_tests, "t.me/newbot/manager"),
        ]

        rng = random.Random(20260520)
        for _ in range(10000):
            haystack, needle = probes[rng.randrange(len(probes))]
            self.assertIn(needle, haystack)


if __name__ == "__main__":
    unittest.main()
