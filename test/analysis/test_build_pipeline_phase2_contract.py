# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
ROOT_CMAKE_PATH = REPO_ROOT / "CMakeLists.txt"
TEST_CMAKE_PATH = REPO_ROOT / "test" / "CMakeLists.txt"
TDCORE_PCH_PATH = REPO_ROOT / "td" / "tdcore_pch.h"
TEST_PCH_PATH = REPO_ROOT / "test" / "test_pch.h"


class BuildPipelinePhase2ContractTest(unittest.TestCase):
    def test_root_cmake_exposes_target_scoped_pch_option(self) -> None:
        root_cmake = ROOT_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "option(TD_ENABLE_TARGET_PCH",
            root_cmake,
            msg="phase 2 must be guarded by an explicit target-scoped PCH option",
        )
        self.assertIn(
            "if (TD_ENABLE_TARGET_PCH AND COMMAND target_precompile_headers)",
            root_cmake,
            msg="PCH wiring must be feature-detected because the repository still declares CMake 3.10 compatibility",
        )

    def test_root_cmake_applies_tdcore_pch_to_all_tdcore_build_shapes(self) -> None:
        root_cmake = ROOT_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "target_precompile_headers(tdcore_part1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/td/tdcore_pch.h)",
            root_cmake,
            msg="MSVC+LTO split tdcore build must apply the tdcore PCH to tdcore_part1",
        )
        self.assertIn(
            "target_precompile_headers(tdcore_part2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/td/tdcore_pch.h)",
            root_cmake,
            msg="MSVC+LTO split tdcore build must apply the tdcore PCH to tdcore_part2",
        )
        self.assertIn(
            "target_precompile_headers(tdcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/td/tdcore_pch.h)",
            root_cmake,
            msg="single-target tdcore build must apply the tdcore PCH to tdcore",
        )

    def test_test_cmake_applies_dedicated_run_all_tests_pch(self) -> None:
        test_cmake = TEST_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "if (TD_ENABLE_TARGET_PCH AND COMMAND target_precompile_headers)",
            test_cmake,
            msg="run_all_tests PCH must be guarded behind feature detection for older CMake clients",
        )
        self.assertIn(
            "target_precompile_headers(run_all_tests PRIVATE ${CMAKE_SOURCE_DIR}/test/test_pch.h)",
            test_cmake,
            msg="run_all_tests must use a dedicated test PCH instead of reusing tdcore internals",
        )

    def test_tdcore_pch_header_contains_shared_tdcore_front_door_headers(self) -> None:
        self.assertTrue(TDCORE_PCH_PATH.exists(), msg="phase 2 must add a dedicated tdcore PCH header")
        tdcore_pch = TDCORE_PCH_PATH.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/common.h"', tdcore_pch)
        self.assertIn('#include "td/actor/actor.h"', tdcore_pch)
        self.assertIn('#include "td/telegram/td_api.h"', tdcore_pch)
        self.assertIn('#include "td/telegram/telegram_api.h"', tdcore_pch)

    def test_test_pch_header_contains_shared_test_front_door_headers(self) -> None:
        self.assertTrue(TEST_PCH_PATH.exists(), msg="phase 2 must add a dedicated run_all_tests PCH header")
        test_pch = TEST_PCH_PATH.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/common.h"', test_pch)
        self.assertIn('#include "td/utils/tests.h"', test_pch)


if __name__ == "__main__":
    unittest.main()