# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
README_PATH = REPO_ROOT / "example" / "README.md"
MANIFEST_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md"
)
GATING_PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md"
)
IOS_BUILD_OPENSSL_PATH = REPO_ROOT / "example" / "ios" / "build-openssl.sh"
IOS_BUILD_PATH = REPO_ROOT / "example" / "ios" / "build.sh"


class ToolingDocsBackportPlanStressTest(unittest.TestCase):
    def test_repeated_source_reads_keep_tooling_docs_contracts_stable(self) -> None:
        iterations = 2000

        for _ in range(iterations):
            readme = README_PATH.read_text(encoding="utf-8")
            manifest = MANIFEST_PATH.read_text(encoding="utf-8")
            gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
            openssl_script = IOS_BUILD_OPENSSL_PATH.read_text(encoding="utf-8")
            build_script = IOS_BUILD_PATH.read_text(encoding="utf-8")

            self.assertEqual(
                1,
                readme.count(
                    "[tdl-coroutines](https://github.com/g000sha256/tdl-coroutines)"
                ),
            )
            self.assertEqual(
                1,
                readme.count(
                    "[react-native-tdlib](https://github.com/vladlenskiy/react-native-tdlib)"
                ),
            )
            self.assertEqual(
                1,
                readme.count(
                    "TDLib can also be used from React Native on iOS and Android.",
                ),
            )
            self.assertEqual(
                1,
                readme.count(
                    "All listed wrappers are third-party community projects; audit and pin dependencies before production use.",
                ),
            )
            self.assertIn(
                "#### 0.3.10 `W7-D` pass-B value decisions (2026-05-20)", gating_plan
            )
            self.assertIn("Section `0.3.10`", manifest)
            self.assertEqual(
                1,
                openssl_script.count(
                    'SOURCE_DATE_EPOCH=1 ZERO_AR_DATE=1 make "OpenSSL-$target_platform"',
                ),
            )
            self.assertEqual(1, build_script.count("ZERO_AR_DATE=1 make -j3 install"))


if __name__ == "__main__":
    unittest.main()
