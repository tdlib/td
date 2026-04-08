#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
from collections import Counter
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


def _parse_record_sizes(report: dict[str, Any]) -> tuple[list[int], list[str]]:
    failures: list[str] = []
    sizes = report.get("record_payload_sizes")
    if not isinstance(sizes, list) or not sizes:
        return [], ["record-sizes-present"]
    parsed: list[int] = []
    for value in sizes:
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            append_failure_once(failures, "record-size-schema")
            continue
        parsed.append(value)
    if not parsed:
        append_failure_once(failures, "record-sizes-present")
    return parsed, failures


def _has_long_constant_run(sizes: list[int], threshold: int) -> bool:
    streak = 1
    for previous, current in zip(sizes, sizes[1:]):
        if current == previous:
            streak += 1
            if streak >= threshold:
                return True
        else:
            streak = 1
    return False


def _has_two_size_oscillation(sizes: list[int], minimum_length: int) -> bool:
    if len(sizes) < minimum_length:
        return False
    first = sizes[0]
    second = sizes[1]
    if first == second:
        return False
    for index, size in enumerate(sizes):
        expected = first if index % 2 == 0 else second
        if size != expected:
            return False
    return True


def check_drs(report: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    failures = validate_smoke_scenario(report)
    threshold = _read_float(policy, "histogram_distance_threshold")
    if threshold is None or threshold < 0.0:
        append_failure_once(failures, "policy-histogram-distance-threshold")
    sizes, size_failures = _parse_record_sizes(report)
    for failure in size_failures:
        append_failure_once(failures, failure)
    if failures:
        return failures

    dominant_mode, _ = Counter(sizes).most_common(1)[0]
    if dominant_mode == 2878:
        append_failure_once(failures, "dominant-mode-2878")

    if _has_long_constant_run(sizes, 10):
        append_failure_once(failures, "constant-size-run")

    if _has_two_size_oscillation(sizes, 12):
        append_failure_once(failures, "two-size-oscillation")

    long_flow_windows = report.get("long_flow_windows", [])
    if not isinstance(long_flow_windows, list):
        append_failure_once(failures, "long-flow-window-schema")
        return failures

    for entry in long_flow_windows:
        if not isinstance(entry, dict):
            append_failure_once(failures, "long-flow-window-schema")
            continue
        total_payload_bytes = entry.get("total_payload_bytes")
        max_record_payload_size = entry.get("max_record_payload_size")
        if isinstance(total_payload_bytes, bool) or not isinstance(total_payload_bytes, int):
            append_failure_once(failures, "long-flow-window-schema")
            continue
        if isinstance(max_record_payload_size, bool) or not isinstance(max_record_payload_size, int):
            append_failure_once(failures, "long-flow-window-schema")
            continue
        if total_payload_bytes >= 100000 and max_record_payload_size <= 8192:
            append_failure_once(failures, "missing-long-flow-growth")

    baseline_distance = _read_float(report, "baseline_distance")
    if baseline_distance is None or baseline_distance < 0.0:
        append_failure_once(failures, "baseline-distance-missing")
    elif baseline_distance > threshold:
        append_failure_once(failures, "histogram-distance")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate PR-9 DRS smoke artifacts.")
    parser.add_argument("--artifact", required=True, help="Path to a JSON DRS artifact")
    parser.add_argument(
        "--histogram-distance-threshold",
        type=float,
        default=0.15,
        help="Maximum allowed histogram distance to the HTTPS baseline",
    )
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = load_json_report(args.artifact)
    failures = check_drs(report, {"histogram_distance_threshold": args.histogram_distance_threshold})
    output = {
        "ok": not failures,
        "failures": failures,
        "record_count": len(report.get("record_payload_sizes", [])) if isinstance(report.get("record_payload_sizes"), list) else 0,
        "histogram_distance_threshold": args.histogram_distance_threshold,
    }
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(output, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())