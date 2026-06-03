# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"
TL_CORE_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_core.cpp"


class TlConfigLifetimeStressTest(unittest.TestCase):
    def test_repeated_scan_requires_cleanup_markers(self) -> None:
        config_source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        core_source = TL_CORE_CPP.read_text(encoding="utf-8")

        for _ in range(10000):
            self.assertGreaterEqual(config_source.count("destroy_zeroed(type);"), 1)
            self.assertGreaterEqual(config_source.count("destroy_zeroed(function);"), 1)
            self.assertGreaterEqual(core_source.count("delete arg_entry.type;"), 2)
            self.assertGreaterEqual(
                core_source.count("destroy_zeroed_tl_object(constructor);"), 1
            )
            self.assertNotIn("delete constructor;", core_source)


if __name__ == "__main__":
    unittest.main()
