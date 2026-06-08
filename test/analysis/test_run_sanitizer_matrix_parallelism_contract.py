# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import unittest
from unittest import mock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
RUNNER = TOOLS_CI / "run_sanitizer_matrix.py"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class RunSanitizerMatrixParallelismContractTest(unittest.TestCase):
    def test_default_jobs_uses_detected_cpu_capacity(self) -> None:
        with (
            mock.patch.object(run_sanitizer_matrix, "detect_default_jobs", return_value=6),
            mock.patch.object(sys, "argv", ["run_sanitizer_matrix.py"]),
        ):
            self.assertEqual(6, run_sanitizer_matrix.parse_args().jobs)

    def test_explicit_jobs_overrides_detected_cpu_capacity(self) -> None:
        with (
            mock.patch.object(run_sanitizer_matrix, "detect_default_jobs", return_value=6),
            mock.patch.object(sys, "argv", ["run_sanitizer_matrix.py", "--jobs", "3"]),
        ):
            self.assertEqual(3, run_sanitizer_matrix.parse_args().jobs)

    def test_build_refreshes_cmake_discovery_before_ctest(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")

        build_index = source.index('phase_results["build"] = run_command(')
        refresh_index = source.index('phase_name="test-discovery-configure"')
        discovery_index = source.index('phase_results["discovery"] = try_collect_test_discovery(')
        test_index = source.index('phase_results["test"] = run_command(')

        self.assertLess(build_index, refresh_index)
        self.assertLess(refresh_index, discovery_index)
        self.assertLess(discovery_index, test_index)
        self.assertIn('phase_results["test_discovery_configure"].get("ok")', source)
        self.assertIn('"test_discovery_configure_log": str(test_discovery_configure_log)', source)

    def test_runner_exports_requested_jobs_to_cmake_environment(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")

        self.assertIn('lane_env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(args.jobs)', source)
        self.assertIn('"build_jobs": args.jobs', source)
        self.assertIn('"test_jobs": args.jobs', source)
        self.assertIn("mirror_log_paths=[ctest_output_log]", source)
        self.assertNotIn('            "--output-log",', source)

    def test_runtime_findings_are_counted_in_lane_and_matrix_totals(self) -> None:
        normal = run_sanitizer_matrix.empty_findings_summary()
        runtime = run_sanitizer_matrix.empty_findings_summary()
        runtime["counts_by_category"] = {"msan_uninitialized": 2}
        runtime["counts_by_severity"] = {"high": 2}
        runtime["total_matches"] = 2

        combined = run_sanitizer_matrix.merge_findings_summaries([normal, runtime], 10)

        self.assertEqual(2, combined["total_matches"])
        self.assertEqual({"msan_uninitialized": 2}, combined["counts_by_category"])
        self.assertEqual({"high": 2}, combined["counts_by_severity"])

        counts = run_sanitizer_matrix.compute_overall_counts(
            [{"ok": False, "findings": normal, "runtime_findings": runtime, "combined_findings": combined}]
        )

        self.assertEqual(1, counts["lanes_failed"])
        self.assertEqual(2, counts["findings_total"])
        self.assertEqual({"high": 2}, counts["findings_by_severity"])

        resumed_counts = run_sanitizer_matrix.compute_overall_counts(
            [{"ok": False, "findings": normal, "runtime_findings": runtime}]
        )
        self.assertEqual(2, resumed_counts["findings_total"])
        self.assertEqual({"high": 2}, resumed_counts["findings_by_severity"])

    def test_failed_ctest_names_are_extracted_from_output_log(self) -> None:
        output_log = pathlib.Path(self.id().replace(".", "_"))
        output_log.write_text(
            "\n".join(
                [
                    "1/4 Test #10: Test_Log_Bench .................***Failed    0.25 sec",
                    "2/4 Test #11: Test_Other .....................   Passed    0.01 sec",
                    "The following tests FAILED:",
                    "        10 - Test_Log_Bench (Failed)",
                    "        12 - Test_MSan_Foo (Timeout)",
                ]
            ),
            encoding="utf-8",
        )
        self.addCleanup(output_log.unlink)

        failures = run_sanitizer_matrix.parse_failed_ctest_tests(output_log)

        self.assertEqual(
            [
                {"id": 10, "name": "Test_Log_Bench", "status": "Failed"},
                {"id": 12, "name": "Test_MSan_Foo", "status": "Timeout"},
            ],
            failures,
        )


if __name__ == "__main__":
    unittest.main()
