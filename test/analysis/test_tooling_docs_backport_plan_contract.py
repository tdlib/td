# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
README_PATH = REPO_ROOT / "example" / "README.md"
IOS_BUILD_OPENSSL_PATH = REPO_ROOT / "example" / "ios" / "build-openssl.sh"
IOS_BUILD_PATH = REPO_ROOT / "example" / "ios" / "build.sh"


class ToolingDocsBackportPlanContractTest(unittest.TestCase):
    def test_ios_openssl_build_uses_reproducible_env_contract(self) -> None:
        script = IOS_BUILD_OPENSSL_PATH.read_text(encoding="utf-8")

        self.assertIn(
            'SOURCE_DATE_EPOCH=1 ZERO_AR_DATE=1 make "OpenSSL-$target_platform"',
            script,
        )

    def test_ios_build_uses_deterministic_archive_dates_contract(self) -> None:
        script = IOS_BUILD_PATH.read_text(encoding="utf-8")

        self.assertIn("ZERO_AR_DATE=1 make -j3 install", script)

    def test_kotlin_wrapper_list_keeps_tdl_coroutines_contract(self) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "[tdl-coroutines](https://github.com/g000sha256/tdl-coroutines)",
            readme,
        )

    def test_javascript_wrapper_list_adds_react_native_tdlib_contract(self) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "[react-native-tdlib](https://github.com/vladlenskiy/react-native-tdlib)",
            readme,
        )

    def test_javascript_wrapper_list_mentions_react_native_platform_scope_contract(
        self,
    ) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "TDLib can also be used from React Native on iOS and Android.",
            readme,
        )

    def test_wrapper_section_mentions_third_party_security_review_contract(
        self,
    ) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "All listed wrappers are third-party community projects; audit and pin dependencies before production use.",
            readme,
        )


if __name__ == "__main__":
    unittest.main()
