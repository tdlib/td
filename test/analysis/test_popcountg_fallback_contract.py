# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLATFORM_HEADER = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "platform.h"


class PopcountgFallbackContractTest(unittest.TestCase):
    def test_platform_header_defines_clang_builtin_guard(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "#if TD_CLANG && defined(__has_builtin) && !__has_builtin(__builtin_popcountg)",
            source,
        )

    def test_platform_header_defines_popcountg_fallback_function(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        self.assertIn("td_builtin_popcountg_fallback", source)
        self.assertIn("if constexpr (sizeof(T) <= sizeof(unsigned int))", source)
        self.assertIn("if constexpr (sizeof(T) <= sizeof(unsigned long))", source)

    def test_platform_header_maps_builtin_popcountg_to_fallback(self) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "#define __builtin_popcountg(x) ::td_builtin_popcountg_fallback((x))",
            source,
        )


if __name__ == "__main__":
    unittest.main()
