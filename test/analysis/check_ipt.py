#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Any

from common_smoke import append_failure_once, load_json_report, validate_smoke_scenario


def _read_float(mapping: dict[str, Any], field: str) -> float | None:
    value = mapping.get(field)
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        parsed = float(value)
        if not math.isfinite(parsed):
            return None
        return parsed
    return None


def _validate_runs(report: dict[str, Any]) -> tuple[list[dict[str, Any]], list[str]]:
    failures: list[str] = []
    runs = report.get("runs")
    if not isinstance(runs, list) or not runs:
        return [], ["runs-present"]
    parsed: list[dict[str, Any]] = []
    for run in runs:
        if not isinstance(run, dict):
            append_failure_once(failures, "run-schema")
            continue
        p_value = _read_float(run, "lognormal_fit_p_value")
        keepalive_bypass_p99_ms = _read_float(run, "keepalive_bypass_p99_ms")
        baseline_distance = _read_float(run, "baseline_distance")
        active_intervals_ms = run.get("active_intervals_ms")
        if p_value is None or keepalive_bypass_p99_ms is None or baseline_distance is None:
            append_failure_once(failures, "run-schema")
            continue
        if p_value < 0.0 or keepalive_bypass_p99_ms < 0.0 or baseline_distance < 0.0:
            append_failure_once(failures, "run-schema")
            continue
        if not isinstance(active_intervals_ms, list):
            append_failure_once(failures, "run-schema")
            continue
        parsed_intervals: list[float] = []
        valid = True
        for interval in active_intervals_ms:
            if isinstance(interval, bool) or not isinstance(interval, (int, float)) or interval < 0:
                append_failure_once(failures, "run-schema")
                valid = False
                break
            parsed_intervals.append(float(interval))
        if not valid:
            continue
        parsed.append(
            {
                "lognormal_fit_p_value": p_value,
                "keepalive_bypass_p99_ms": keepalive_bypass_p99_ms,
                "baseline_distance": baseline_distance,
                "active_intervals_ms": parsed_intervals,
            }
        )
    if not parsed:
        append_failure_once(failures, "runs-present")
    return parsed, failures


def check_ipt(report: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    failures = validate_smoke_scenario(report)
    threshold = _read_float(policy, "baseline_distance_threshold")
    if threshold is None or threshold < 0.0:
        append_failure_once(failures, "policy-baseline-distance-threshold")
    runs, run_failures = _validate_runs(report)
    for failure in run_failures:
        append_failure_once(failures, failure)
    if failures:
        return failures

    unique_interval_shapes = {tuple(run["active_intervals_ms"]) for run in runs}
    if len(runs) >= 3 and len(unique_interval_shapes) < 2:
        append_failure_once(failures, "replayed-active-intervals")

    rejected_runs = 0
    for run in runs:
        if run["lognormal_fit_p_value"] < 0.05:
            rejected_runs += 1
        if run["keepalive_bypass_p99_ms"] > 10.0:
            append_failure_once(failures, "keepalive-bypass")
        if any(interval > 5000.0 for interval in run["active_intervals_ms"]):
            append_failure_once(failures, "detector-visible-stall")
        if run["baseline_distance"] > threshold:
            append_failure_once(failures, "baseline-distance")

    if rejected_runs >= 2 or rejected_runs > len(runs) // 2:
        append_failure_once(failures, "lognormal-fit")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate PR-9 IPT smoke artifacts.")
    parser.add_argument("--artifact", required=True, help="Path to a JSON IPT artifact")
    parser.add_argument(
        "--baseline-distance-threshold",
        type=float,
        default=0.10,
        help="Maximum allowed distance to the HTTPS baseline",
    )
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = load_json_report(args.artifact)
    failures = check_ipt(report, {"baseline_distance_threshold": args.baseline_distance_threshold})
    output = {
        "ok": not failures,
        "failures": failures,
        "run_count": len(report.get("runs", [])) if isinstance(report.get("runs"), list) else 0,
        "baseline_distance_threshold": args.baseline_distance_threshold,
    }
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(output, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())