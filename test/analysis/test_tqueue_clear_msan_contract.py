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

    def test_runtime_clear_test_does_not_skip_msan_with_stale_map_rationale(
        self,
    ) -> None:
        source = normalize(TQUEUE_TEST_CPP)

        self.assertNotIn("SkipTest_TQueue_clearunderMSan", source)
        self.assertNotIn("std::mapreturn-pathfalsepositiveinclear()", source)


if __name__ == "__main__":
    unittest.main()
