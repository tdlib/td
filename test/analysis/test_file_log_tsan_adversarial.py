# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.cpp"


class FileLogTsanAdversarialTest(unittest.TestCase):
    def test_do_append_locks_before_state_access(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        body_start = source.find("void FileLog::do_append(int log_level, CSlice slice)")
        self.assertNotEqual(body_start, -1)

        lock_pos = source.find("auto lock = mutex_.lock();", body_start)
        rotate_check_pos = source.find("if (size_ > rotate_threshold_", body_start)
        write_pos = source.find("auto r_size = fd_.write(slice);", body_start)

        self.assertGreaterEqual(lock_pos, 0)
        self.assertGreaterEqual(rotate_check_pos, 0)
        self.assertGreaterEqual(write_pos, 0)
        self.assertLess(lock_pos, rotate_check_pos)
        self.assertLess(lock_pos, write_pos)

    def test_after_rotation_locks_before_reopen(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        start = source.find("void FileLog::after_rotation()")
        self.assertNotEqual(start, -1)
        lock_pos = source.find("auto lock = mutex_.lock();", start)
        reopen_pos = source.find("do_after_rotation_locked();", start)

        self.assertGreaterEqual(lock_pos, 0)
        self.assertGreaterEqual(reopen_pos, 0)
        self.assertLess(lock_pos, reopen_pos)

    def test_rotation_helper_keeps_fatal_handling_outside_locked_code(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        helper_start = source.find("Status FileLog::do_after_rotation_locked()")
        self.assertNotEqual(helper_start, -1)
        helper_slice = source[helper_start : helper_start + 1200]

        self.assertNotIn(
            "process_fatal_error(",
            helper_slice,
            msg=(
                "Rotation helper must propagate Status and let callers trigger process_fatal_error "
                "after lock release"
            ),
        )


if __name__ == "__main__":
    unittest.main()
