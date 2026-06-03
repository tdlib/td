# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLATFORM_HEADER = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "platform.h"


class PopcountgFallbackStressTest(unittest.TestCase):
    def test_repeated_reads_keep_guard_and_macro_singleton_stable(self) -> None:
        guard = "#if TD_CLANG && defined(__has_builtin) && !__has_builtin(__builtin_popcountg)"
        macro_pattern = re.compile(
            r"#define\s+__builtin_popcountg\(x\)\s+::td_builtin_popcountg_fallback\(\(x\)\)"
        )

        for _ in range(2000):
            source = PLATFORM_HEADER.read_text(encoding="utf-8")
            self.assertIn(guard, source)
            self.assertEqual(1, len(macro_pattern.findall(source)))


if __name__ == "__main__":
    unittest.main()
