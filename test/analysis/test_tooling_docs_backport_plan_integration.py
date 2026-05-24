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


class ToolingDocsBackportPlanIntegrationTest(unittest.TestCase):
    def test_gating_plan_has_tooling_docs_pass_b_section_with_all_three_commits(
        self,
    ) -> None:
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "#### 0.3.10 `W7-D` pass-B value decisions (2026-05-20)", gating_plan
        )
        self.assertIn("3bde4782c", gating_plan)
        self.assertIn("f3713bba0", gating_plan)
        self.assertIn("ed87ce103", gating_plan)
        self.assertIn("Valuable and adapted", gating_plan)
        self.assertIn("Assessed but intentionally not adapted", gating_plan)

    def test_manifest_tooling_docs_rows_reference_tooling_docs_pass_b_anchor(
        self,
    ) -> None:
        manifest = MANIFEST_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "| 15 | `3bde4782c` |",
            manifest,
        )
        self.assertIn(
            "| 16 | `f3713bba0` |",
            manifest,
        )
        self.assertIn(
            "| 197 | `ed87ce103` |",
            manifest,
        )
        self.assertIn("Section `0.3.10`", manifest)

    def test_readme_and_ios_scripts_are_consistent_with_tooling_docs_value_scope(
        self,
    ) -> None:
        readme = README_PATH.read_text(encoding="utf-8")
        openssl_script = IOS_BUILD_OPENSSL_PATH.read_text(encoding="utf-8")
        build_script = IOS_BUILD_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "TDLib can also be used from React Native on iOS and Android.",
            readme,
        )
        self.assertIn(
            "[react-native-tdlib](https://github.com/vladlenskiy/react-native-tdlib)",
            readme,
        )
        self.assertIn(
            "All listed wrappers are third-party community projects; audit and pin dependencies before production use.",
            readme,
        )
        self.assertIn(
            'SOURCE_DATE_EPOCH=1 ZERO_AR_DATE=1 make "OpenSSL-$target_platform"',
            openssl_script,
        )
        self.assertIn("ZERO_AR_DATE=1 make -j3 install", build_script)


if __name__ == "__main__":
    unittest.main()
