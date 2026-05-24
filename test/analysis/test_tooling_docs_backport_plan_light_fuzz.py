# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import random
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
README_PATH = REPO_ROOT / "example" / "README.md"
GATING_PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md"
)
IOS_BUILD_OPENSSL_PATH = REPO_ROOT / "example" / "ios" / "build-openssl.sh"
IOS_BUILD_PATH = REPO_ROOT / "example" / "ios" / "build.sh"


class ToolingDocsBackportPlanLightFuzzTest(unittest.TestCase):
    def test_deterministic_probe_sampling_preserves_tooling_docs_invariants_10000_iterations(
        self,
    ) -> None:
        readme = README_PATH.read_text(encoding="utf-8")
        gating_plan = GATING_PLAN_PATH.read_text(encoding="utf-8")
        openssl_script = IOS_BUILD_OPENSSL_PATH.read_text(encoding="utf-8")
        build_script = IOS_BUILD_PATH.read_text(encoding="utf-8")

        probes = [
            (readme, "[tdl-coroutines](https://github.com/g000sha256/tdl-coroutines)"),
            (
                readme,
                "TDLib can also be used from React Native on iOS and Android.",
            ),
            (
                readme,
                "[react-native-tdlib](https://github.com/vladlenskiy/react-native-tdlib)",
            ),
            (
                readme,
                "All listed wrappers are third-party community projects; audit and pin dependencies before production use.",
            ),
            (gating_plan, "#### 0.3.10 `W7-D` pass-B value decisions (2026-05-20)"),
            (gating_plan, "3bde4782c"),
            (gating_plan, "f3713bba0"),
            (gating_plan, "ed87ce103"),
            (
                openssl_script,
                'SOURCE_DATE_EPOCH=1 ZERO_AR_DATE=1 make "OpenSSL-$target_platform"',
            ),
            (build_script, "ZERO_AR_DATE=1 make -j3 install"),
        ]

        rng = random.Random(20260520)
        for _ in range(10000):
            haystack, needle = probes[rng.randrange(len(probes))]
            self.assertIn(needle, haystack)


if __name__ == "__main__":
    unittest.main()
