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

        self.assertIn("compiler: gcc", workflow_text)
        self.assertIn("compiler: clang", workflow_text)
        self.assertIn("cmake --build build --target tdjson tg_cli run_all_tests", workflow_text)

    def test_workflow_runs_cpp23_guard_and_sanitizer_lane(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn("python3 tools/ci/check_cpp23_compat.py", workflow_text)
        self.assertIn("-fsanitize=address,undefined", workflow_text)

    def test_compiler_setup_exposes_strict_ci_warning_option(self) -> None:
        compiler_setup_text = COMPILER_SETUP_PATH.read_text(encoding="utf-8")

        self.assertIn("option(TD_STRICT_CI_WARNINGS", compiler_setup_text)
        self.assertIn("-Werror=return-type", compiler_setup_text)
        self.assertIn("-Werror=deprecated", compiler_setup_text)


if __name__ == "__main__":
    unittest.main()