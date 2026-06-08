# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest
from unittest import mock

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


def write_junit(path: pathlib.Path, *, tests: int, failures: int = 0, errors: int = 0) -> None:
    path.write_text(
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<testsuites><testsuite name="ctest" tests="{tests}" failures="{failures}" '
        f'errors="{errors}" skipped="0" time="1.0"></testsuite></testsuites>\n',
        encoding="utf-8",
    )


class RunSanitizerMatrixCtestEvidenceContractTest(unittest.TestCase):
    def test_run_command_does_not_hide_mirror_log_write_failures(self) -> None:
        class FailingMirrorFile:
            def __init__(self) -> None:
                self.header_finished = False

            def __enter__(self) -> "FailingMirrorFile":
                return self

            def __exit__(self, *args: object) -> None:
                return None

            def write(self, text: str) -> int:
                if self.header_finished and "mirror-payload" in text:
                    raise OSError("forced mirror write failure")
                if text == "\n":
                    self.header_finished = True
                return len(text)

            def flush(self) -> None:
                return None

        with tempfile.TemporaryDirectory() as tmp:
            temp_dir = pathlib.Path(tmp)
            command_log = temp_dir / "ctest-command.log"
            output_log = temp_dir / "ctest-output.log"
            original_open = pathlib.Path.open

            def open_with_failing_mirror(
                path: pathlib.Path, *args: object, **kwargs: object
            ) -> object:
                if path == output_log:
                    return FailingMirrorFile()
                return original_open(path, *args, **kwargs)

            with mock.patch.object(pathlib.Path, "open", open_with_failing_mirror):
                with self.assertRaisesRegex(RuntimeError, "forced mirror write failure"):
                    run_sanitizer_matrix.run_command(
                        command=[
                            sys.executable,
                            "-c",
                            "print('mirror-payload')",
                        ],
                        cwd=REPO_ROOT,
                        env_overrides={},
                        log_path=command_log,
                        mirror_log_paths=[output_log],
                        lane_name="contract",
                        phase_name="test",
                        heartbeat_seconds=60.0,
                        status_detail="basic",
                    )

    def test_run_command_mirrors_stdout_into_ctest_output_log_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_dir = pathlib.Path(tmp)
            command_log = temp_dir / "ctest-command.log"
            output_log = temp_dir / "ctest-output.log"

            result = run_sanitizer_matrix.run_command(
                command=[
                    sys.executable,
                    "-c",
                    (
                        "print('1/1 Test #1: smoke ............................   Passed    0.01 sec')\n"
                        "print()\n"
                        "print('100% tests passed, 0 tests failed out of 1')\n"
                    ),
                ],
                cwd=REPO_ROOT,
                env_overrides={},
                log_path=command_log,
                mirror_log_paths=[output_log],
                lane_name="contract",
                phase_name="test",
                heartbeat_seconds=60.0,
                status_detail="basic",
            )

            self.assertTrue(result["ok"])
            self.assertTrue(command_log.exists())
            self.assertTrue(output_log.exists())
            self.assertIn(
                "100% tests passed, 0 tests failed out of 1",
                command_log.read_text(encoding="utf-8"),
            )
            self.assertEqual(
                command_log.read_text(encoding="utf-8"),
                output_log.read_text(encoding="utf-8"),
            )

    def test_clean_ctest_evidence_accepts_junit_discovery_and_command_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_dir = pathlib.Path(tmp)
            command_log = temp_dir / "ctest-command.log"
            output_log = temp_dir / "ctest-output.log"
            junit = temp_dir / "ctest-junit.xml"
            command_log.write_text(
                "100% tests passed, 0 tests failed out of 3\n",
                encoding="utf-8",
            )
            output_log.write_text(
                "100% tests passed, 0 tests failed out of 3\n",
                encoding="utf-8",
            )
            write_junit(junit, tests=3)

            discovery_result: run_sanitizer_matrix.DiscoveryResult = {"ok": True, "test_count": 3}
            audit = run_sanitizer_matrix.audit_ctest_evidence(
                test_phase_result={"ok": True},
                discovery_result=discovery_result,
                ctest_command_log=command_log,
                ctest_output_log=output_log,
                ctest_junit=junit,
                junit_summary=run_sanitizer_matrix.parse_junit(junit),
                failed_tests=[],
            )

            self.assertTrue(audit["ok"])
            self.assertEqual([], audit["issues"])
            self.assertEqual([], audit["warnings"])

    def test_failed_ctest_tests_are_detected_from_command_log_when_output_log_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_dir = pathlib.Path(tmp)
            command_log = temp_dir / "ctest-command.log"
            output_log = temp_dir / "ctest-output.log"
            junit = temp_dir / "ctest-junit.xml"
            command_log.write_text(
                "\n".join(
                    [
                        "1/2 Test #10: Test_MSan_Foo .................***Failed    0.25 sec",
                        "The following tests FAILED:",
                        "        10 - Test_MSan_Foo (Failed)",
                    ]
                ),
                encoding="utf-8",
            )
            write_junit(junit, tests=2, failures=1)

            failed_tests = run_sanitizer_matrix.merge_failed_ctest_tests(
                [
                    run_sanitizer_matrix.parse_failed_ctest_tests(output_log),
                    run_sanitizer_matrix.parse_failed_ctest_tests(command_log),
                ]
            )
            discovery_result: run_sanitizer_matrix.DiscoveryResult = {"ok": True, "test_count": 2}
            audit = run_sanitizer_matrix.audit_ctest_evidence(
                test_phase_result={"ok": False},
                discovery_result=discovery_result,
                ctest_command_log=command_log,
                ctest_output_log=output_log,
                ctest_junit=junit,
                junit_summary=run_sanitizer_matrix.parse_junit(junit),
                failed_tests=failed_tests,
            )

            self.assertEqual(
                [{"id": 10, "name": "Test_MSan_Foo", "status": "Failed"}],
                failed_tests,
            )
            self.assertFalse(audit["ok"])
            self.assertTrue(
                any("ctest output reports failed tests" in issue for issue in audit["issues"])
            )
            self.assertTrue(
                any(
                    "ctest mirrored output log artifact is missing" in warning
                    for warning in audit["warnings"]
                )
            )

    def test_ctest_evidence_rejects_discovery_junit_count_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_dir = pathlib.Path(tmp)
            command_log = temp_dir / "ctest-command.log"
            output_log = temp_dir / "ctest-output.log"
            junit = temp_dir / "ctest-junit.xml"
            command_log.write_text(
                "100% tests passed, 0 tests failed out of 3\n",
                encoding="utf-8",
            )
            output_log.write_text(
                "100% tests passed, 0 tests failed out of 3\n",
                encoding="utf-8",
            )
            write_junit(junit, tests=3)

            discovery_result: run_sanitizer_matrix.DiscoveryResult = {"ok": True, "test_count": 2}
            audit = run_sanitizer_matrix.audit_ctest_evidence(
                test_phase_result={"ok": True},
                discovery_result=discovery_result,
                ctest_command_log=command_log,
                ctest_output_log=output_log,
                ctest_junit=junit,
                junit_summary=run_sanitizer_matrix.parse_junit(junit),
                failed_tests=[],
            )

            self.assertFalse(audit["ok"])
            self.assertTrue(
                any("discovery/JUnit test count mismatch" in issue for issue in audit["issues"])
            )

    def test_overall_counts_surface_ctest_evidence_quality(self) -> None:
        counts = run_sanitizer_matrix.compute_overall_counts(
            [
                {
                    "ok": True,
                    "combined_findings": run_sanitizer_matrix.empty_findings_summary(),
                    "ctest_evidence": {
                        "issues": [],
                        "warnings": ["ctest mirrored output log artifact is missing or empty"],
                    },
                },
                {
                    "ok": False,
                    "combined_findings": run_sanitizer_matrix.empty_findings_summary(),
                    "ctest_evidence": {
                        "issues": ["ctest JUnit artifact is missing or empty"],
                        "warnings": [],
                    },
                },
            ]
        )

        self.assertEqual(1, counts["ctest_evidence_issues_total"])
        self.assertEqual(1, counts["ctest_evidence_warnings_total"])

    def test_failure_reasons_include_phase_findings_and_particular_failed_tests(self) -> None:
        findings = run_sanitizer_matrix.empty_findings_summary()
        findings["total_matches"] = 2
        findings["counts_by_category"] = {"thread_sanitizer_race": 2}
        findings["counts_by_severity"] = {"critical": 2}

        failed_tests: list[run_sanitizer_matrix.FailedTestRecord] = [
            {"id": 10, "name": "Test_Log_Bench", "status": "Failed"},
            {"id": 12, "name": "Test_MSan_Foo", "status": "Timeout"},
        ]
        ctest_evidence: run_sanitizer_matrix.CtestEvidenceAudit = {
            "ok": False,
            "issues": ["ctest JUnit artifact is missing or empty"],
            "warnings": [],
            "command_log_present": True,
            "output_log_present": False,
            "junit_present": False,
            "ctest_pass_summaries": [],
        }
        reasons = run_sanitizer_matrix.build_lane_failure_reasons(
            phase_results={
                "configure": {"ok": True},
                "build": {"ok": False, "exit_code": 2, "log_path": "/tmp/build.log"},
                "test_discovery_configure": {"skipped": True},
                "discovery": {"skipped": True},
                "test": {"skipped": True},
            },
            ctest_evidence=ctest_evidence,
            failed_tests=failed_tests,
            combined_findings=findings,
        )

        categories = [reason["category"] for reason in reasons]
        self.assertIn("phase_failed", categories)
        self.assertIn("ctest_evidence", categories)
        self.assertIn("failed_tests", categories)
        self.assertIn("sanitizer_findings", categories)
        self.assertTrue(any("build phase failed" in reason["message"] for reason in reasons))
        self.assertTrue(any("Test_Log_Bench" in reason["message"] for reason in reasons))
        self.assertTrue(any(reason.get("tests") == failed_tests for reason in reasons))

    def test_monitor_summary_shows_failed_lane_reasons(self) -> None:
        lines: list[str] = []

        run_sanitizer_matrix.append_monitor_summary_lines(
            lines,
            {
                "counts": {"lanes_total": 1, "lanes_ok": 0, "lanes_failed": 1, "findings_total": 2},
                "completed_lanes": ["tsan"],
                "failed_lanes": [
                    {
                        "lane": "tsan",
                        "failure_reasons": [
                            {
                                "category": "failed_tests",
                                "message": "CTest failed tests: Test_Log_Bench",
                            }
                        ],
                    }
                ],
            },
        )

        rendered = "\n".join(lines)
        self.assertIn("Failed lane reasons:", rendered)
        self.assertIn("tsan", rendered)
        self.assertIn("Test_Log_Bench", rendered)


if __name__ == "__main__":
    unittest.main()
