# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import random
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"


class TlConfigLifetimeLightFuzzTest(unittest.TestCase):
    def test_random_whitespace_collapse_keeps_ownership_flow_markers(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        rng = random.Random(20260527)

        for _ in range(1000):
            collapsed = re.sub(r"\s+", " " if rng.random() < 0.5 else "", source)
            compact = collapsed.replace(" ", "")
            self.assertIn("voidtl_config::clear()", compact)
            self.assertIn("destroy_zeroed(type);", compact)
            self.assertIn("destroy_zeroed(function);", compact)
            self.assertIn("returnstd::move(config);", compact)


if __name__ == "__main__":
    unittest.main()
