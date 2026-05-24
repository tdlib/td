# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import collections
import dataclasses
import datetime as dt
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import typing as t

SANITIZER_FINDING_PATTERNS: tuple[dict[str, t.Any], ...] = (
    {
        "category": "asan_memory_error",
        "severity": "critical",
        "regex": r"ERROR: AddressSanitizer:",
        "risk": "Memory safety violation detected by AddressSanitizer",
        "principles": ["ASVS-V5", "ASVS-V7", "OWASP-A03", "OWASP-A05"],
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
        "category": "sanitizer_summary",
        "severity": "high",
        "regex": r"SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer)",
        "risk": "Sanitizer summary indicates one or more defects",
        "principles": ["ASVS-V7", "OWASP-A05"],
    },
)

MAX_SNIPPET_FINDINGS_PER_LOG = 250


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


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def isoformat_utc(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def as_json(data: t.Any) -> str:
    return json.dumps(data, indent=2, ensure_ascii=True, sort_keys=True)


def load_lane_map() -> dict[str, Lane]:
    return {lane.name: lane for lane in LANES}


def parse_args() -> argparse.Namespace:
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
        default=14,
        type=int,
        help="ctest parallel jobs for test execution.",
    )
    parser.add_argument(
        "--lanes",
        default=",".join(lane.name for lane in LANES),
        help="Comma-separated lane names. Available: asan, ubsan, tsan, asan-fast, ubsan-fast, tsan-fast.",
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
    return parser.parse_args()


def write_json(path: pathlib.Path, payload: dict[str, t.Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(as_json(payload) + "\n", encoding="utf-8")


def read_json(path: pathlib.Path) -> dict[str, t.Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_isoformat_utc(value: str) -> dt.datetime:
    return dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
        tzinfo=dt.timezone.utc
    )


def format_duration(seconds: float) -> str:
    seconds_int = max(0, int(seconds))
    hours, remainder = divmod(seconds_int, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes:02d}m {secs:02d}s"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


def tail_file_lines(path: pathlib.Path, line_count: int) -> list[str]:
    if line_count <= 0 or not path.exists():
        return []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        return [line.rstrip("\n") for line in collections.deque(handle, maxlen=line_count)]


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
    ).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines.append(f"Log: {log_path} ({stat_result.st_size} bytes, modified {modified_at})")
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


def render_monitor(run_dir: pathlib.Path, tail_lines_count: int) -> str:
    status = read_json(run_dir / "status.json")

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


def run_monitor(args: argparse.Namespace, output_root: pathlib.Path) -> int:
    run_dir = resolve_run_dir(output_root, args.run_dir)

    while True:
        snapshot = render_monitor(run_dir, args.tail_lines)
        if args.follow and sys.stdout.isatty():
            sys.stdout.write("\033[2J\033[H")
        sys.stdout.write(snapshot)
        sys.stdout.flush()

        if not args.follow:
            return 0

        status = read_json(run_dir / "status.json")
        if not status.get("in_progress_lane") and not status.get("active_phase"):
            return 0
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
) -> list[dict[str, t.Any]]:
    lane_results: list[dict[str, t.Any]] = []
    for lane_name in requested_lane_names:
        lane_summary_path = run_dir / lane_name / "lane_summary.json"
        if not lane_summary_path.exists():
            continue
        lane_results.append(read_json(lane_summary_path))
    return lane_results


def run_command(
    *,
    command: list[str],
    cwd: pathlib.Path,
    env_overrides: dict[str, str],
    log_path: pathlib.Path,
    append: bool = False,
) -> dict[str, t.Any]:
    started_at = utc_now()
    merged_env = os.environ.copy()
    merged_env.update(env_overrides)

    log_path.parent.mkdir(parents=True, exist_ok=True)
    open_mode = "a" if append else "w"
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
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            universal_newlines=True,
        )

        assert process.stdout is not None
        for line in process.stdout:
            log_file.write(line)
        return_code = process.wait()

    ended_at = utc_now()
    duration = ended_at - started_at
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
    build_dir: pathlib.Path,
    lane_dir: pathlib.Path,
    env_overrides: dict[str, str],
) -> dict[str, t.Any]:
    discovery_path = lane_dir / "ctest-discovery.json"
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--show-only=json-v1",
    ]
    started_at = utc_now()
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
    else:
        (lane_dir / "ctest-discovery-error.log").write_text(
            completed.stdout + "\n\n" + completed.stderr,
            encoding="utf-8",
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


def parse_junit(junit_path: pathlib.Path) -> dict[str, t.Any] | None:
    if not junit_path.exists():
        return None

    source = junit_path.read_text(encoding="utf-8", errors="replace")
    test_suite_tags = re.findall(r"<testsuite\b[^>]*>", source)
    if not test_suite_tags:
        return {
            "parse_error": True,
            "path": str(junit_path),
        }

    totals = {
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


def collect_findings(log_paths: list[pathlib.Path]) -> dict[str, t.Any]:
    compiled_patterns: list[tuple[dict[str, t.Any], re.Pattern[str]]] = [
        (pattern, re.compile(pattern["regex"]))
        for pattern in SANITIZER_FINDING_PATTERNS
    ]

    findings: list[dict[str, t.Any]] = []
    counts_by_category: dict[str, int] = {}
    counts_by_severity: dict[str, int] = {}

    for log_path in log_paths:
        if not log_path.exists():
            continue
        with log_path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_number, line in enumerate(handle, start=1):
                for pattern_info, compiled in compiled_patterns:
                    if not compiled.search(line):
                        continue

                    category = pattern_info["category"]
                    severity = pattern_info["severity"]
                    counts_by_category[category] = (
                        counts_by_category.get(category, 0) + 1
                    )
                    counts_by_severity[severity] = (
                        counts_by_severity.get(severity, 0) + 1
                    )

                    if len(findings) < MAX_SNIPPET_FINDINGS_PER_LOG:
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

    return {
        "counts_by_category": counts_by_category,
        "counts_by_severity": counts_by_severity,
        "total_matches": sum(counts_by_category.values()),
        "sample_findings": findings,
    }


def set_latest_pointer(output_root: pathlib.Path, run_dir: pathlib.Path) -> None:
    latest_file = output_root / "latest_run_path.txt"
    latest_file.write_text(str(run_dir) + "\n", encoding="utf-8")


def compute_overall_counts(lane_results: list[dict[str, t.Any]]) -> dict[str, t.Any]:
    lanes_total = len(lane_results)
    lanes_ok = sum(1 for lane in lane_results if lane.get("ok"))
    lanes_failed = lanes_total - lanes_ok

    total_findings = 0
    severity_counts: dict[str, int] = {}
    for lane in lane_results:
        findings = lane.get("findings", {})
        total_findings += int(findings.get("total_matches", 0))
        for severity, count in findings.get("counts_by_severity", {}).items():
            severity_counts[severity] = severity_counts.get(severity, 0) + int(count)

    return {
        "lanes_total": lanes_total,
        "lanes_ok": lanes_ok,
        "lanes_failed": lanes_failed,
        "findings_total": total_findings,
        "findings_by_severity": severity_counts,
    }


def write_status(
    status_path: pathlib.Path,
    *,
    run_id: str,
    run_started_at: str,
    lane_results: list[dict[str, t.Any]],
    in_progress_lane: str | None,
    output_dir: pathlib.Path,
    parallelism: dict[str, t.Any],
    active_phase: dict[str, t.Any] | None,
) -> None:
    payload = {
        "run_id": run_id,
        "run_started_at": run_started_at,
        "updated_at": isoformat_utc(utc_now()),
        "in_progress_lane": in_progress_lane,
        "completed_lanes": [lane["lane"] for lane in lane_results],
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
        return run_monitor(args, output_root)

    lane_map = load_lane_map()
    requested_lane_names = [
        name.strip() for name in args.lanes.split(",") if name.strip()
    ]
    unknown_lanes = [name for name in requested_lane_names if name not in lane_map]
    if unknown_lanes:
        print(f"Unknown lane names: {', '.join(unknown_lanes)}", file=sys.stderr)
        return 2

    selected_lanes = [lane_map[name] for name in requested_lane_names]

    parallelism = {
        "build_jobs": args.jobs,
        "test_jobs": args.jobs,
        "test_parallelism_note": (
            "CTest discovery currently exposes a single umbrella test, run_all_tests, so test execution may not saturate all CPU cores even with -j set to the full core count."
        ),
    }

    if args.resume_run_dir:
        run_dir = pathlib.Path(args.resume_run_dir).resolve()
        status_path = run_dir / "status.json"
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
        status_path = run_dir / "status.json"
        lane_results = []

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
            continue

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

        configure_log = lane_dir / "configure.log"
        build_log = lane_dir / "build.log"
        ctest_log = lane_dir / "ctest-command.log"
        ctest_output_log = lane_dir / "ctest-output.log"
        ctest_junit = lane_dir / "ctest-junit.xml"

        phase_results: dict[str, dict[str, t.Any]] = {}
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
            str(repo_root / lane.build_dir),
            "--show-only=json-v1",
        ]
        test_command = [
            "ctest",
            "--test-dir",
            str(repo_root / lane.build_dir),
            "--output-on-failure",
            "-j",
            str(args.jobs),
            "--output-junit",
            str(ctest_junit),
            "--output-log",
            str(ctest_output_log),
        ]

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
                env_overrides=lane.env,
                log_path=configure_log,
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
                env_overrides=lane.env,
                log_path=build_log,
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
                    phase_name="discovery",
                    command=discovery_command,
                    log_path=lane_dir / "ctest-discovery.json",
                ),
            )
            phase_results["discovery"] = try_collect_test_discovery(
                build_dir=repo_root / lane.build_dir,
                lane_dir=lane_dir,
                env_overrides=lane.env,
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
                env_overrides=lane.env,
                log_path=ctest_log,
                append=resumed_lane and ctest_log.exists(),
            )
        else:
            phase_results["discovery"] = {
                "skipped": True,
                "reason": "build_failed",
            }
            phase_results["test"] = {
                "skipped": True,
                "reason": "build_failed",
            }

        log_paths = [configure_log, build_log, ctest_log, ctest_output_log]
        findings = collect_findings(log_paths)

        lane_ok = bool(
            phase_results["configure"].get("ok")
            and phase_results["build"].get("ok")
            and phase_results["test"].get("ok")
            and findings["total_matches"] == 0
        )

        lane_result = {
            "lane": lane.name,
            "configure_preset": lane.configure_preset,
            "build_preset": lane.build_preset,
            "build_dir": lane.build_dir,
            "env": lane.env,
            "parallelism": parallelism,
            "phase_results": phase_results,
            "artifacts": {
                "configure_log": str(configure_log),
                "build_log": str(build_log),
                "ctest_command_log": str(ctest_log),
                "ctest_output_log": str(ctest_output_log),
                "ctest_junit": str(ctest_junit),
            },
            "junit_summary": parse_junit(ctest_junit),
            "findings": findings,
            "ok": lane_ok,
        }

        lane_results.append(lane_result)
        completed_lane_names.add(lane.name)
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
        "lane_order": requested_lane_names,
        "lane_results": lane_results,
        "overall_counts": compute_overall_counts(lane_results),
    }

    summary_path = run_dir / "summary.json"
    write_json(summary_path, overall)
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

    print(str(run_dir))
    print(str(summary_path))

    return 0 if overall["overall_counts"]["lanes_failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
