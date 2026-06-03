# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import random
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
THREAD_LOCAL_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "thread_local.cpp"


class ThreadLocalDestructorLifecycleLightFuzzTest(unittest.TestCase):
    def test_randomized_whitespace_scan_finds_no_raw_delete(self) -> None:
        source = THREAD_LOCAL_CPP.read_text(encoding="utf-8")
        rng = random.Random(20260527)

        for _ in range(1000):
            collapsed = re.sub(r"\s+", " " if rng.random() < 0.5 else "", source)
            self.assertNotIn("deleteto_delete;", collapsed)


if __name__ == "__main__":
    unittest.main()
