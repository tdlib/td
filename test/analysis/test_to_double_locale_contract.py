# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MISC_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "misc.cpp"


class ToDoubleLocaleContractTest(unittest.TestCase):
    def test_to_double_uses_locale_independent_parser(self) -> None:
        source = MISC_CPP.read_text(encoding="utf-8")

        self.assertNotIn("std::strtod(", source)
        self.assertTrue(
            any(
                token in source
                for token in ("strtod_l(", "_strtod_l(", "std::from_chars(")
            )
        )


if __name__ == "__main__":
    unittest.main()
