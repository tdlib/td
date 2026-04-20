# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "cpp23-compat.yml"
COMPILER_SETUP_PATH = REPO_ROOT / "CMake" / "TdSetUpCompiler.cmake"


class Cpp23CiContractTest(unittest.TestCase):
    def test_workflow_builds_gcc_and_clang_core_targets(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn("compiler: gcc15", workflow_text)
        self.assertIn("cc: gcc-15", workflow_text)
        self.assertIn("cxx: g++-15", workflow_text)
        self.assertIn("compiler: clang22", workflow_text)
        self.assertIn("cc: clang-22", workflow_text)
        self.assertIn("cxx: clang++-22", workflow_text)
        self.assertIn("cmake --build build --target tdjson tg_cli run_all_tests", workflow_text)

    def test_workflow_runs_cpp23_guard_and_sanitizer_lane(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn("python3 tools/ci/check_cpp23_compat.py", workflow_text)
        self.assertIn("-fsanitize=address,undefined", workflow_text)

    def test_compiler_setup_exposes_strict_ci_warning_option(self) -> None:
        compiler_setup_text = COMPILER_SETUP_PATH.read_text(encoding="utf-8")

        self.assertIn("option(TD_STRICT_CI_WARNINGS", compiler_setup_text)
        self.assertIn("option(TD_STRICT_COMPILER_VERSIONS", compiler_setup_text)
        self.assertIn("GCC 15.2+ is recommended", compiler_setup_text)
        self.assertIn("Clang 22.1.3+ is recommended", compiler_setup_text)
        self.assertIn("-Werror=return-type", compiler_setup_text)
        self.assertIn("-Werror=deprecated", compiler_setup_text)

    def test_workflow_enables_strict_compiler_versions_in_all_cpp23_jobs(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertGreaterEqual(workflow_text.count("-DTD_STRICT_COMPILER_VERSIONS=ON"), 3)


if __name__ == "__main__":
    unittest.main()