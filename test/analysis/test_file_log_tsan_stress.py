# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.cpp"


class FileLogTsanStressTest(unittest.TestCase):
    def test_repeated_scan_requires_two_lock_sites(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        for _ in range(10000):
            self.assertGreaterEqual(source.count("auto lock = mutex_.lock();"), 2)


if __name__ == "__main__":
    unittest.main()
