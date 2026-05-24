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
TD_API_PATH = REPO_ROOT / "td" / "generate" / "scheme" / "td_api.tl"
MESSAGES_MANAGER_PATH = REPO_ROOT / "td" / "telegram" / "MessagesManager.cpp"
POLL_MANAGER_PATH = REPO_ROOT / "td" / "telegram" / "PollManager.cpp"
MESSAGE_CONTENT_PATH = REPO_ROOT / "td" / "telegram" / "MessageContent.cpp"


def extract_poll_pass_b_section(gating_plan: str) -> str:
    section_header = "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)"
    section_start = gating_plan.find(section_header)
    if section_start == -1:
        raise AssertionError("W3 pass-B section header not found")

    next_section_start = gating_plan.find("\n#### 0.3.4 ", section_start)
    if next_section_start == -1:
        return gating_plan[section_start:]
    return gating_plan[section_start:next_section_start]


class PollBackportPlanLightFuzzTest(unittest.TestCase):
    def test_deterministic_probe_sampling_preserves_poll_invariants_10000_iterations(
        self,
    ) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        section = extract_poll_pass_b_section(gating_plan)
        td_api_tl = TD_API_PATH.read_text(encoding="utf-8")
        messages_manager = MESSAGES_MANAGER_PATH.read_text(encoding="utf-8")
        poll_manager = POLL_MANAGER_PATH.read_text(encoding="utf-8")
        message_content = MESSAGE_CONTENT_PATH.read_text(encoding="utf-8")

        probes = [
            (gating_plan, "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)"),
            (section, "1eaf2481e"),
            (section, "04498cfbb"),
            (section, "d6ef00fa9"),
            (section, "1574780ca"),
            (section, "1f68a4a84"),
            (section, "c6411b9c9"),
            (manifest, "Section `0.3.3.a`"),
            (manifest, "978979edb"),
            (manifest, "aaea672ae"),
            (td_api_tl, "contains_unread_poll_votes:Bool"),
            (td_api_tl, "updateMessageContainsUnreadPollVotes"),
            (messages_manager, "send_update_message_contains_unread_poll_votes"),
            (poll_manager, "has_message_pending_read_poll_votes"),
            (poll_manager, "vote_restriction_reason_tag"),
            (message_content, "pollVoteRestrictionReasonOther"),
        ]

        rng = random.Random(20260520)
        for _ in range(10000):
            haystack, needle = probes[rng.randrange(len(probes))]
            self.assertIn(needle, haystack)


if __name__ == "__main__":
    unittest.main()
