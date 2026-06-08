# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TQUEUE_CPP = REPO_ROOT / "tddb" / "td" / "db" / "TQueue.cpp"
TQUEUE_TEST_CPP = REPO_ROOT / "test" / "tqueue.cpp"


def normalize(path: pathlib.Path) -> str:
    return "".join(path.read_text(encoding="utf-8").split())


class TQueueClearMsanContractTest(unittest.TestCase):
    def test_clear_function_is_not_broadly_memory_sanitizer_suppressed(self) -> None:
        source = normalize(TQUEUE_CPP)

        self.assertNotIn(
            "std::map<EventId,RawEvent>clear(QueueIdqueue_id,size_tkeep_count)finalTD_TQUEUE_NO_SANITIZE_MEMORY{",
            source,
        )

    def test_clear_appends_deleted_events_without_lower_bound_tree_search(self) -> None:
        source = normalize(TQUEUE_CPP)

        self.assertIn("staticbooladd_deleted_event(", source)
        self.assertIn("deleted_events.emplace_hint(deleted_events.end(),event_id,std::move(raw_event))", source)
        self.assertIn("staticvoidunpoison_deleted_events_if_msan(", source)
        self.assertIn("__msan_unpoison(&deleted_events,sizeof(deleted_events));", source)
        self.assertIn("__msan_unpoison(node_ptr,sizeof(TreeNode));", source)
        self.assertNotIn("deleted_events.emplace(it->first,std::move(it->second))", source)

    def test_runtime_clear_test_does_not_skip_msan_with_stale_map_rationale(
        self,
    ) -> None:
        source = normalize(TQUEUE_TEST_CPP)

        self.assertNotIn("SkipTest_TQueue_clearunderMSan", source)
        self.assertNotIn("std::mapreturn-pathfalsepositiveinclear()", source)


if __name__ == "__main__":
    unittest.main()
