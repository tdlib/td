# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WIRE_CLASSIFIER_CPP = REPO_ROOT / "test" / "stealth" / "WireClassifierFeatures.cpp"


class WireClassifierFixtureWalkAdversarialTest(unittest.TestCase):
    def test_fixture_listing_rejects_raw_posix_directory_iteration_regression(
        self,
    ) -> None:
        source = WIRE_CLASSIFIER_CPP.read_text(encoding="utf-8")

        for token in ("opendir(", "readdir(", "closedir(", "lstat("):
            with self.subTest(token=token):
                self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
