# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Usage guide for humans and AI agents.

Purpose:
- Run the sanitizer CMake preset matrix for configure, build, discovery, and ctest.
- Store logs, JUnit output, JSON summaries, and status snapshots under --output-root.
- Surface sanitizer findings in terminal status updates and optional report files.

Supported sanitizers:
- ASan (AddressSanitizer): catches use-after-free, double-free, and out-of-bounds reads/writes. On supported platforms it also integrates LeakSanitizer.
- LSan (LeakSanitizer): catches memory that was allocated but not freed before process exit. This runner supports a standalone `lsan` lane in addition to ASan-integrated leak detection.
- MSan (MemorySanitizer): catches reads of uninitialized memory.
- TSan (ThreadSanitizer): catches unsynchronized data races and lock-order/deadlock style concurrency defects reported by the runtime.
- UBSan (UndefinedBehaviorSanitizer): catches undefined behavior such as signed overflow, division by zero, invalid shifts, misaligned accesses, and similar illegal operations.

Recommended commands:
- Run a single lane interactively:
    python3 tools/ci/run_sanitizer_matrix.py --lanes asan --jobs 14 --status-detail detailed
- Run the core sanitizer chain with build-dir cleanup between lanes:
    python3 tools/ci/run_sanitizer_matrix.py --chain-core-sanitizers --jobs 14
- Run standalone LeakSanitizer:
    python3 tools/ci/run_sanitizer_matrix.py --lanes lsan --jobs 14 --status-detail detailed
- Run MemorySanitizer lane:
    python3 tools/ci/run_sanitizer_matrix.py --lanes msan --jobs 14 --status-detail detailed
- Run the full matrix and stop at the first failing lane:
    python3 tools/ci/run_sanitizer_matrix.py --jobs 14 --stop-on-failure
- Resume an interrupted run in place:
    python3 tools/ci/run_sanitizer_matrix.py --resume-run-dir <run-dir>
- Monitor an existing run continuously:
    python3 tools/ci/run_sanitizer_matrix.py --monitor --run-dir <run-dir> --follow
- Export findings to a report file:
    python3 tools/ci/run_sanitizer_matrix.py --lanes asan --findings-report artifacts/sast/sanitizer_matrix/findings.json

Important options:
- Use --heartbeat-seconds to control how often active-phase status updates are printed.
- Use --status-detail detailed to print log growth, idle time, and failure log tails.
- Use --findings-report-format json|text to choose report output format.
- Use --report-all-findings when the report must contain every matched finding without truncation.
- Use --max-sample-findings to limit in-memory samples when full capture is not needed.
- The runner auto-detects llvm-symbolizer when available and exposes it to sanitizer runs for better stack traces.

Operational notes:
- Relative paths are resolved from --repo-root.
- Every run is stored under --output-root/<run_id>/.
- summary.json contains run-wide results; unified_findings_report.json/txt contain merged normal/runtime findings with explicit source markers.
- Each lane directory contains lane_summary.json, unified_findings_report.json/txt, command logs, and runtime-reports/ raw sanitizer logs.
- Raw sanitizer runtime logs rely on documented common sanitizer flags such as log_path and log_exe_name; these reports survive sanitizer-detected aborts, but external SIGKILL/OOM termination can still truncate or prevent report creation.
- In monitor mode the script reads the latest recorded status.json and renders the active phase.
- For AI-driven runs, prefer --status-detail detailed and --findings-report so progress and findings are visible both in terminal output and in artifacts.
- The default --disk-space-mode auto keeps only one sanitizer build tree for new runs and disables cleanup for resumed runs.
- Disk-space cleanup modes are intentionally incompatible with --resume-run-dir because resume relies on preserved build checkpoints.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import datetime as dt
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import typing as t


class FindingPattern(t.TypedDict):
    category: str
    severity: str
    regex: str
    risk: str
    principles: list[str]


class FindingRecord(t.TypedDict):
    category: str
    severity: str
    risk: str
    principles: list[str]
    file: str
    line: int
    snippet: str


class FindingsSummary(t.TypedDict):
    counts_by_category: dict[str, int]
    counts_by_severity: dict[str, int]
    total_matches: int
    sample_findings: list[FindingRecord]
    sample_limit: int
    sample_truncated: bool


FindingSource = t.Literal["normal", "runtime"]


class UnifiedFindingRecord(FindingRecord):
    lane: str
    source: FindingSource


class LaneFindingsView(t.TypedDict):
    lane: str
    findings: FindingsSummary
    runtime_findings: FindingsSummary


class FailedTestRecord(t.TypedDict):
    id: int
    name: str
    status: str


class JunitSummary(t.TypedDict, total=False):
    tests: int
    failures: int
    errors: int
    skipped: int
    time_seconds: float
    path: str
    parse_error: bool


class CtestPassSummary(t.TypedDict):
    path: str
    percent: int
    failed: int
    total: int


class CtestEvidenceAudit(t.TypedDict):
    ok: bool
    issues: list[str]
    warnings: list[str]
    command_log_present: bool
    output_log_present: bool
    junit_present: bool
    ctest_pass_summaries: list[CtestPassSummary]


class FailureReason(t.TypedDict, total=False):
    category: str
    phase: str
    message: str
    exit_code: int
    log_path: str
    phases: list[str]
    tests: list[FailedTestRecord]
    count: int
    counts_by_category: dict[str, int]
    counts_by_severity: dict[str, int]


@dataclasses.dataclass(frozen=True)
class ReportPaths:
    json_path: str
    text_path: str


class CommandResult(t.TypedDict, total=False):
    command: list[str]
    cwd: str
    log_path: str
    started_at: str
    ended_at: str
    duration_seconds: float
    exit_code: int
    ok: bool
    appended: bool
    skipped: bool
    reason: str


class DiscoveryResult(CommandResult, total=False):
    test_count: int | None
    artifact: str | None
    stderr_log: str | None


class RuntimeReportArtifacts(t.TypedDict):
    runtime_report_dir: str
    runtime_report_base: str
    runtime_report_glob: str


class LaneArtifacts(t.TypedDict):
    configure_log: str
    test_discovery_configure_log: str
    build_log: str
    ctest_command_log: str
    ctest_output_log: str
    ctest_junit: str
    runtime_report_dir: str
    runtime_report_base: str
    runtime_report_glob: str
    runtime_report_files: list[str]
    unified_findings_report_json: str
    unified_findings_report_text: str


class RuntimeTooling(t.TypedDict):
    symbolizer_binary: str | None
    symbolizer_wrapper: str | None


class LaneResult(t.TypedDict):
    lane: str
    configure_preset: str
    build_preset: str
    build_dir: str
    env: dict[str, str]
    runtime_tooling: RuntimeTooling
    parallelism: dict[str, t.Any]
    phase_results: dict[str, CommandResult]
    artifacts: LaneArtifacts
    junit_summary: JunitSummary | None
    ctest_evidence: CtestEvidenceAudit
    failed_tests: list[FailedTestRecord]
    failed_tests_total: int
    findings: FindingsSummary
    runtime_findings: FindingsSummary
    combined_findings: FindingsSummary
    failure_reasons: list[FailureReason]
    ok: bool


DiskSpaceMode = t.Literal["off", "auto", "fresh-builds", "ephemeral-builds"]
EffectiveDiskSpaceMode = t.Literal["off", "fresh-builds", "ephemeral-builds"]


@dataclasses.dataclass(frozen=True)
class CliArgs:
    repo_root: str
    output_root: str
    jobs: int
    lanes: str
    stop_on_failure: bool
    monitor: bool
    run_dir: str | None
    follow: bool
    refresh_seconds: float
    tail_lines: int
    resume_run_dir: str | None
    heartbeat_seconds: float
    status_detail: t.Literal["basic", "detailed"]
    findings_report: str | None
    findings_report_format: t.Literal["json", "text"]
    max_sample_findings: int
    report_all_findings: bool
    disk_space_mode: DiskSpaceMode
    ctest_timeout: int | None


SANITIZER_FINDING_PATTERNS: tuple[FindingPattern, ...] = (
    {
        "category": "asan_memory_error",
        "severity": "critical",
        "regex": r"ERROR: AddressSanitizer:",
        "risk": "Memory safety violation detected by AddressSanitizer",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A03", "OWASP-A05"],
    },
    {
        "category": "lsan_memory_leak",
        "severity": "critical",
        "regex": r"ERROR: LeakSanitizer:",
        "risk": "LeakSanitizer detected leaked memory that must be triaged",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A05"],
    },
    {
        "category": "ubsan_runtime_error",
        "severity": "high",
        "regex": r"runtime error:",
        "risk": "Undefined behavior triggered at runtime",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A03", "OWASP-A08"],
    },
    {
        "category": "ubsan_report",
        "severity": "high",
        "regex": r"UndefinedBehaviorSanitizer",
        "risk": "UndefinedBehaviorSanitizer report indicates undefined behavior",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A08"],
    },
    {
        "category": "tsan_data_race",
        "severity": "critical",
        "regex": r"ThreadSanitizer: data race",
        "risk": "ThreadSanitizer reported a data race",
        "principles": ["ASVS-V11", "OWASP-A08"],
    },
    {
        "category": "tsan_deadlock",
        "severity": "critical",
        "regex": r"ThreadSanitizer: (lock-order-inversion|deadlock)",
        "risk": "ThreadSanitizer reported potential deadlock or lock order inversion",
        "principles": ["ASVS-V11", "OWASP-A08"],
    },
    {
        "category": "msan_uninitialized_use",
        "severity": "critical",
        "regex": r"MemorySanitizer: use-of-uninitialized-value",
        "risk": "MemorySanitizer reported use of uninitialized memory",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A08"],
    },
    {
        "category": "sanitizer_summary",
        "severity": "high",
        "regex": r"SUMMARY: (AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|MemorySanitizer)",
        "risk": "Sanitizer summary indicates one or more defects",
        "principles": ["ASVS-V7", "OWASP-A05"],
    },
)

MAX_SNIPPET_FINDINGS_PER_LOG = 250
DEFAULT_STATUS_HEARTBEAT_SECONDS = 15.0
ISO_UTC_FORMAT = "%Y-%m-%dT%H:%M:%SZ"
STATUS_FILE_NAME = "status.json"
# CTest treats "--timeout 0" as "use built-in default timeout" (often 1500s).
# Use a very large finite timeout to model "no timeout" requests reliably.
CTEST_EFFECTIVE_NO_TIMEOUT_SECONDS = 315_360_000  # 10 years


def detect_default_jobs() -> int:
    if hasattr(os, "sched_getaffinity"):
        try:
            return max(1, len(os.sched_getaffinity(0)))
        except OSError:
            pass
    return max(1, os.cpu_count() or 1)


@dataclasses.dataclass(frozen=True)
class Lane:
    name: str
    configure_preset: str
    build_preset: str
    build_dir: str
    env: dict[str, str]


LANES: tuple[Lane, ...] = (
    Lane(
        name="asan",
        configure_preset="sanitizer-asan",
        build_preset="sanitizer-asan-tests",
        build_dir="build-asan",
        env={
            "ASAN_OPTIONS": "detect_leaks=1:strict_string_checks=1:check_initialization_order=1",
        },
    ),
    Lane(
        name="ubsan",
        configure_preset="sanitizer-ubsan",
        build_preset="sanitizer-ubsan-tests",
        build_dir="build-ubsan",
        env={
            "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
        },
    ),
    Lane(
        name="tsan",
        configure_preset="sanitizer-tsan",
        build_preset="sanitizer-tsan-tests",
        build_dir="build-tsan",
        env={
            "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
        },
    ),
    Lane(
        name="msan",
        configure_preset="sanitizer-msan",
        build_preset="sanitizer-msan-tests",
        build_dir="build-msan",
        env={
            "MSAN_OPTIONS": "halt_on_error=1:symbolize=1:poison_in_dtor=1",
        },
    ),
    Lane(
        name="lsan",
        configure_preset="sanitizer-lsan",
        build_preset="sanitizer-lsan-tests",
        build_dir="build-lsan",
        env={},
    ),
    Lane(
        name="asan-fast",
        configure_preset="sanitizer-asan-fast",
        build_preset="sanitizer-asan-fast-tests",
        build_dir="build-asan-fast",
        env={
            "ASAN_OPTIONS": "detect_leaks=1:strict_string_checks=1:check_initialization_order=1",
        },
    ),
    Lane(
        name="ubsan-fast",
        configure_preset="sanitizer-ubsan-fast",
        build_preset="sanitizer-ubsan-fast-tests",
        build_dir="build-ubsan-fast",
        env={
            "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
        },
    ),
    Lane(
        name="tsan-fast",
        configure_preset="sanitizer-tsan-fast",
        build_preset="sanitizer-tsan-fast-tests",
        build_dir="build-tsan-fast",
        env={
            "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
        },
    ),
)

CORE_SANITIZER_LANE_NAMES: tuple[str, ...] = (
    "asan",
    "lsan",
    "msan",
    "tsan",
    "ubsan",
)


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def isoformat_utc(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime(ISO_UTC_FORMAT)


def as_json(data: t.Any) -> str:
    return json.dumps(data, indent=2, ensure_ascii=True, sort_keys=True)


def load_lane_map() -> dict[str, Lane]:
    return {lane.name: lane for lane in LANES}


def parse_args() -> CliArgs:
    parser = argparse.ArgumentParser(
        description=(
            "Run sanitizer configure/build/full-ctest matrix for all sanitizer presets in CMakePresets.json, "
            "capture logs/JUnit artifacts, and emit JSON status/audit summaries."
        )
    )
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Repository root path (default: current directory).",
    )
    parser.add_argument(
        "--output-root",
        default="artifacts/sast/sanitizer_matrix",
        help="Root folder for generated run artifacts.",
    )
    parser.add_argument(
        "--jobs",
        default=detect_default_jobs(),
        type=int,
        help=(
            "Parallel jobs for sanitizer build and CTest execution. "
            "Defaults to the current CPU affinity/core count."
        ),
    )
    parser.add_argument(
        "--ctest-timeout",
        type=int,
        help=(
            "Optional CTest timeout in seconds passed to '--timeout'. "
            "Use this to raise the default timeout for slow sanitizer runs. "
            "Pass 0 for effectively no timeout."
        ),
    )
    parser.add_argument(
        "--lanes",
        default=",".join(lane.name for lane in LANES),
        help=(
            "Comma-separated lane names. Available: "
            + ", ".join(lane.name for lane in LANES)
            + ". Core sanitizer lanes: asan, lsan, msan, tsan, ubsan."
        ),
    )
    parser.add_argument(
        "--chain-core-sanitizers",
        action="store_true",
        help=(
            "Run asan, lsan, msan, tsan, and ubsan sequentially and force "
            "ephemeral-builds cleanup so each lane deletes its build dir after completion."
        ),
    )
    parser.add_argument(
        "--stop-on-failure",
        action="store_true",
        help="Stop matrix at first failed lane.",
    )
    parser.add_argument(
        "--monitor",
        action="store_true",
        help="Render terminal status for an existing run instead of launching a new matrix run.",
    )
    parser.add_argument(
        "--run-dir",
        help="Specific run directory to monitor. Defaults to latest_run_path.txt under output root.",
    )
    parser.add_argument(
        "--follow",
        action="store_true",
        help="Refresh the terminal monitor until the run completes or the user interrupts it.",
    )
    parser.add_argument(
        "--refresh-seconds",
        default=10.0,
        type=float,
        help="Refresh interval for --follow monitor mode.",
    )
    parser.add_argument(
        "--tail-lines",
        default=20,
        type=int,
        help="How many lines of the active phase log to show in monitor mode.",
    )
    parser.add_argument(
        "--resume-run-dir",
        help="Existing run directory to resume. Reuses completed lane summaries and continues incomplete lanes in place.",
    )
    parser.add_argument(
        "--heartbeat-seconds",
        default=DEFAULT_STATUS_HEARTBEAT_SECONDS,
        type=float,
        help="Heartbeat interval for active phase status updates in seconds.",
    )
    parser.add_argument(
        "--status-detail",
        default="detailed",
        choices=["basic", "detailed"],
        help="Controls heartbeat verbosity in terminal updates.",
    )
    parser.add_argument(
        "--findings-report",
        help="Optional path for aggregated findings report. Relative paths are resolved from --repo-root.",
    )
    parser.add_argument(
        "--findings-report-format",
        default="json",
        choices=["json", "text"],
        help="Output format for --findings-report.",
    )
    parser.add_argument(
        "--max-sample-findings",
        default=MAX_SNIPPET_FINDINGS_PER_LOG,
        type=int,
        help=(
            "Maximum findings entries to keep in memory and write to summaries/reports. "
            "Use -1 to store all matches."
        ),
    )
    parser.add_argument(
        "--report-all-findings",
        action="store_true",
        help="Disable sample truncation and include all matched findings in summaries/reports.",
    )
    parser.add_argument(
        "--disk-space-mode",
        default="auto",
        choices=["off", "auto", "fresh-builds", "ephemeral-builds"],
        help=(
            "Manage disk usage by removing selected lane build directories. "
            "Default: auto. auto uses ephemeral-builds for new runs and off for resumed runs; "
            "fresh-builds deletes stale build dirs before each lane; "
            "ephemeral-builds also deletes the lane build dir after completion."
        ),
    )

    namespace = parser.parse_args()
    if namespace.jobs <= 0:
        parser.error("--jobs must be greater than 0")
    if namespace.ctest_timeout is not None and namespace.ctest_timeout < 0:
        parser.error("--ctest-timeout must be greater than or equal to 0")
    if namespace.refresh_seconds <= 0:
        parser.error("--refresh-seconds must be greater than 0")
    if namespace.tail_lines < 0:
        parser.error("--tail-lines must be >= 0")
    if namespace.heartbeat_seconds <= 0:
        parser.error("--heartbeat-seconds must be greater than 0")
    if namespace.max_sample_findings < -1:
        parser.error("--max-sample-findings must be -1 or greater")

    if namespace.ctest_timeout is None:
        namespace.ctest_timeout = 0

    default_lanes = ",".join(lane.name for lane in LANES)
    if namespace.chain_core_sanitizers:
        if str(namespace.lanes) != default_lanes:
            parser.error("--chain-core-sanitizers can't be combined with --lanes")
        if namespace.resume_run_dir is not None:
            parser.error(
                "--chain-core-sanitizers is not compatible with --resume-run-dir because it forces build directory cleanup"
            )
        if namespace.disk_space_mode not in {"auto", "ephemeral-builds"}:
            parser.error(
                "--chain-core-sanitizers requires --disk-space-mode auto or ephemeral-builds"
            )
        namespace.lanes = ",".join(CORE_SANITIZER_LANE_NAMES)
        namespace.disk_space_mode = "ephemeral-builds"

    return CliArgs(
        repo_root=str(namespace.repo_root),
        output_root=str(namespace.output_root),
        jobs=int(namespace.jobs),
        lanes=str(namespace.lanes),
        stop_on_failure=bool(namespace.stop_on_failure),
        monitor=bool(namespace.monitor),
        run_dir=namespace.run_dir,
        follow=bool(namespace.follow),
        refresh_seconds=float(namespace.refresh_seconds),
        tail_lines=int(namespace.tail_lines),
        resume_run_dir=namespace.resume_run_dir,
        heartbeat_seconds=float(namespace.heartbeat_seconds),
        status_detail=t.cast(t.Literal["basic", "detailed"], namespace.status_detail),
        findings_report=namespace.findings_report,
        findings_report_format=t.cast(
            t.Literal["json", "text"], namespace.findings_report_format
        ),
        max_sample_findings=int(namespace.max_sample_findings),
        report_all_findings=bool(namespace.report_all_findings),
        disk_space_mode=t.cast(DiskSpaceMode, namespace.disk_space_mode),
        ctest_timeout=(
            int(namespace.ctest_timeout)
            if namespace.ctest_timeout is not None
            else None
        ),
    )


def write_json(path: pathlib.Path, payload: t.Mapping[str, t.Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(as_json(payload) + "\n", encoding="utf-8")


def read_json(path: pathlib.Path) -> dict[str, t.Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_isoformat_utc(value: str) -> dt.datetime:
    return dt.datetime.strptime(value, ISO_UTC_FORMAT).replace(tzinfo=dt.timezone.utc)


def format_duration(seconds: float) -> str:
    seconds_int = max(0, int(seconds))
    hours, remainder = divmod(seconds_int, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes:02d}m {secs:02d}s"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


def format_bytes(byte_count: int) -> str:
    sign = "-" if byte_count < 0 else ""
    value = float(abs(byte_count))
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    unit_index = 0
    while value >= 1024.0 and unit_index < len(units) - 1:
        value /= 1024.0
        unit_index += 1

    if unit_index == 0:
        return f"{sign}{int(value)}{units[unit_index]}"
    return f"{sign}{value:.1f}{units[unit_index]}"


def format_counts(counts: dict[str, int]) -> str:
    if not counts:
        return "none"
    ordered_items = sorted(counts.items(), key=lambda item: item[0])
    return ", ".join(f"{name}={value}" for name, value in ordered_items)


def emit_status(message: str) -> None:
    timestamp = isoformat_utc(utc_now())
    print(f"[{timestamp}] {message}", flush=True)


def tail_file_lines(path: pathlib.Path, line_count: int) -> list[str]:
    if line_count <= 0 or not path.exists():
        return []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        return [
            line.rstrip("\n") for line in collections.deque(handle, maxlen=line_count)
        ]


def resolve_run_dir(output_root: pathlib.Path, run_dir_arg: str | None) -> pathlib.Path:
    if run_dir_arg:
        return pathlib.Path(run_dir_arg).resolve()
    latest_file = output_root / "latest_run_path.txt"
    if not latest_file.exists():
        raise FileNotFoundError(
            f"No latest_run_path.txt found under {output_root}; specify --run-dir explicitly"
        )
    return pathlib.Path(latest_file.read_text(encoding="utf-8").strip()).resolve()


def append_active_phase_lines(
    lines: list[str], active_phase: dict[str, t.Any], tail_lines_count: int
) -> None:
    lines.append("")
    lines.append(
        f"Active: lane={active_phase.get('lane', '?')} phase={active_phase.get('phase', '?')}"
    )
    started_at = active_phase.get("started_at")
    if started_at:
        elapsed = utc_now() - parse_isoformat_utc(started_at)
        lines.append(f"Elapsed: {format_duration(elapsed.total_seconds())}")

    command = active_phase.get("command")
    if command:
        lines.append(f"Command: {' '.join(command)}")

    log_path_value = active_phase.get("log_path")
    if not log_path_value:
        return

    log_path = pathlib.Path(log_path_value)
    if not log_path.exists():
        lines.append(f"Log: {log_path} (not created yet)")
        return

    stat_result = log_path.stat()
    modified_at = dt.datetime.fromtimestamp(
        stat_result.st_mtime, tz=dt.timezone.utc
    ).strftime(ISO_UTC_FORMAT)
    lines.append(
        f"Log: {log_path} ({stat_result.st_size} bytes, modified {modified_at})"
    )
    lines.append("")
    lines.append("Log tail:")
    tail_lines = tail_file_lines(log_path, tail_lines_count)
    if tail_lines:
        lines.extend(tail_lines)
    else:
        lines.append("(log exists but has no tail lines yet)")


def append_idle_phase_lines(lines: list[str], run_dir: pathlib.Path) -> None:
    lines.append("")
    lines.append("Active: none")
    summary_path = run_dir / "summary.json"
    if summary_path.exists():
        lines.append(f"Summary: {summary_path}")


def append_monitor_summary_lines(lines: list[str], status: dict[str, t.Any]) -> None:
    counts = status.get("counts", {})
    lines.append(
        "Counts: "
        f"lanes_total={counts.get('lanes_total', 0)} "
        f"lanes_ok={counts.get('lanes_ok', 0)} "
        f"lanes_failed={counts.get('lanes_failed', 0)} "
        f"findings_total={counts.get('findings_total', 0)}"
    )

    parallelism = status.get("parallelism", {})
    if parallelism:
        lines.append(
            "Parallelism: "
            f"build_jobs={parallelism.get('build_jobs', '?')} "
            f"test_jobs={parallelism.get('test_jobs', '?')}"
        )
        note = parallelism.get("test_parallelism_note")
        if note:
            lines.append(f"Note: {note}")

    completed_lanes = status.get("completed_lanes", [])
    lines.append(
        f"Completed lanes: {', '.join(completed_lanes) if completed_lanes else '(none)'}"
    )
    failed_lanes = status.get("failed_lanes", [])
    if isinstance(failed_lanes, list) and failed_lanes:
        lines.append("Failed lane reasons:")
        for lane in failed_lanes:
            if not isinstance(lane, dict):
                continue
            lane_name = lane.get("lane", "?")
            reasons = lane.get("failure_reasons", [])
            if not isinstance(reasons, list) or not reasons:
                lines.append(f"- {lane_name}: no structured reason recorded")
                continue
            reason_messages = []
            for reason in reasons[:3]:
                if isinstance(reason, dict):
                    reason_messages.append(str(reason.get("message", reason)))
                else:
                    reason_messages.append(str(reason))
            if len(reasons) > 3:
                reason_messages.append("...")
            lines.append(f"- {lane_name}: {'; '.join(reason_messages)}")


def render_monitor(run_dir: pathlib.Path, tail_lines_count: int) -> str:
    status = read_json(run_dir / STATUS_FILE_NAME)

    lines: list[str] = []
    lines.append("Sanitizer Matrix Monitor")
    lines.append(f"Run ID: {status.get('run_id', 'unknown')}")
    lines.append(f"Output: {run_dir}")
    lines.append(f"Updated: {status.get('updated_at', 'unknown')}")
    append_monitor_summary_lines(lines, status)

    active_phase = status.get("active_phase")
    if active_phase:
        append_active_phase_lines(lines, active_phase, tail_lines_count)
    else:
        append_idle_phase_lines(lines, run_dir)

    lines.append("")
    lines.append(
        f"Monitor command: python3 tools/ci/run_sanitizer_matrix.py --monitor --run-dir {run_dir} --follow"
    )
    return "\n".join(lines) + "\n"


def run_monitor(args: CliArgs, output_root: pathlib.Path) -> None:
    run_dir = resolve_run_dir(output_root, args.run_dir)

    while True:
        snapshot = render_monitor(run_dir, args.tail_lines)
        if args.follow and sys.stdout.isatty():
            sys.stdout.write("\033[2J\033[H")
        sys.stdout.write(snapshot)
        sys.stdout.flush()

        if not args.follow:
            return

        status = read_json(run_dir / STATUS_FILE_NAME)

        if not status.get("in_progress_lane") and not status.get("active_phase"):
            return
        time.sleep(args.refresh_seconds)


def make_active_phase(
    *,
    lane_name: str,
    phase_name: str,
    command: list[str],
    log_path: pathlib.Path,
) -> dict[str, t.Any]:
    return {
        "lane": lane_name,
        "phase": phase_name,
        "started_at": isoformat_utc(utc_now()),
        "command": command,
        "log_path": str(log_path),
    }


def load_completed_lane_results(
    run_dir: pathlib.Path, requested_lane_names: list[str]
) -> list[LaneResult]:
    lane_results: list[LaneResult] = []
    for lane_name in requested_lane_names:
        lane_summary_path = run_dir / lane_name / "lane_summary.json"
        if not lane_summary_path.exists():
            continue
        lane_results.append(t.cast(LaneResult, read_json(lane_summary_path)))
    return lane_results


def detect_symbolizer_binary() -> pathlib.Path | None:
    candidates = (
        os.environ.get("ASAN_SYMBOLIZER_PATH"),
        os.environ.get("MSAN_SYMBOLIZER_PATH"),
        shutil.which("llvm-symbolizer"),
        shutil.which("llvm-symbolizer-22"),
        shutil.which("llvm-symbolizer-18"),
        shutil.which("llvm-symbolizer-17"),
        shutil.which("llvm-symbolizer-16"),
    )
    for candidate in candidates:
        if not candidate:
            continue
        path = pathlib.Path(candidate).expanduser().resolve()
        if path.is_file() and os.access(path, os.X_OK):
            return path
    return None


def prepare_symbolizer_wrapper(
    run_dir: pathlib.Path, symbolizer_path: pathlib.Path | None
) -> pathlib.Path | None:
    if symbolizer_path is None:
        return None

    if symbolizer_path.name == "llvm-symbolizer":
        return symbolizer_path

    tooling_dir = run_dir / "tooling"
    tooling_dir.mkdir(parents=True, exist_ok=True)
    wrapper_path = tooling_dir / "llvm-symbolizer"
    if wrapper_path.exists() or wrapper_path.is_symlink():
        wrapper_path.unlink()
    wrapper_path.symlink_to(symbolizer_path)
    return wrapper_path


def build_lane_env(
    base_env: dict[str, str],
    symbolizer_wrapper: pathlib.Path | None,
) -> dict[str, str]:
    lane_env = dict(base_env)
    if symbolizer_wrapper is None:
        return lane_env

    lane_env["ASAN_SYMBOLIZER_PATH"] = str(symbolizer_wrapper)
    lane_env["MSAN_SYMBOLIZER_PATH"] = str(symbolizer_wrapper)
    wrapper_dir = str(symbolizer_wrapper.parent)
    existing_path = lane_env.get("PATH") or os.environ.get("PATH", "")
    lane_env["PATH"] = (
        f"{wrapper_dir}{os.pathsep}{existing_path}" if existing_path else wrapper_dir
    )
    return lane_env


def get_lane_runtime_options_env_var(lane_name: str) -> str | None:
    if lane_name.startswith("asan"):
        return "ASAN_OPTIONS"
    if lane_name.startswith("lsan"):
        return "LSAN_OPTIONS"
    if lane_name.startswith("msan"):
        return "MSAN_OPTIONS"
    if lane_name.startswith("tsan"):
        return "TSAN_OPTIONS"
    if lane_name.startswith("ubsan"):
        return "UBSAN_OPTIONS"
    return None


def append_sanitizer_options(
    existing_value: str | None, extra_options: dict[str, str]
) -> str:
    parts: list[str] = []
    if existing_value:
        parts.append(existing_value)
    parts.extend(f"{key}={value}" for key, value in extra_options.items())
    return ":".join(parts)


def configure_lane_runtime_reports(
    lane_env: dict[str, str], lane_name: str, lane_dir: pathlib.Path
) -> RuntimeReportArtifacts:
    runtime_report_dir = lane_dir / "runtime-reports"
    runtime_report_dir.mkdir(parents=True, exist_ok=True)
    runtime_report_base = runtime_report_dir / "sanitizer"

    runtime_env_var = get_lane_runtime_options_env_var(lane_name)
    if runtime_env_var is not None:
        lane_env[runtime_env_var] = append_sanitizer_options(
            lane_env.get(runtime_env_var),
            {
                "log_path": str(runtime_report_base),
                "log_exe_name": "1",
                "handle_abort": "1",
            },
        )

    return {
        "runtime_report_dir": str(runtime_report_dir),
        "runtime_report_base": str(runtime_report_base),
        "runtime_report_glob": str(runtime_report_dir / "*"),
    }


def configure_lane_non_test_phase_env(lane_env: dict[str, str], lane_name: str) -> None:
    """Preserve strict sanitizer behavior across configure, build, and test.

    Generator binaries and other build-time helpers are part of the lane's real
    attack surface. Suppressing sanitizer failures here would turn genuine
    defects into non-blocking behavior and violate the repository's generator
    sanitizer contract.
    """

    del lane_env
    del lane_name


def list_runtime_report_files(runtime_report_dir: pathlib.Path) -> list[pathlib.Path]:
    if not runtime_report_dir.exists():
        return []
    return sorted(path for path in runtime_report_dir.iterdir() if path.is_file())


def resolve_lane_build_dir(repo_root: pathlib.Path, build_dir: str) -> pathlib.Path:
    build_path = (repo_root / build_dir).resolve()
    try:
        build_path.relative_to(repo_root)
    except ValueError as exc:
        raise ValueError(f"Build directory escapes repo root: {build_dir}") from exc
    if build_path == repo_root:
        raise ValueError("Build directory must not resolve to the repository root")
    return build_path


def resolve_effective_disk_space_mode(
    disk_space_mode: DiskSpaceMode, resume_run_dir: str | None
) -> EffectiveDiskSpaceMode:
    if disk_space_mode == "auto":
        return "off" if resume_run_dir else "ephemeral-builds"
    if resume_run_dir and disk_space_mode != "off":
        raise ValueError(
            "--disk-space-mode is not compatible with --resume-run-dir because resume requires preserved build directories"
        )
    return t.cast(EffectiveDiskSpaceMode, disk_space_mode)


def should_clean_build_dir_before_lane(
    disk_space_mode: EffectiveDiskSpaceMode,
) -> bool:
    return disk_space_mode in {"fresh-builds", "ephemeral-builds"}


def should_clean_build_dir_after_lane(
    disk_space_mode: EffectiveDiskSpaceMode,
) -> bool:
    return disk_space_mode == "ephemeral-builds"


def cleanup_build_dir(build_dir: pathlib.Path, reason: str) -> None:
    if not build_dir.exists():
        emit_status(f"disk cleanup skipped ({reason}): {build_dir} does not exist")
        return

    usage_before = shutil.disk_usage(build_dir.parent)
    emit_status(
        f"disk cleanup started ({reason}): path={build_dir} free_before={format_bytes(usage_before.free)}"
    )

    if build_dir.is_symlink() or build_dir.is_file():
        build_dir.unlink()
    else:
        shutil.rmtree(build_dir)

    usage_after = shutil.disk_usage(build_dir.parent)
    free_delta = usage_after.free - usage_before.free
    emit_status(
        f"disk cleanup finished ({reason}): path={build_dir} free_delta={format_bytes(free_delta)} free_now={format_bytes(usage_after.free)}"
    )


def cleanup_sanitizer_build_dirs(
    repo_root: pathlib.Path, lanes: t.Iterable[Lane], reason: str
) -> list[pathlib.Path]:
    resolved_build_dirs: list[pathlib.Path] = []
    seen_build_dirs: set[pathlib.Path] = set()
    for lane in lanes:
        build_dir_path = resolve_lane_build_dir(repo_root, lane.build_dir)
        if build_dir_path in seen_build_dirs:
            continue
        seen_build_dirs.add(build_dir_path)
        resolved_build_dirs.append(build_dir_path)

    cleaned_build_dirs: list[pathlib.Path] = []
    for build_dir_path in resolved_build_dirs:
        if not build_dir_path.exists() and not build_dir_path.is_symlink():
            continue
        cleanup_build_dir(build_dir_path, reason)
        cleaned_build_dirs.append(build_dir_path)

    return cleaned_build_dirs


def emit_running_heartbeat(
    *,
    phase_label: str,
    started_at: dt.datetime,
    status_detail: t.Literal["basic", "detailed"],
    log_path: pathlib.Path,
    last_log_size: int,
    last_output_at: dt.datetime,
) -> tuple[int, dt.datetime]:
    elapsed = utc_now() - started_at
    stat_result = log_path.stat()
    current_log_size = stat_result.st_size
    delta_size = current_log_size - last_log_size

    updated_last_output_at = last_output_at
    if delta_size > 0:
        updated_last_output_at = utc_now()

    if status_detail == "detailed":
        idle_duration = utc_now() - updated_last_output_at
        delta_label = (
            f"+{format_bytes(delta_size)}"
            if delta_size >= 0
            else format_bytes(delta_size)
        )
        emit_status(
            f"{phase_label} running ({format_duration(elapsed.total_seconds())}) "
            f"log={format_bytes(current_log_size)} "
            f"delta={delta_label} "
            f"idle={format_duration(idle_duration.total_seconds())}"
        )
    else:
        emit_status(
            f"{phase_label} running ({format_duration(elapsed.total_seconds())})"
        )

    return current_log_size, updated_last_output_at


def emit_failure_log_tail(
    *,
    phase_label: str,
    log_path: pathlib.Path,
    status_detail: t.Literal["basic", "detailed"],
) -> None:
    if status_detail != "detailed":
        return

    tail_lines = tail_file_lines(log_path, 30)
    if not tail_lines:
        return

    emit_status(f"{phase_label} failure tail (last {len(tail_lines)} lines):")
    for tail_line in tail_lines:
        emit_status(f"{phase_label} | {tail_line}")


def run_command(
    *,
    command: list[str],
    cwd: pathlib.Path,
    env_overrides: dict[str, str],
    log_path: pathlib.Path,
    lane_name: str,
    phase_name: str,
    heartbeat_seconds: float,
    status_detail: t.Literal["basic", "detailed"],
    append: bool = False,
) -> CommandResult:
    started_at = utc_now()
    merged_env = os.environ.copy()
    merged_env.update(env_overrides)
    phase_label = f"{lane_name}:{phase_name}"
    emit_status(f"{phase_label} started")
    emit_status(f"{phase_label} log={log_path}")
    emit_status(f"{phase_label} command={' '.join(command)}")

    log_path.parent.mkdir(parents=True, exist_ok=True)
    open_mode = "a" if append else "w"
    return_code: int | None = None
    with log_path.open(open_mode, encoding="utf-8") as log_file:
        if append and log_path.stat().st_size > 0:
            log_file.write("\n# --- resumed command invocation ---\n")
        log_file.write(f"# started_at={isoformat_utc(started_at)}\n")
        log_file.write(f"# cwd={cwd}\n")
        log_file.write(f"# command={' '.join(command)}\n")
        if env_overrides:
            for key, value in sorted(env_overrides.items()):
                log_file.write(f"# env:{key}={value}\n")
        log_file.write("\n")
        log_file.flush()

        process = subprocess.Popen(
            command,
            cwd=str(cwd),
            env=merged_env,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )
        next_heartbeat_at = time.monotonic() + heartbeat_seconds
        last_log_size = log_path.stat().st_size
        last_output_at = started_at
        while True:
            return_code = process.poll()
            if return_code is not None:
                break

            now_monotonic = time.monotonic()
            if now_monotonic >= next_heartbeat_at:
                current_log_size, last_output_at = emit_running_heartbeat(
                    phase_label=phase_label,
                    started_at=started_at,
                    status_detail=status_detail,
                    log_path=log_path,
                    last_log_size=last_log_size,
                    last_output_at=last_output_at,
                )
                next_heartbeat_at = now_monotonic + heartbeat_seconds
                last_log_size = current_log_size
                log_file.flush()
            time.sleep(1.0)

    ended_at = utc_now()
    if return_code is None:
        raise RuntimeError(f"{phase_label} finished without a process return code")
    duration = ended_at - started_at
    if return_code == 0:
        emit_status(
            f"{phase_label} finished successfully in {format_duration(duration.total_seconds())}"
        )
    else:
        emit_status(
            f"{phase_label} failed with exit code {return_code} after {format_duration(duration.total_seconds())}"
        )
        emit_failure_log_tail(
            phase_label=phase_label,
            log_path=log_path,
            status_detail=status_detail,
        )
    return {
        "command": command,
        "cwd": str(cwd),
        "log_path": str(log_path),
        "started_at": isoformat_utc(started_at),
        "ended_at": isoformat_utc(ended_at),
        "duration_seconds": round(duration.total_seconds(), 3),
        "exit_code": return_code,
        "ok": return_code == 0,
        "appended": append,
    }


def try_collect_test_discovery(
    *,
    lane_name: str,
    build_dir: pathlib.Path,
    lane_dir: pathlib.Path,
    env_overrides: dict[str, str],
) -> DiscoveryResult:
    discovery_path = lane_dir / "ctest-discovery.json"
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--show-only=json-v1",
    ]
    started_at = utc_now()
    emit_status(f"{lane_name}:discovery started")
    merged_env = os.environ.copy()
    merged_env.update(env_overrides)
    completed = subprocess.run(
        command,
        cwd=str(build_dir.parent),
        env=merged_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    ended_at = utc_now()

    test_count: int | None = None
    if completed.returncode == 0:
        discovery_path.write_text(completed.stdout, encoding="utf-8")
        try:
            discovery_payload = json.loads(completed.stdout)
            test_count = len(discovery_payload.get("tests", []))
        except json.JSONDecodeError:
            test_count = None
        emit_status(
            f"{lane_name}:discovery finished successfully "
            f"tests={test_count if test_count is not None else 'unknown'}"
        )
    else:
        (lane_dir / "ctest-discovery-error.log").write_text(
            completed.stdout + "\n\n" + completed.stderr,
            encoding="utf-8",
        )
        emit_status(
            f"{lane_name}:discovery failed with exit code {completed.returncode}"
        )

    return {
        "command": command,
        "started_at": isoformat_utc(started_at),
        "ended_at": isoformat_utc(ended_at),
        "exit_code": completed.returncode,
        "ok": completed.returncode == 0,
        "test_count": test_count,
        "artifact": str(discovery_path) if completed.returncode == 0 else None,
        "stderr_log": (
            str(lane_dir / "ctest-discovery-error.log")
            if completed.returncode != 0
            else None
        ),
    }


def parse_junit(junit_path: pathlib.Path) -> JunitSummary | None:
    if not junit_path.exists():
        return None

    source = junit_path.read_text(encoding="utf-8", errors="replace")
    test_suite_tags = re.findall(r"<testsuite\b[^>]*>", source)
    if not test_suite_tags:
        return {
            "parse_error": True,
            "path": str(junit_path),
        }

    totals: JunitSummary = {
        "tests": 0,
        "failures": 0,
        "errors": 0,
        "skipped": 0,
        "time_seconds": 0.0,
    }

    for suite_tag in test_suite_tags:
        attributes = dict(
            re.findall(r"([A-Za-z_][A-Za-z0-9_:-]*)=\"([^\"]*)\"", suite_tag)
        )
        totals["tests"] += int(attributes.get("tests", "0"))
        totals["failures"] += int(attributes.get("failures", "0"))
        totals["errors"] += int(attributes.get("errors", "0"))
        totals["skipped"] += int(attributes.get("skipped", "0"))
        try:
            totals["time_seconds"] += float(attributes.get("time", "0"))
        except ValueError:
            pass

    totals["path"] = str(junit_path)
    totals["time_seconds"] = round(totals["time_seconds"], 3)
    return totals


def parse_failed_ctest_tests(ctest_output_log: pathlib.Path) -> list[FailedTestRecord]:
    if not ctest_output_log.exists():
        return []

    failures_by_name: dict[str, FailedTestRecord] = {}
    inline_failure_re = re.compile(
        r"^\s*\d+/\d+\s+Test\s+#?(?P<id>\d+):\s+(?P<name>\S+)\s+.*?(?P<status>\*\*\*Failed|Timeout)\b"
    )
    summary_failure_re = re.compile(
        r"^\s*(?P<id>\d+)\s+-\s+(?P<name>\S+)\s+\((?P<status>Failed|Timeout)\)"
    )

    with ctest_output_log.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = inline_failure_re.search(line)
            if match is not None:
                status = "Failed" if match.group("status") == "***Failed" else match.group("status")
                failures_by_name.setdefault(
                    match.group("name"),
                    {
                        "id": int(match.group("id")),
                        "name": match.group("name"),
                        "status": status,
                    },
                )
                continue

            match = summary_failure_re.search(line)
            if match is not None:
                failures_by_name[match.group("name")] = {
                    "id": int(match.group("id")),
                    "name": match.group("name"),
                    "status": match.group("status"),
                }

    return sorted(failures_by_name.values(), key=lambda item: (item["id"], item["name"]))


def merge_failed_ctest_tests(
    failed_test_groups: t.Iterable[list[FailedTestRecord]],
) -> list[FailedTestRecord]:
    failures_by_name: dict[str, FailedTestRecord] = {}
    for failed_tests in failed_test_groups:
        for item in failed_tests:
            failures_by_name[item["name"]] = item
    return sorted(failures_by_name.values(), key=lambda item: (item["id"], item["name"]))


def parse_ctest_pass_summary(log_path: pathlib.Path) -> CtestPassSummary | None:
    if not log_path.exists():
        return None

    summary_re = re.compile(
        r"(?P<percent>\d+)% tests passed,\s+"
        r"(?P<failed>\d+) tests failed out of (?P<total>\d+)"
    )
    summary: CtestPassSummary | None = None
    with log_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = summary_re.search(line)
            if match is not None:
                summary = {
                    "path": str(log_path),
                    "percent": int(match.group("percent")),
                    "failed": int(match.group("failed")),
                    "total": int(match.group("total")),
                }
    return summary


def audit_ctest_evidence(
    *,
    test_phase_result: CommandResult,
    discovery_result: CommandResult,
    ctest_command_log: pathlib.Path,
    ctest_output_log: pathlib.Path,
    ctest_junit: pathlib.Path,
    junit_summary: JunitSummary | None,
    failed_tests: list[FailedTestRecord],
) -> CtestEvidenceAudit:
    issues: list[str] = []
    warnings: list[str] = []

    command_log_present = ctest_command_log.exists() and ctest_command_log.stat().st_size > 0
    output_log_present = ctest_output_log.exists() and ctest_output_log.stat().st_size > 0
    junit_present = ctest_junit.exists() and ctest_junit.stat().st_size > 0

    if not test_phase_result.get("ok"):
        issues.append("ctest phase did not exit successfully")
    if not command_log_present:
        issues.append("ctest command log is missing or empty")
    if not output_log_present:
        warnings.append("ctest --output-log artifact is missing or empty")
    if not junit_present:
        issues.append("ctest JUnit artifact is missing or empty")

    if junit_summary is None:
        issues.append("ctest JUnit summary is unavailable")
    elif junit_summary.get("parse_error"):
        issues.append("ctest JUnit summary could not be parsed")
    else:
        junit_tests = int(junit_summary.get("tests", 0))
        junit_failures = int(junit_summary.get("failures", 0))
        junit_errors = int(junit_summary.get("errors", 0))
        if junit_tests <= 0:
            issues.append("ctest JUnit summary reports zero executed tests")
        if junit_failures or junit_errors:
            issues.append(
                f"ctest JUnit summary reports failures={junit_failures} errors={junit_errors}"
            )

        discovery_count_obj = discovery_result.get("test_count")
        discovery_count = (
            discovery_count_obj if isinstance(discovery_count_obj, int) else None
        )
        if not discovery_result.get("ok"):
            issues.append("ctest discovery did not exit successfully")
        elif discovery_count is None:
            issues.append("ctest discovery test count is unavailable")
        elif discovery_count <= 0:
            issues.append("ctest discovery reports zero tests")
        elif junit_tests and discovery_count != junit_tests:
            issues.append(
                f"ctest discovery/JUnit test count mismatch: discovery={discovery_count} junit={junit_tests}"
            )

    if failed_tests:
        failed_test_names = ", ".join(item["name"] for item in failed_tests[:5])
        if len(failed_tests) > 5:
            failed_test_names += ", ..."
        issues.append(f"ctest output reports failed tests: {failed_test_names}")

    pass_summaries = [
        summary
        for summary in (
            parse_ctest_pass_summary(ctest_output_log),
            parse_ctest_pass_summary(ctest_command_log),
        )
        if summary is not None
    ]
    if not pass_summaries:
        warnings.append("no human-readable CTest pass summary was captured")
    for summary in pass_summaries:
        if summary["failed"] != 0:
            issues.append(
                f"CTest pass summary reports {summary['failed']} failed tests in {summary['path']}"
            )
        if junit_summary is not None and not junit_summary.get("parse_error"):
            junit_tests = int(junit_summary.get("tests", 0))
            if junit_tests and summary["total"] != junit_tests:
                issues.append(
                    f"CTest pass summary/JUnit test count mismatch in {summary['path']}: "
                    f"summary={summary['total']} junit={junit_tests}"
                )

    return {
        "ok": not issues,
        "issues": issues,
        "warnings": warnings,
        "command_log_present": command_log_present,
        "output_log_present": output_log_present,
        "junit_present": junit_present,
        "ctest_pass_summaries": pass_summaries,
    }


def summarize_failed_test_names(
    failed_tests: list[FailedTestRecord], limit: int = 5
) -> str:
    failed_test_names = ", ".join(item["name"] for item in failed_tests[:limit])
    if len(failed_tests) > limit:
        failed_test_names += ", ..."
    return failed_test_names


def build_lane_failure_reasons(
    *,
    phase_results: dict[str, CommandResult],
    ctest_evidence: CtestEvidenceAudit,
    failed_tests: list[FailedTestRecord],
    combined_findings: FindingsSummary,
) -> list[FailureReason]:
    reasons: list[FailureReason] = []
    skipped_phases: list[str] = []

    for phase_name in (
        "configure",
        "build",
        "test_discovery_configure",
        "discovery",
        "test",
    ):
        phase_result = phase_results.get(phase_name, {})
        if phase_result.get("ok"):
            continue
        if phase_result.get("skipped"):
            skipped_phases.append(phase_name)
            continue

        exit_code = phase_result.get("exit_code")
        log_path_obj = phase_result.get("log_path") or phase_result.get("stderr_log")
        log_path = log_path_obj if isinstance(log_path_obj, str) else None
        message = f"{phase_name} phase failed"
        if exit_code is not None:
            message += f" with exit code {exit_code}"
        reason: FailureReason = {
            "category": "phase_failed",
            "phase": phase_name,
            "message": message,
        }
        if exit_code is not None:
            reason["exit_code"] = exit_code
        if log_path:
            reason["log_path"] = log_path
        reasons.append(reason)

    if not reasons and skipped_phases:
        reasons.append(
            {
                "category": "phase_skipped",
                "message": f"required phases were skipped: {', '.join(skipped_phases)}",
                "phases": skipped_phases,
            }
        )

    for issue in ctest_evidence.get("issues", []):
        reasons.append(
            {
                "category": "ctest_evidence",
                "message": str(issue),
            }
        )

    if failed_tests:
        reasons.append(
            {
                "category": "failed_tests",
                "message": f"CTest failed tests: {summarize_failed_test_names(failed_tests)}",
                "tests": failed_tests,
                "count": len(failed_tests),
            }
        )

    if combined_findings["total_matches"]:
        reasons.append(
            {
                "category": "sanitizer_findings",
                "message": f"sanitizer findings detected: {combined_findings['total_matches']}",
                "count": combined_findings["total_matches"],
                "counts_by_category": combined_findings["counts_by_category"],
                "counts_by_severity": combined_findings["counts_by_severity"],
            }
        )

    return reasons


def iter_log_pattern_matches(
    log_path: pathlib.Path,
    compiled_patterns: list[tuple[FindingPattern, re.Pattern[str]]],
) -> t.Iterator[tuple[FindingPattern, int, str]]:
    if not log_path.exists():
        return

    with log_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, start=1):
            for pattern_info, compiled in compiled_patterns:
                if compiled.search(line):
                    yield pattern_info, line_number, line


def collect_findings(
    log_paths: list[pathlib.Path], max_sample_findings: int
) -> FindingsSummary:
    compiled_patterns: list[tuple[FindingPattern, re.Pattern[str]]] = [
        (pattern, re.compile(pattern["regex"]))
        for pattern in SANITIZER_FINDING_PATTERNS
    ]

    findings: list[FindingRecord] = []
    counts_by_category: dict[str, int] = {}
    counts_by_severity: dict[str, int] = {}

    for log_path in log_paths:
        for pattern_info, line_number, line in iter_log_pattern_matches(
            log_path, compiled_patterns
        ):
            category = pattern_info["category"]
            severity = pattern_info["severity"]
            counts_by_category[category] = counts_by_category.get(category, 0) + 1
            counts_by_severity[severity] = counts_by_severity.get(severity, 0) + 1

            should_store = (
                max_sample_findings < 0 or len(findings) < max_sample_findings
            )
            if should_store:
                findings.append(
                    {
                        "category": category,
                        "severity": severity,
                        "risk": pattern_info["risk"],
                        "principles": pattern_info["principles"],
                        "file": str(log_path),
                        "line": line_number,
                        "snippet": line.strip()[:1000],
                    }
                )

    total_matches = sum(counts_by_category.values())
    truncated = max_sample_findings >= 0 and total_matches > len(findings)

    return {
        "counts_by_category": counts_by_category,
        "counts_by_severity": counts_by_severity,
        "total_matches": total_matches,
        "sample_findings": findings,
        "sample_limit": max_sample_findings,
        "sample_truncated": truncated,
    }


def merge_findings_summaries(
    summaries: t.Iterable[FindingsSummary], max_sample_findings: int
) -> FindingsSummary:
    counts_by_category: dict[str, int] = {}
    counts_by_severity: dict[str, int] = {}
    sample_findings: list[FindingRecord] = []
    total_matches = 0
    sample_truncated = False

    for summary in summaries:
        total_matches += summary["total_matches"]
        for category, count in summary["counts_by_category"].items():
            counts_by_category[category] = counts_by_category.get(category, 0) + count
        for severity, count in summary["counts_by_severity"].items():
            counts_by_severity[severity] = counts_by_severity.get(severity, 0) + count

        if max_sample_findings == -1:
            sample_findings.extend(summary["sample_findings"])
        else:
            remaining = max_sample_findings - len(sample_findings)
            if remaining > 0:
                sample_findings.extend(summary["sample_findings"][:remaining])

        if summary["sample_truncated"]:
            sample_truncated = True

    if max_sample_findings != -1 and total_matches > len(sample_findings):
        sample_truncated = True

    return {
        "counts_by_category": counts_by_category,
        "counts_by_severity": counts_by_severity,
        "total_matches": total_matches,
        "sample_findings": sample_findings,
        "sample_limit": max_sample_findings,
        "sample_truncated": sample_truncated,
    }


def set_latest_pointer(output_root: pathlib.Path, run_dir: pathlib.Path) -> None:
    latest_file = output_root / "latest_run_path.txt"
    latest_file.write_text(str(run_dir) + "\n", encoding="utf-8")


def compute_overall_counts(
    lane_results: t.Sequence[t.Mapping[str, object]],
) -> dict[str, t.Any]:
    lanes_total = len(lane_results)
    lanes_ok = sum(1 for lane in lane_results if lane.get("ok"))
    lanes_failed = lanes_total - lanes_ok

    total_findings = 0
    ctest_evidence_issues_total = 0
    ctest_evidence_warnings_total = 0
    severity_counts: dict[str, int] = {}
    for lane in lane_results:
        summaries = [lane.get("combined_findings")]
        if summaries[0] is None:
            summaries = [lane.get("findings"), lane.get("runtime_findings")]

        for findings in summaries:
            if not isinstance(findings, dict):
                continue
            total_findings += int(findings.get("total_matches", 0))
            counts_by_severity = findings.get("counts_by_severity", {})
            if not isinstance(counts_by_severity, dict):
                continue
            for severity, count in counts_by_severity.items():
                severity_counts[severity] = severity_counts.get(severity, 0) + int(count)

        ctest_evidence = lane.get("ctest_evidence")
        if isinstance(ctest_evidence, dict):
            issues = ctest_evidence.get("issues", [])
            warnings = ctest_evidence.get("warnings", [])
            if isinstance(issues, list):
                ctest_evidence_issues_total += len(issues)
            if isinstance(warnings, list):
                ctest_evidence_warnings_total += len(warnings)

    return {
        "lanes_total": lanes_total,
        "lanes_ok": lanes_ok,
        "lanes_failed": lanes_failed,
        "findings_total": total_findings,
        "findings_by_severity": severity_counts,
        "ctest_evidence_issues_total": ctest_evidence_issues_total,
        "ctest_evidence_warnings_total": ctest_evidence_warnings_total,
    }


def resolve_report_path(repo_root: pathlib.Path, report_arg: str) -> pathlib.Path:
    report_path = pathlib.Path(report_arg).expanduser()
    if report_path.is_absolute():
        return report_path.resolve()
    return (repo_root / report_path).resolve()


def empty_findings_summary() -> FindingsSummary:
    return {
        "counts_by_category": {},
        "counts_by_severity": {},
        "total_matches": 0,
        "sample_findings": [],
        "sample_limit": MAX_SNIPPET_FINDINGS_PER_LOG,
        "sample_truncated": False,
    }


def coerce_int(value: object, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return default


def coerce_str_list(value: object) -> list[str]:
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, str)]


def coerce_count_map(value: object) -> dict[str, int]:
    if not isinstance(value, dict):
        return {}

    output: dict[str, int] = {}
    for key, raw_value in value.items():
        if not isinstance(key, str):
            continue
        output[key] = coerce_int(raw_value)
    return output


def coerce_finding_record(value: object) -> FindingRecord | None:
    if not isinstance(value, dict):
        return None

    category_obj = value.get("category")
    severity_obj = value.get("severity")
    risk_obj = value.get("risk")
    file_obj = value.get("file")
    if not isinstance(category_obj, str) or not isinstance(severity_obj, str):
        return None
    if not isinstance(risk_obj, str) or not isinstance(file_obj, str):
        return None

    line_obj = value.get("line")
    snippet_obj = value.get("snippet")
    principles_obj = value.get("principles")

    return {
        "category": category_obj,
        "severity": severity_obj,
        "risk": risk_obj,
        "principles": coerce_str_list(principles_obj),
        "file": file_obj,
        "line": coerce_int(line_obj),
        "snippet": snippet_obj if isinstance(snippet_obj, str) else "",
    }


def coerce_findings_summary(value: object) -> FindingsSummary:
    if not isinstance(value, dict):
        return empty_findings_summary()

    sample_findings_obj = value.get("sample_findings")
    sample_findings: list[FindingRecord] = []
    if isinstance(sample_findings_obj, list):
        for item in sample_findings_obj:
            finding = coerce_finding_record(item)
            if finding is not None:
                sample_findings.append(finding)

    counts_by_category = coerce_count_map(value.get("counts_by_category"))
    counts_by_severity = coerce_count_map(value.get("counts_by_severity"))

    sample_limit = coerce_int(
        value.get("sample_limit"), default=MAX_SNIPPET_FINDINGS_PER_LOG
    )
    total_matches_default = sum(counts_by_category.values())
    total_matches = coerce_int(
        value.get("total_matches"),
        default=total_matches_default,
    )

    sample_truncated_obj = value.get("sample_truncated")
    if isinstance(sample_truncated_obj, bool):
        sample_truncated = sample_truncated_obj
    else:
        sample_truncated = sample_limit >= 0 and total_matches > len(sample_findings)

    return {
        "counts_by_category": counts_by_category,
        "counts_by_severity": counts_by_severity,
        "total_matches": total_matches,
        "sample_findings": sample_findings,
        "sample_limit": sample_limit,
        "sample_truncated": sample_truncated,
    }


def lane_findings_from_result(
    lane_result: t.Mapping[str, object],
) -> LaneFindingsView:
    lane_name_obj = lane_result.get("lane")
    lane_name = lane_name_obj if isinstance(lane_name_obj, str) else "unknown"

    return {
        "lane": lane_name,
        "findings": coerce_findings_summary(lane_result.get("findings")),
        "runtime_findings": coerce_findings_summary(
            lane_result.get("runtime_findings")
        ),
    }


def lane_findings_from_results(
    lane_results: t.Sequence[t.Mapping[str, object]],
) -> list[LaneFindingsView]:
    return [lane_findings_from_result(lane_result) for lane_result in lane_results]


def flatten_unified_findings(
    lane_findings: t.Sequence[LaneFindingsView],
) -> list[UnifiedFindingRecord]:
    flattened: list[UnifiedFindingRecord] = []
    for lane in lane_findings:
        lane_name = lane["lane"]

        for finding in lane["findings"]["sample_findings"]:
            flattened.append(
                {
                    "lane": lane_name,
                    "source": "normal",
                    "category": finding["category"],
                    "severity": finding["severity"],
                    "risk": finding["risk"],
                    "principles": list(finding["principles"]),
                    "file": finding["file"],
                    "line": finding["line"],
                    "snippet": finding["snippet"],
                }
            )

        for finding in lane["runtime_findings"]["sample_findings"]:
            flattened.append(
                {
                    "lane": lane_name,
                    "source": "runtime",
                    "category": finding["category"],
                    "severity": finding["severity"],
                    "risk": finding["risk"],
                    "principles": list(finding["principles"]),
                    "file": finding["file"],
                    "line": finding["line"],
                    "snippet": finding["snippet"],
                }
            )

    return flattened


def write_unified_findings_report(
    *,
    report_path: pathlib.Path,
    report_format: t.Literal["json", "text"],
    run_id: str,
    run_dir: pathlib.Path,
    lane_findings: t.Sequence[LaneFindingsView],
    max_sample_findings: int,
) -> None:
    findings_entries = flatten_unified_findings(lane_findings)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    if report_format == "json":
        payload = {
            "run_id": run_id,
            "generated_at": isoformat_utc(utc_now()),
            "run_dir": str(run_dir),
            "max_sample_findings": max_sample_findings,
            "entry_count": len(findings_entries),
            "findings": findings_entries,
        }
        report_path.write_text(as_json(payload) + "\n", encoding="utf-8")
    else:
        lines = [
            f"run_id={run_id}",
            f"generated_at={isoformat_utc(utc_now())}",
            f"run_dir={run_dir}",
            f"max_sample_findings={max_sample_findings}",
            f"entry_count={len(findings_entries)}",
            "",
        ]
        for entry in findings_entries:
            lines.append(
                f"[{entry['lane']}|{entry['source']}] {entry['severity']}/{entry['category']} "
                f"{entry['file']}:{entry['line']}"
            )
            lines.append(f"  risk: {entry['risk']}")
            lines.append(f"  snippet: {entry['snippet']}")
            lines.append("")
        report_path.write_text("\n".join(lines), encoding="utf-8")

    emit_status(
        f"findings report written path={report_path} format={report_format} entries={len(findings_entries)}"
    )


def write_default_unified_findings_reports(
    *,
    report_stem: pathlib.Path,
    run_id: str,
    run_dir: pathlib.Path,
    lane_findings: t.Sequence[LaneFindingsView],
    max_sample_findings: int,
) -> ReportPaths:
    json_path = report_stem.with_suffix(".json")
    text_path = report_stem.with_suffix(".txt")

    write_unified_findings_report(
        report_path=json_path,
        report_format="json",
        run_id=run_id,
        run_dir=run_dir,
        lane_findings=lane_findings,
        max_sample_findings=max_sample_findings,
    )
    write_unified_findings_report(
        report_path=text_path,
        report_format="text",
        run_id=run_id,
        run_dir=run_dir,
        lane_findings=lane_findings,
        max_sample_findings=max_sample_findings,
    )

    return ReportPaths(json_path=str(json_path), text_path=str(text_path))


def write_status(
    status_path: pathlib.Path,
    *,
    run_id: str,
    run_started_at: str,
    lane_results: list[LaneResult],
    in_progress_lane: str | None,
    output_dir: pathlib.Path,
    parallelism: dict[str, t.Any],
    active_phase: dict[str, t.Any] | None,
) -> None:
    failed_lanes = []
    for lane in lane_results:
        if lane.get("ok"):
            continue
        failed_lanes.append(
            {
                "lane": lane.get("lane"),
                "failure_reasons": lane.get("failure_reasons", []),
                "failed_tests": lane.get("failed_tests", []),
                "failed_tests_total": lane.get("failed_tests_total", 0),
            }
        )
    payload = {
        "run_id": run_id,
        "run_started_at": run_started_at,
        "updated_at": isoformat_utc(utc_now()),
        "in_progress_lane": in_progress_lane,
        "completed_lanes": [lane["lane"] for lane in lane_results],
        "failed_lanes": failed_lanes,
        "counts": compute_overall_counts(lane_results),
        "output_dir": str(output_dir),
        "parallelism": parallelism,
    }
    if active_phase is not None:
        payload["active_phase"] = active_phase
    write_json(status_path, payload)


def main() -> int:
    args = parse_args()
    repo_root = pathlib.Path(args.repo_root).resolve()
    output_root = (repo_root / args.output_root).resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    if args.monitor:
        run_monitor(args, output_root)
        return 0

    lane_map = load_lane_map()
    requested_lane_names = [
        name.strip() for name in args.lanes.split(",") if name.strip()
    ]
    unknown_lanes = [name for name in requested_lane_names if name not in lane_map]
    if unknown_lanes:
        print(f"Unknown lane names: {', '.join(unknown_lanes)}", file=sys.stderr)
        return 2

    selected_lanes = [lane_map[name] for name in requested_lane_names]
    effective_max_sample_findings = (
        -1 if args.report_all_findings else args.max_sample_findings
    )
    try:
        effective_disk_space_mode = resolve_effective_disk_space_mode(
            args.disk_space_mode, args.resume_run_dir
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if effective_max_sample_findings < 0:
        emit_status("findings capture mode: all matches will be stored")
    else:
        emit_status(
            f"findings capture mode: max_sample_findings={effective_max_sample_findings}"
        )
    emit_status(
        f"disk space mode: requested={args.disk_space_mode} effective={effective_disk_space_mode}"
    )
    if args.ctest_timeout == 0:
        emit_status(
            "ctest timeout mode: requested=0 mapped="
            f"{CTEST_EFFECTIVE_NO_TIMEOUT_SECONDS}s (effective no-timeout)"
        )

    parallelism = {
        "build_jobs": args.jobs,
        "test_jobs": args.jobs,
        "test_parallelism_note": (
            "The runner refreshes CMake test discovery after building run_all_tests so CTest can schedule exact test cases across the requested jobs."
        ),
    }

    lane_results: list[LaneResult]
    if args.resume_run_dir:
        run_dir = pathlib.Path(args.resume_run_dir).resolve()
        status_path = run_dir / STATUS_FILE_NAME
        resume_status = read_json(status_path)
        run_id = resume_status["run_id"]
        run_started = parse_isoformat_utc(resume_status["run_started_at"])
        lane_results = load_completed_lane_results(run_dir, requested_lane_names)
        set_latest_pointer(output_root, run_dir)
    else:
        run_started = utc_now()
        run_id = run_started.strftime("%Y%m%dT%H%M%SZ")
        run_dir = output_root / run_id
        run_dir.mkdir(parents=True, exist_ok=True)
        set_latest_pointer(output_root, run_dir)
        status_path = run_dir / STATUS_FILE_NAME
        lane_results = []

    if should_clean_build_dir_before_lane(effective_disk_space_mode):
        cleanup_sanitizer_build_dirs(repo_root, selected_lanes, "matrix preflight")

    symbolizer_binary = detect_symbolizer_binary()
    symbolizer_wrapper = prepare_symbolizer_wrapper(run_dir, symbolizer_binary)
    if symbolizer_wrapper is None:
        emit_status(
            "symbolizer: llvm-symbolizer not found; sanitizer stacks may be less readable"
        )
    else:
        emit_status(
            f"symbolizer: using {symbolizer_wrapper} (source {symbolizer_binary})"
        )

    write_status(
        status_path,
        run_id=run_id,
        run_started_at=isoformat_utc(run_started),
        lane_results=lane_results,
        in_progress_lane=None,
        output_dir=run_dir,
        parallelism=parallelism,
        active_phase=None,
    )

    completed_lane_names = {lane_result["lane"] for lane_result in lane_results}

    for lane in selected_lanes:
        if lane.name in completed_lane_names:
            emit_status(f"lane {lane.name}: skipped (already completed in resumed run)")
            continue

        emit_status(f"lane {lane.name}: started")

        build_dir_path = resolve_lane_build_dir(repo_root, lane.build_dir)
        if should_clean_build_dir_before_lane(effective_disk_space_mode):
            cleanup_sanitizer_build_dirs(
                repo_root, [lane], f"lane {lane.name} preflight"
            )

        lane_dir = run_dir / lane.name
        lane_dir.mkdir(parents=True, exist_ok=True)
        write_status(
            status_path,
            run_id=run_id,
            run_started_at=isoformat_utc(run_started),
            lane_results=lane_results,
            in_progress_lane=lane.name,
            output_dir=run_dir,
            parallelism=parallelism,
            active_phase=None,
        )

        lane_test_env = build_lane_env(lane.env, symbolizer_wrapper)

        lane_env = dict(lane_test_env)
        configure_lane_non_test_phase_env(lane_env, lane.name)
        lane_env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(args.jobs)

        # Scope sanitizer runtime log_path/log_exe_name to test execution only.
        # Applying them to configure/build can surface generator-tool findings and
        # fail lanes before run_all_tests has started.
        runtime_report_artifacts = configure_lane_runtime_reports(
            lane_test_env, lane.name, lane_dir
        )
        runtime_report_dir = pathlib.Path(
            runtime_report_artifacts["runtime_report_dir"]
        )

        configure_log = lane_dir / "configure.log"
        test_discovery_configure_log = lane_dir / "test-discovery-configure.log"
        build_log = lane_dir / "build.log"
        ctest_log = lane_dir / "ctest-command.log"
        ctest_output_log = lane_dir / "ctest-output.log"
        ctest_junit = lane_dir / "ctest-junit.xml"

        phase_results: dict[str, CommandResult] = {}
        resumed_lane = bool(args.resume_run_dir)
        configure_command = ["cmake", "--preset", lane.configure_preset]
        build_command = [
            "cmake",
            "--build",
            "--preset",
            lane.build_preset,
            "--parallel",
            str(args.jobs),
        ]
        discovery_command = [
            "ctest",
            "--test-dir",
            str(build_dir_path),
            "--show-only=json-v1",
        ]
        test_command = [
            "ctest",
            "--test-dir",
            str(build_dir_path),
            "--output-on-failure",
            "-j",
            str(args.jobs),
            "--output-junit",
            str(ctest_junit),
            "--output-log",
            str(ctest_output_log),
        ]
        if args.ctest_timeout is not None:
            effective_ctest_timeout = (
                CTEST_EFFECTIVE_NO_TIMEOUT_SECONDS
                if args.ctest_timeout == 0
                else args.ctest_timeout
            )
            test_command.extend(["--timeout", str(effective_ctest_timeout)])

        configure_checkpoint_exists = configure_log.exists()
        should_skip_configure = resumed_lane and configure_checkpoint_exists

        if should_skip_configure:
            phase_results["configure"] = {
                "ok": True,
                "skipped": True,
                "reason": "reused_configure_checkpoint",
                "log_path": str(configure_log),
            }
        else:
            write_status(
                status_path,
                run_id=run_id,
                run_started_at=isoformat_utc(run_started),
                lane_results=lane_results,
                in_progress_lane=lane.name,
                output_dir=run_dir,
                parallelism=parallelism,
                active_phase=make_active_phase(
                    lane_name=lane.name,
                    phase_name="configure",
                    command=configure_command,
                    log_path=configure_log,
                ),
            )

            phase_results["configure"] = run_command(
                command=configure_command,
                cwd=repo_root,
                env_overrides=lane_env,
                log_path=configure_log,
                lane_name=lane.name,
                phase_name="configure",
                heartbeat_seconds=args.heartbeat_seconds,
                status_detail=args.status_detail,
            )

        if phase_results["configure"]["ok"]:
            write_status(
                status_path,
                run_id=run_id,
                run_started_at=isoformat_utc(run_started),
                lane_results=lane_results,
                in_progress_lane=lane.name,
                output_dir=run_dir,
                parallelism=parallelism,
                active_phase=make_active_phase(
                    lane_name=lane.name,
                    phase_name="build",
                    command=build_command,
                    log_path=build_log,
                ),
            )
            phase_results["build"] = run_command(
                command=build_command,
                cwd=repo_root,
                env_overrides=lane_env,
                log_path=build_log,
                lane_name=lane.name,
                phase_name="build",
                heartbeat_seconds=args.heartbeat_seconds,
                status_detail=args.status_detail,
                append=resumed_lane and build_log.exists(),
            )
        else:
            phase_results["build"] = {
                "skipped": True,
                "reason": "configure_failed",
            }

        if phase_results["build"].get("ok"):
            write_status(
                status_path,
                run_id=run_id,
                run_started_at=isoformat_utc(run_started),
                lane_results=lane_results,
                in_progress_lane=lane.name,
                output_dir=run_dir,
                parallelism=parallelism,
                active_phase=make_active_phase(
                    lane_name=lane.name,
                    phase_name="test-discovery-configure",
                    command=configure_command,
                    log_path=test_discovery_configure_log,
                ),
            )
            phase_results["test_discovery_configure"] = run_command(
                command=configure_command,
                cwd=repo_root,
                env_overrides=lane_env,
                log_path=test_discovery_configure_log,
                lane_name=lane.name,
                phase_name="test-discovery-configure",
                heartbeat_seconds=args.heartbeat_seconds,
                status_detail=args.status_detail,
                append=resumed_lane and test_discovery_configure_log.exists(),
            )
        else:
            phase_results["test_discovery_configure"] = {
                "skipped": True,
                "reason": "build_failed",
            }

        if phase_results["test_discovery_configure"].get("ok"):
            write_status(
                status_path,
                run_id=run_id,
                run_started_at=isoformat_utc(run_started),
                lane_results=lane_results,
                in_progress_lane=lane.name,
                output_dir=run_dir,
                parallelism=parallelism,
                active_phase=make_active_phase(
                    lane_name=lane.name,
                    phase_name="discovery",
                    command=discovery_command,
                    log_path=lane_dir / "ctest-discovery.json",
                ),
            )
            phase_results["discovery"] = try_collect_test_discovery(
                lane_name=lane.name,
                build_dir=build_dir_path,
                lane_dir=lane_dir,
                env_overrides=lane_env,
            )
            write_status(
                status_path,
                run_id=run_id,
                run_started_at=isoformat_utc(run_started),
                lane_results=lane_results,
                in_progress_lane=lane.name,
                output_dir=run_dir,
                parallelism=parallelism,
                active_phase=make_active_phase(
                    lane_name=lane.name,
                    phase_name="test",
                    command=test_command,
                    log_path=ctest_output_log,
                ),
            )
            phase_results["test"] = run_command(
                command=test_command,
                cwd=repo_root,
                env_overrides=lane_test_env,
                log_path=ctest_log,
                lane_name=lane.name,
                phase_name="test",
                heartbeat_seconds=args.heartbeat_seconds,
                status_detail=args.status_detail,
                append=resumed_lane and ctest_log.exists(),
            )
        else:
            test_skip_reason = (
                "build_failed"
                if not phase_results["build"].get("ok")
                else "test_discovery_configure_failed"
            )
            phase_results["discovery"] = {
                "skipped": True,
                "reason": test_skip_reason,
            }
            phase_results["test"] = {
                "skipped": True,
                "reason": test_skip_reason,
            }

        log_paths = [
            configure_log,
            test_discovery_configure_log,
            build_log,
            ctest_log,
            ctest_output_log,
        ]
        findings = collect_findings(
            log_paths=log_paths,
            max_sample_findings=effective_max_sample_findings,
        )
        runtime_report_files = list_runtime_report_files(runtime_report_dir)
        runtime_findings = collect_findings(
            log_paths=runtime_report_files,
            max_sample_findings=effective_max_sample_findings,
        )
        combined_findings = merge_findings_summaries(
            [findings, runtime_findings],
            max_sample_findings=effective_max_sample_findings,
        )
        failed_tests = merge_failed_ctest_tests(
            [
                parse_failed_ctest_tests(ctest_output_log),
                parse_failed_ctest_tests(ctest_log),
            ]
        )
        junit_summary = parse_junit(ctest_junit)
        ctest_evidence = audit_ctest_evidence(
            test_phase_result=phase_results["test"],
            discovery_result=phase_results["discovery"],
            ctest_command_log=ctest_log,
            ctest_output_log=ctest_output_log,
            ctest_junit=ctest_junit,
            junit_summary=junit_summary,
            failed_tests=failed_tests,
        )
        failure_reasons = build_lane_failure_reasons(
            phase_results=phase_results,
            ctest_evidence=ctest_evidence,
            failed_tests=failed_tests,
            combined_findings=combined_findings,
        )

        lane_ok = bool(
            phase_results["configure"].get("ok")
            and phase_results["build"].get("ok")
            and phase_results["test_discovery_configure"].get("ok")
            and phase_results["test"].get("ok")
            and ctest_evidence["ok"]
            and combined_findings["total_matches"] == 0
        )

        lane_artifacts: LaneArtifacts = {
            "configure_log": str(configure_log),
            "test_discovery_configure_log": str(test_discovery_configure_log),
            "build_log": str(build_log),
            "ctest_command_log": str(ctest_log),
            "ctest_output_log": str(ctest_output_log),
            "ctest_junit": str(ctest_junit),
            "runtime_report_dir": runtime_report_artifacts["runtime_report_dir"],
            "runtime_report_base": runtime_report_artifacts["runtime_report_base"],
            "runtime_report_glob": runtime_report_artifacts["runtime_report_glob"],
            "runtime_report_files": [str(path) for path in runtime_report_files],
            "unified_findings_report_json": "",
            "unified_findings_report_text": "",
        }
        runtime_tooling: RuntimeTooling = {
            "symbolizer_binary": (
                str(symbolizer_binary) if symbolizer_binary is not None else None
            ),
            "symbolizer_wrapper": (
                str(symbolizer_wrapper) if symbolizer_wrapper is not None else None
            ),
        }
        lane_result: LaneResult = {
            "lane": lane.name,
            "configure_preset": lane.configure_preset,
            "build_preset": lane.build_preset,
            "build_dir": lane.build_dir,
            "env": lane.env,
            "runtime_tooling": runtime_tooling,
            "parallelism": parallelism,
            "phase_results": phase_results,
            "artifacts": lane_artifacts,
            "junit_summary": junit_summary,
            "ctest_evidence": ctest_evidence,
            "failed_tests": failed_tests,
            "failed_tests_total": len(failed_tests),
            "findings": findings,
            "runtime_findings": runtime_findings,
            "combined_findings": combined_findings,
            "failure_reasons": failure_reasons,
            "ok": lane_ok,
        }

        lane_findings_view: LaneFindingsView = {
            "lane": lane.name,
            "findings": findings,
            "runtime_findings": runtime_findings,
        }
        lane_report_artifacts = write_default_unified_findings_reports(
            report_stem=lane_dir / "unified_findings_report",
            run_id=run_id,
            run_dir=run_dir,
            lane_findings=[lane_findings_view],
            max_sample_findings=effective_max_sample_findings,
        )
        lane_artifacts["unified_findings_report_json"] = lane_report_artifacts.json_path
        lane_artifacts["unified_findings_report_text"] = lane_report_artifacts.text_path

        lane_results.append(lane_result)
        completed_lane_names.add(lane.name)
        emit_status(
            f"lane {lane.name}: {'ok' if lane_ok else 'failed'} with findings={combined_findings['total_matches']}"
        )
        if failure_reasons:
            reason_messages = "; ".join(
                str(reason.get("message", reason)) for reason in failure_reasons[:5]
            )
            if len(failure_reasons) > 5:
                reason_messages += "; ..."
            emit_status(f"lane {lane.name}: failure_reasons={reason_messages}")
        if failed_tests:
            emit_status(
                f"lane {lane.name}: failed_tests={len(failed_tests)} [{summarize_failed_test_names(failed_tests)}]"
            )
        if ctest_evidence["issues"]:
            emit_status(
                f"lane {lane.name}: ctest_evidence_issues={ctest_evidence['issues']}"
            )
        if ctest_evidence["warnings"]:
            emit_status(
                f"lane {lane.name}: ctest_evidence_warnings={ctest_evidence['warnings']}"
            )
        emit_status(
            f"lane {lane.name}: findings_by_severity={format_counts(combined_findings['counts_by_severity'])}"
        )
        emit_status(
            f"lane {lane.name}: findings_by_category={format_counts(combined_findings['counts_by_category'])}"
        )
        if combined_findings["sample_truncated"]:
            emit_status(
                f"lane {lane.name}: findings samples truncated at limit={combined_findings['sample_limit']}"
            )
        write_json(lane_dir / "lane_summary.json", lane_result)
        write_status(
            status_path,
            run_id=run_id,
            run_started_at=isoformat_utc(run_started),
            lane_results=lane_results,
            in_progress_lane=None,
            output_dir=run_dir,
            parallelism=parallelism,
            active_phase=None,
        )

        if should_clean_build_dir_after_lane(effective_disk_space_mode):
            cleanup_build_dir(build_dir_path, f"lane {lane.name} completion")

        if args.stop_on_failure and not lane_ok:
            break

    run_ended = utc_now()
    overall = {
        "schema_version": 1,
        "run_id": run_id,
        "started_at": isoformat_utc(run_started),
        "ended_at": isoformat_utc(run_ended),
        "duration_seconds": round((run_ended - run_started).total_seconds(), 3),
        "repo_root": str(repo_root),
        "output_dir": str(run_dir),
        "jobs": args.jobs,
        "parallelism": parallelism,
        "disk_space_mode": effective_disk_space_mode,
        "requested_disk_space_mode": args.disk_space_mode,
        "lane_order": requested_lane_names,
        "lane_results": lane_results,
        "overall_counts": compute_overall_counts(lane_results),
    }

    summary_path = run_dir / "summary.json"
    run_findings = lane_findings_from_results(lane_results)
    run_report_artifacts = write_default_unified_findings_reports(
        report_stem=run_dir / "unified_findings_report",
        run_id=run_id,
        run_dir=run_dir,
        lane_findings=run_findings,
        max_sample_findings=effective_max_sample_findings,
    )
    overall["artifacts"] = {
        "summary_json": str(summary_path),
        "unified_findings_report_json": run_report_artifacts.json_path,
        "unified_findings_report_text": run_report_artifacts.text_path,
    }
    write_json(summary_path, overall)
    emit_status(
        "matrix finished "
        f"lanes_total={overall['overall_counts']['lanes_total']} "
        f"lanes_failed={overall['overall_counts']['lanes_failed']} "
        f"findings_total={overall['overall_counts']['findings_total']}"
    )
    emit_status(
        "matrix ctest_evidence "
        f"issues={overall['overall_counts']['ctest_evidence_issues_total']} "
        f"warnings={overall['overall_counts']['ctest_evidence_warnings_total']}"
    )
    emit_status(
        f"matrix findings_by_severity={format_counts(overall['overall_counts']['findings_by_severity'])}"
    )
    write_status(
        status_path,
        run_id=run_id,
        run_started_at=isoformat_utc(run_started),
        lane_results=lane_results,
        in_progress_lane=None,
        output_dir=run_dir,
        parallelism=parallelism,
        active_phase=None,
    )

    if args.findings_report:
        findings_report_path = resolve_report_path(repo_root, args.findings_report)
        write_unified_findings_report(
            report_path=findings_report_path,
            report_format=args.findings_report_format,
            run_id=run_id,
            run_dir=run_dir,
            lane_findings=run_findings,
            max_sample_findings=effective_max_sample_findings,
        )

    print(str(run_dir))
    print(str(summary_path))
    print(str(run_report_artifacts.json_path))
    print(str(run_report_artifacts.text_path))

    return 0 if overall["overall_counts"]["lanes_failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
