# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.cpp"


class ThreadLocalDestructorLifecycleAdversarialTest(unittest.TestCase):
    def test_clear_phase_captures_ownership_with_unique_ptr(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        self.assertIn("auto &guard = get_thread_local_destructor_guard();", source)
        self.assertIn(
            "std::unique_ptr<std::vector<unique_ptr<Destructor>>> to_delete",
            source,
        )

    def test_old_raw_pointer_cleanup_path_is_removed(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        self.assertNotIn("auto to_delete = detail::thread_local_destructors;", source)


if __name__ == "__main__":
    unittest.main()
