# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.cpp"


class ThreadLocalDestructorLifecycleIntegrationTest(unittest.TestCase):
    def test_add_and_clear_paths_preserve_re_registration_guard(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        self.assertIn("static thread_local ThreadLocalDestructorGuard guard;", source)
        self.assertIn(
            "ThreadLocalDestructorGuard::~ThreadLocalDestructorGuard() {", source
        )
        self.assertIn("td::clear_thread_locals();", source)
        self.assertIn(
            "thread_local_destructors->push_back(std::move(destructor));", source
        )
        self.assertIn("detail::thread_local_destructors = nullptr;", source)
        self.assertIn("CHECK(detail::thread_local_destructors == nullptr);", source)


if __name__ == "__main__":
    unittest.main()
