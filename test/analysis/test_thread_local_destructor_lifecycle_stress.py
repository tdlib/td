# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.cpp"


class ThreadLocalDestructorLifecycleStressTest(unittest.TestCase):
    def test_repeated_source_validation_for_safe_cleanup_markers(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        for _ in range(10000):
            self.assertIn(
                "static thread_local ThreadLocalDestructorGuard guard;", source
            )
            self.assertIn(
                "thread_local_destructors->push_back(std::move(destructor));", source
            )
            self.assertIn("detail::thread_local_destructors = nullptr;", source)
            self.assertNotIn("delete to_delete;", source)


if __name__ == "__main__":
    unittest.main()
