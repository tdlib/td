# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import random
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLATFORM_HEADER = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "platform.h"


class PopcountgFallbackLightFuzzTest(unittest.TestCase):
    def test_probe_sampling_preserves_required_fallback_tokens_10000_iterations(
        self,
    ) -> None:
        source = PLATFORM_HEADER.read_text(encoding="utf-8")
        probes = (
            "TD_CLANG",
            "__has_builtin",
            "__builtin_popcountg",
            "td_builtin_popcountg_fallback",
            "__builtin_popcount(",
            "__builtin_popcountl(",
            "__builtin_popcountll(",
        )

        rng = random.Random(20260526)
        for _ in range(10000):
            self.assertIn(probes[rng.randrange(len(probes))], source)


if __name__ == "__main__":
    unittest.main()
