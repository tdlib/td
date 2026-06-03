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


class PopcountgFallbackAdversarialTest(unittest.TestCase):
    def test_fallback_is_not_enabled_when_builtin_exists(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        self.assertIn("!__has_builtin(__builtin_popcountg)", source)

    def test_guard_scope_is_clang_only(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        self.assertIn("#if TD_CLANG", source)
        self.assertNotIn("#if TD_GCC && defined(__has_builtin)", source)

    def test_popcountg_rebinding_is_guarded_exactly_once(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        define_count = len(
            re.findall(
                r"#define\s+__builtin_popcountg\(x\)\s+::td_builtin_popcountg_fallback\(\(x\)\)",
                source,
            )
        )
        self.assertEqual(1, define_count)


if __name__ == "__main__":
    unittest.main()
