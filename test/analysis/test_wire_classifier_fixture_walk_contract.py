# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WIRE_CLASSIFIER_CPP = REPO_ROOT / "test" / "stealth" / "WireClassifierFeatures.cpp"


class WireClassifierFixtureWalkContractTest(unittest.TestCase):
    def test_fixture_listing_uses_repo_walk_path_abstraction(self) -> None:
        source = WIRE_CLASSIFIER_CPP.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/port/path.h"', source)
        self.assertIn("WalkPath::run(", source)

    def test_fixture_listing_no_longer_depends_on_posix_directory_headers(self) -> None:
        source = WIRE_CLASSIFIER_CPP.read_text(encoding="utf-8")

        self.assertNotIn("#include <dirent.h>", source)
        self.assertNotIn("#include <sys/stat.h>", source)


if __name__ == "__main__":
    unittest.main()
