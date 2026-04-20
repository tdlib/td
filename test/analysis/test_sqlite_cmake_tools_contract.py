# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
SETTINGS_PATH = REPO_ROOT / ".vscode" / "settings.json"
PRESETS_PATH = REPO_ROOT / "CMakePresets.json"
TASKS_PATH = REPO_ROOT / ".vscode" / "tasks.json"
ROOT_CMAKE_PATH = REPO_ROOT / "CMakeLists.txt"
TEST_CMAKE_PATH = REPO_ROOT / "test" / "CMakeLists.txt"
SQLITE_VENDOR_CONTRACT_TEST_PATH = REPO_ROOT / "test" / "sqlite_vendor_contract.cpp"


class SqliteCMakeToolsContractTest(unittest.TestCase):
    def test_workspace_enables_cmake_auto_configure_on_open(self) -> None:
        settings = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))

        self.assertTrue(
            settings.get("cmake.configureOnOpen"),
            msg="workspace CMake settings must auto-configure on open so CMake Tools can discover build targets and tests",
        )

    def test_workspace_enables_ctest_explorer_integration(self) -> None:
        settings = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))

        self.assertTrue(
            settings.get("cmake.ctest.testExplorerIntegrationEnabled"),
            msg="workspace CMake settings must enable CTest explorer integration so exact SQLite smoke tests surface in VS Code test discovery",
        )

    def test_workspace_forces_preset_mode_for_cmake_tools(self) -> None:
        settings = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))

        self.assertEqual(
            "always",
            settings.get("cmake.useCMakePresets"),
            msg="workspace CMake settings must keep CMake Tools on the checked-in preset flow",
        )

    def test_repo_declares_default_configure_and_sqlite_smoke_test_presets(self) -> None:
        presets = json.loads(PRESETS_PATH.read_text(encoding="utf-8"))

        configure_presets = {preset["name"]: preset for preset in presets.get("configurePresets", [])}
        test_presets = {preset["name"]: preset for preset in presets.get("testPresets", [])}

        self.assertIn(
            "default",
            configure_presets,
            msg="repo CMake presets must expose a default configure preset for CMake Tools",
        )
        self.assertEqual("${sourceDir}/build", configure_presets["default"].get("binaryDir"))
        self.assertIn(
            "sqlite-vendor-smoke",
            test_presets,
            msg="repo CMake presets must expose a targeted SQLite smoke test preset",
        )
        self.assertEqual("default", test_presets["sqlite-vendor-smoke"].get("configurePreset"))
        self.assertEqual(
            "^Test_DB_sqlite_vendor_wrapper_surface_contract$",
            test_presets["sqlite-vendor-smoke"].get("filter", {}).get("include", {}).get("name"),
        )

    def test_workspace_declares_preset_backed_cmake_tasks(self) -> None:
        tasks_document = json.loads(TASKS_PATH.read_text(encoding="utf-8"))
        tasks = {task["label"]: task for task in tasks_document.get("tasks", [])}

        self.assertEqual(
            "cmake",
            tasks.get("cmake configure preset", {}).get("type"),
            msg="workspace tasks must expose a preset-backed CMake configure task",
        )
        self.assertEqual("configure", tasks["cmake configure preset"].get("command"))
        self.assertEqual("default", tasks["cmake configure preset"].get("preset"))

    def test_workspace_declares_exact_ctest_smoke_task(self) -> None:
        tasks_document = json.loads(TASKS_PATH.read_text(encoding="utf-8"))
        tasks = {task["label"]: task for task in tasks_document.get("tasks", [])}

        self.assertEqual(
            "shell",
            tasks.get("cmake test sqlite vendor smoke", {}).get("type"),
            msg="workspace smoke task must use an explicit shell ctest invocation so it cannot widen to the full suite",
        )
        self.assertEqual("ctest", tasks["cmake test sqlite vendor smoke"].get("command"))
        self.assertEqual(
            [
                "--test-dir",
                "build/test",
                "--output-on-failure",
                "-R",
                "^Test_DB_sqlite_vendor_wrapper_surface_contract$",
            ],
            tasks["cmake test sqlite vendor smoke"].get("args"),
            msg="workspace smoke task must target only the exact SQLite wrapper contract case",
        )
        self.assertEqual(
            ["cmake refresh exact sqlite smoke discovery"],
            tasks["cmake test sqlite vendor smoke"].get("dependsOn"),
            msg="workspace smoke task must depend on the exact-discovery refresh step",
        )

    def test_workspace_declares_post_build_discovery_refresh_task(self) -> None:
        tasks_document = json.loads(TASKS_PATH.read_text(encoding="utf-8"))
        tasks = {task["label"]: task for task in tasks_document.get("tasks", [])}

        self.assertEqual(
            "shell",
            tasks.get("cmake refresh exact sqlite smoke discovery", {}).get("type"),
            msg="workspace must expose a dedicated post-build configure refresh task for exact CTest discovery",
        )
        self.assertEqual("cmake", tasks["cmake refresh exact sqlite smoke discovery"].get("command"))
        self.assertEqual(
            [
                "-S",
                ".",
                "-B",
                "build",
                "-G",
                "Ninja",
                "-DBUILD_SHARED_LIBS=ON",
                "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                "-DTD_ENABLE_BENCHMARKS=OFF",
                "-DTD_ENABLE_LLD=ON",
                "-DTD_ENABLE_NATIVE_ARCH=ON",
            ],
            tasks["cmake refresh exact sqlite smoke discovery"].get("args"),
            msg="workspace must refresh exact CTest discovery with the checked-in configure command",
        )
        self.assertEqual(
            ["cmake build tests preset"],
            tasks["cmake refresh exact sqlite smoke discovery"].get("dependsOn"),
            msg="exact-discovery refresh must run after building run_all_tests",
        )

    def test_workspace_marks_broad_ctest_task_as_slow(self) -> None:
        tasks_document = json.loads(TASKS_PATH.read_text(encoding="utf-8"))
        task_labels = {task["label"] for task in tasks_document.get("tasks", [])}

        self.assertNotIn(
            "ctest",
            task_labels,
            msg="workspace must not expose the full-suite ctest task under an ambiguous label",
        )
        self.assertIn(
            "ctest all (slow)",
            task_labels,
            msg="workspace must label the full-suite ctest task as slow so the exact SQLite smoke path is harder to confuse with it",
        )

    def test_root_cmake_wires_ctest_and_test_subdirectory(self) -> None:
        root_cmake = ROOT_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "include(CTest)",
            root_cmake,
            msg="root CMake must enable CTest so configure-generated test discovery is available on clean CI trees",
        )
        self.assertIn(
            "if (BUILD_TESTING)",
            root_cmake,
            msg="root CMake must gate the test subtree behind BUILD_TESTING",
        )
        self.assertIn(
            "add_subdirectory(test)",
            root_cmake,
            msg="root CMake must include the test subtree when BUILD_TESTING is enabled",
        )

    def test_test_cmake_discovers_exact_run_all_tests_cases_and_has_clean_tree_fallback(self) -> None:
        test_cmake = TEST_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            'COMMAND "${TD_RUN_ALL_TESTS_EXECUTABLE}" --list',
            test_cmake,
            msg="test CMake must discover exact run_all_tests cases from the built binary during configure",
        )
        self.assertIn(
            'add_test(NAME "${TD_RUN_ALL_TEST_NAME}"',
            test_cmake,
            msg="test CMake must register exact discovered run_all_tests cases instead of only an umbrella entry",
        )
        self.assertIn(
            'COMMAND run_all_tests --exact "${TD_RUN_ALL_TEST_NAME}"',
            test_cmake,
            msg="discovered run_all_tests cases must remain targetable through the exact-filter entrypoint",
        )
        self.assertIn(
            'message(STATUS "run_all_tests executable is not available during configure; registering umbrella CTest entry")',
            test_cmake,
            msg="clean-tree configure must degrade to an umbrella CTest entry instead of assuming generated CTest files already exist",
        )

    def test_sqlite_vendor_wrapper_smoke_case_exists_in_run_all_tests_source(self) -> None:
        sqlite_vendor_contract_test = SQLITE_VENDOR_CONTRACT_TEST_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "TEST(DB, sqlite_vendor_wrapper_surface_contract)",
            sqlite_vendor_contract_test,
            msg="the SQLite vendor wrapper smoke must remain compiled into run_all_tests for exact CTest discovery",
        )


if __name__ == "__main__":
    unittest.main()