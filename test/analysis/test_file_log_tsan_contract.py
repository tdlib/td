# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
FILE_LOG_H = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.h"
FILE_LOG_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "FileLog.cpp"


class FileLogTsanContractTest(unittest.TestCase):
    def test_get_path_returns_owned_string_not_borrowed_slice(self) -> None:
        header = FILE_LOG_H.read_text(encoding="utf-8")
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        self.assertIn("string get_path() const;", header)
        self.assertNotIn("Slice get_path() const;", header)
        self.assertIn("string FileLog::get_path() const {", source)

    def test_header_declares_mutex_for_shared_state(self) -> None:
        header = FILE_LOG_H.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/port/Mutex.h"', header)
        self.assertIn("mutable Mutex mutex_;", header)
        self.assertIn("Status do_after_rotation_locked();", header)

    def test_filelog_public_state_accessors_lock_mutex(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        self.assertIn(
            "Status FileLog::init(string path, int64 rotate_threshold, bool redirect_stderr)",
            source,
        )
        self.assertIn("string FileLog::get_path() const", source)
        self.assertIn("vector<string> FileLog::get_file_paths()", source)
        self.assertIn(
            "void FileLog::set_rotate_threshold(int64 rotate_threshold)", source
        )
        self.assertIn("int64 FileLog::get_rotate_threshold() const", source)
        self.assertIn("bool FileLog::get_redirect_stderr() const", source)

        for marker in (
            "Status FileLog::init(string path, int64 rotate_threshold, bool redirect_stderr)",
            "string FileLog::get_path() const",
            "vector<string> FileLog::get_file_paths()",
            "void FileLog::set_rotate_threshold(int64 rotate_threshold)",
            "int64 FileLog::get_rotate_threshold() const",
            "bool FileLog::get_redirect_stderr() const",
            "void FileLog::do_append(int log_level, CSlice slice)",
            "void FileLog::after_rotation()",
        ):
            start = source.find(marker)
            self.assertNotEqual(start, -1, msg=f"Missing function marker: {marker}")
            self.assertIn(
                "auto lock = mutex_.lock();",
                source[start : start + 600],
                msg=f"Function `{marker}` must lock mutex_ before touching shared state",
            )

    def test_do_append_uses_lock_free_fatal_handoff(self) -> None:
        source = FILE_LOG_CPP.read_text(encoding="utf-8")

        self.assertIn("void FileLog::do_append(int log_level, CSlice slice)", source)
        self.assertIn("string fatal_error;", source)
        self.assertIn("if (!fatal_error.empty())", source)
        self.assertIn("process_fatal_error(fatal_error);", source)
        self.assertIn("Status FileLog::do_after_rotation_locked()", source)
        self.assertNotIn(
            "process_fatal_error(PSLICE()",
            source,
            msg="Fatal logging should happen after releasing mutex, not inline in locked helpers",
        )


if __name__ == "__main__":
    unittest.main()
