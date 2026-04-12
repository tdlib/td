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
ROOT_CTEST_PATH = REPO_ROOT / "build" / "CTestTestfile.cmake"
TEST_CTEST_PATH = REPO_ROOT / "build" / "test" / "CTestTestfile.cmake"


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

        self.assertEqual(
            "cmake",
            tasks.get("cmake test sqlite vendor smoke", {}).get("type"),
            msg="workspace tasks must expose a preset-backed CMake smoke test task",
        )
        self.assertEqual("test", tasks["cmake test sqlite vendor smoke"].get("command"))
        self.assertEqual("sqlite-vendor-smoke", tasks["cmake test sqlite vendor smoke"].get("preset"))

    def test_root_ctest_tree_includes_test_subdirectory(self) -> None:
        root_ctest = ROOT_CTEST_PATH.read_text(encoding="utf-8")

        self.assertIn('subdirs("test")', root_ctest)

    def test_ctest_registers_exact_sqlite_vendor_wrapper_smoke(self) -> None:
        test_ctest = TEST_CTEST_PATH.read_text(encoding="utf-8")

        self.assertIn(
            'add_test(Test_DB_sqlite_vendor_wrapper_surface_contract',
            test_ctest,
            msg="the generated CTest tree must expose the exact SQLite vendor smoke test for targeted CMake Tools execution",
        )
        self.assertIn(
            '"--exact" "Test_DB_sqlite_vendor_wrapper_surface_contract"',
            test_ctest,
            msg="the SQLite vendor smoke must stay targetable as an exact run_all_tests case",
        )


if __name__ == "__main__":
    unittest.main()