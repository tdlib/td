# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.cpp"


class FileLogTsanIntegrationTest(unittest.TestCase):
    def test_guarded_paths_cover_append_and_rotation(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        do_append_start = source.find(
            "void FileLog::do_append(int log_level, CSlice slice)"
        )
        after_rotation_start = source.find("void FileLog::after_rotation()")

        self.assertNotEqual(do_append_start, -1)
        self.assertNotEqual(after_rotation_start, -1)
        self.assertIn("auto lock = mutex_.lock();", source[do_append_start:])
        self.assertIn("auto lock = mutex_.lock();", source[after_rotation_start:])


if __name__ == "__main__":
    unittest.main()
