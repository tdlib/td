# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.cpp"


class ThreadLocalDestructorLifecycleContractTest(unittest.TestCase):
    def test_add_path_uses_make_unique_vector_allocation(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        self.assertIn(
            "std::make_unique<std::vector<unique_ptr<Destructor>>>()",
            source,
        )

    def test_clear_path_avoids_raw_delete_operator(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")

        self.assertNotIn("delete to_delete;", source)


if __name__ == "__main__":
    unittest.main()
