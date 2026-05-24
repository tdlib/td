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
TD_API_PATH = REPO_ROOT / "td" / "generate" / "scheme" / "td_api.tl"
POLL_MANAGER_PATH = REPO_ROOT / "td" / "telegram" / "PollManager.cpp"
MESSAGE_CONTENT_PATH = REPO_ROOT / "td" / "telegram" / "MessageContent.cpp"
MESSAGES_MANAGER_PATH = REPO_ROOT / "td" / "telegram" / "MessagesManager.cpp"

ADAPTED_COMMITS = (
    "1eaf2481e",
    "04498cfbb",
    "d6ef00fa9",
    "1574780ca",
    "1f68a4a84",
    "c6411b9c9",
)
REJECTED_COMMITS = (
    "978979edb",
    "ca82791de",
    "aaea672ae",
)


def extract_poll_pass_b_section(gating_plan: str) -> str:
    section_header = "#### 0.3.3.a `W3-P` pass-B value decisions (2026-05-20)"
    section_start = gating_plan.find(section_header)
    if section_start == -1:
        raise AssertionError("W3 pass-B section header not found")

    next_section_start = gating_plan.find("\n#### 0.3.4 ", section_start)
    if next_section_start == -1:
        return gating_plan[section_start:]
    return gating_plan[section_start:next_section_start]


def get_manifest_line(manifest: str, commit: str) -> str:
    for line in manifest.splitlines():
        if f"`{commit}`" in line:
            return line
    raise AssertionError(f"manifest row for {commit} not found")


class PollBackportPlanIntegrationTest(unittest.TestCase):
    def test_manifest_rows_reference_poll_pass_b_anchor(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in ADAPTED_COMMITS + REJECTED_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("Section `0.3.3.a`", line)

    def test_manifest_distinguishes_adapted_and_rejected_rows(self) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        for commit in ADAPTED_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("adapted", line)

        for commit in REJECTED_COMMITS:
            line = get_manifest_line(manifest, commit)
            self.assertIn("not adapted", line)

    def test_local_poll_surfaces_match_poll_accounting(self) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        section = extract_poll_pass_b_section(gating_plan)
        td_api_tl = TD_API_PATH.read_text(encoding="utf-8")
        poll_manager = POLL_MANAGER_PATH.read_text(encoding="utf-8")
        message_content = MESSAGE_CONTENT_PATH.read_text(encoding="utf-8")
        messages_manager = MESSAGES_MANAGER_PATH.read_text(encoding="utf-8")

        self.assertIn("1eaf2481e", section)
        self.assertIn("04498cfbb", section)
        self.assertIn("d6ef00fa9", section)
        self.assertIn("1574780ca", section)
        self.assertIn("1f68a4a84", section)
        self.assertIn("c6411b9c9", section)

        self.assertIn("contains_unread_poll_votes:Bool", td_api_tl)
        self.assertIn("updateMessageContainsUnreadPollVotes", td_api_tl)
        self.assertIn("has_message_pending_read_poll_votes", poll_manager)
        self.assertIn("is_real_message_content", poll_manager)
        self.assertIn("vote_restriction_reason_tag", poll_manager)
        self.assertIn("read_all_local_dialog_poll_votes", messages_manager)
        self.assertIn(
            "send_update_message_contains_unread_poll_votes", messages_manager
        )
        self.assertIn("pollVoteRestrictionReasonOther", message_content)


if __name__ == "__main__":
    unittest.main()
