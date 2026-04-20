# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import json
import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TASKS_PATH = REPO_ROOT / ".vscode" / "tasks.json"

HEAVY_TEST_REGEX = (
    "^(Test_Log_Bench|"
    "Test_ConcurrentHashMap_Benchmark|"
    "Test_DB_sqlite_phase3_statement_blob_roundtrip_light_fuzz|"
    "Test_CryptoPQ_generated_slow|"
    "Test_Http_gzip_bomb|"
    "Test_SslCtxLightFuzzExtended_.*)$"
)


class BuildPipelinePhase1ContractTest(unittest.TestCase):
    def _tasks(self) -> dict[str, dict]:
        tasks_document = json.loads(TASKS_PATH.read_text(encoding="utf-8"))
        return {task["label"]: task for task in tasks_document.get("tasks", [])}

    def test_workspace_declares_fast_ctest_lane_excluding_known_heavy_hotspots(self) -> None:
        tasks = self._tasks()

        self.assertIn("ctest fast lane (14 cores)", tasks)
        self.assertEqual("ctest", tasks["ctest fast lane (14 cores)"].get("command"))
        self.assertEqual(
            [
                "--test-dir",
                "build",
                "--output-on-failure",
                "-j",
                "14",
                "-E",
                HEAVY_TEST_REGEX,
            ],
            tasks["ctest fast lane (14 cores)"].get("args"),
            msg="fast lane must skip only the measured heavyweight tests",
        )
        self.assertEqual(
            ["cmake build tests fast"],
            tasks["ctest fast lane (14 cores)"].get("dependsOn"),
            msg="fast lane must still build run_all_tests before execution",
        )

    def test_workspace_declares_heavy_hotspot_lane_for_explicit_follow_up(self) -> None:
        tasks = self._tasks()

        self.assertIn("ctest heavy hotspots (14 cores)", tasks)
        self.assertEqual("ctest", tasks["ctest heavy hotspots (14 cores)"].get("command"))
        self.assertEqual(
            [
                "--test-dir",
                "build",
                "--output-on-failure",
                "-j",
                "14",
                "-R",
                HEAVY_TEST_REGEX,
            ],
            tasks["ctest heavy hotspots (14 cores)"].get("args"),
            msg="heavy hotspot lane must run exactly the tests excluded from the fast lane",
        )
        self.assertEqual(
            ["cmake build tests fast"],
            tasks["ctest heavy hotspots (14 cores)"].get("dependsOn"),
            msg="heavy hotspot lane must reuse the same build artifact as the fast lane",
        )


if __name__ == "__main__":
    unittest.main()