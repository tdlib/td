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
FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.cpp"


class FileLogTsanLightFuzzTest(unittest.TestCase):
    def test_random_whitespace_transform_preserves_lock_markers(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")
        rng = random.Random(20260527)

        for _ in range(1000):
            collapsed = re.sub(r"\s+", " " if rng.random() < 0.5 else "", source)
            self.assertIn("autolock=mutex_.lock();", collapsed.replace(" ", ""))


if __name__ == "__main__":
    unittest.main()
