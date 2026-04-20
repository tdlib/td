# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TEST_CMAKE_PATH = REPO_ROOT / "test" / "CMakeLists.txt"
REFERENCES_CPP_PATH = REPO_ROOT / "test" / "stealth" / "ReviewedClientHelloReferences.cpp"
STEALTH_ROOT = REPO_ROOT / "td" / "mtproto" / "stealth"


class BuildPipelinePhase4ContractTest(unittest.TestCase):
    def test_phase4_compiled_reviewed_reference_unit_registered(self) -> None:
        test_cmake = TEST_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "stealth/ReviewedClientHelloReferences.cpp",
            test_cmake,
            msg="phase 4 must register compiled reviewed-reference support unit in test source list",
        )

    def test_phase4_lightweight_reference_header_exists(self) -> None:
        self.assertTrue(
            (REPO_ROOT / "test" / "stealth" / "ReviewedClientHelloReferences.h").exists(),
            msg="phase 4 must provide lightweight reviewed-fixture reference header",
        )

    def test_phase4_reference_unit_is_backed_by_generated_reviewed_fixtures(self) -> None:
        references_cpp = REFERENCES_CPP_PATH.read_text(encoding="utf-8")

        self.assertIn(
            '#include "test/stealth/ReviewedClientHelloFixtures.h"',
            references_cpp,
            msg="phase 4 lightweight references must source values from the generated reviewed fixtures",
        )
        self.assertNotIn(
            "= {",
            references_cpp,
            msg="phase 4 lightweight references must not hardcode vector initializer tables",
        )

    def test_phase4_replaced_direct_heavy_includes_in_targeted_tests(self) -> None:
        files = [
            REPO_ROOT / "test" / "stealth" / "test_tls_capture_chrome144_differential.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_capture_firefox148_differential.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_capture_safari26_3_differential.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_firefox_linux_desktop_ech.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_firefox_fixture_contract.cpp",
            REPO_ROOT / "test" / "stealth" / "test_stealth_alpn_connection_coherence.cpp",
        ]

        for file_path in files:
            content = file_path.read_text(encoding="utf-8")
            self.assertNotIn(
                '#include "test/stealth/ReviewedClientHelloFixtures.h"',
                content,
                msg=f"phase 4 should remove direct heavy reviewed-fixture include from {file_path.name}",
            )
            self.assertIn(
                '#include "test/stealth/ReviewedClientHelloReferences.h"',
                content,
                msg=f"phase 4 should use lightweight reviewed references include in {file_path.name}",
            )

    def test_phase4_reviewed_refs_namespace_remains_test_only(self) -> None:
        for file_path in STEALTH_ROOT.rglob("*"):
            if not file_path.is_file() or file_path.suffix not in {".h", ".hpp", ".cpp", ".cc"}:
                continue

            content = file_path.read_text(encoding="utf-8")
            self.assertNotIn(
                "reviewed_refs",
                content,
                msg=f"reviewed_refs must stay test-only and not appear in production stealth code: {file_path}",
            )
            self.assertNotIn(
                "ReviewedClientHelloReferences",
                content,
                msg=f"lightweight reviewed reference shim must not be included from production stealth code: {file_path}",
            )


if __name__ == "__main__":
    unittest.main()
