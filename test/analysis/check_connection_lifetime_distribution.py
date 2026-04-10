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
import statistics
import sys
from typing import Any

from check_flow_behavior import DEFAULT_POLICY, check_flow_behavior
from common_smoke import append_failure_once, load_json_report


def _read_int(mapping: dict[str, Any], field: str) -> int | None:
    value = mapping.get(field)
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    return None


def _validate_percentiles(mapping: dict[str, Any], fields: tuple[str, ...], failure: str) -> list[str]:
    failures: list[str] = []
    values: list[int] = []
    for field in fields:
        value = _read_int(mapping, field)
        if value is None or value < 0:
            append_failure_once(failures, failure)
            return failures
        values.append(value)
    if values != sorted(values):
        append_failure_once(failures, failure)
    return failures


def validate_connection_lifetime_baseline(baseline: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    profile_family = baseline.get("profile_family")
    if not isinstance(profile_family, str) or not profile_family:
        append_failure_once(failures, "baseline-profile-family")

    aggregate_stats = baseline.get("aggregate_stats")
    if not isinstance(aggregate_stats, dict):
        append_failure_once(failures, "baseline-aggregate-stats")
        return failures

    lifetime_ms = aggregate_stats.get("lifetime_ms")
    idle_gap_ms = aggregate_stats.get("idle_gap_ms")
    replacement_overlap_ms = aggregate_stats.get("replacement_overlap_ms")
    if not isinstance(lifetime_ms, dict):
        append_failure_once(failures, "baseline-lifetime-percentiles")
    else:
        for failure in _validate_percentiles(lifetime_ms, ("p10", "p50", "p90", "p99"), "baseline-lifetime-percentiles"):
            append_failure_once(failures, failure)
    if not isinstance(idle_gap_ms, dict):
        append_failure_once(failures, "baseline-idle-gap-percentiles")
    else:
        for failure in _validate_percentiles(idle_gap_ms, ("p50", "p90", "p99"), "baseline-idle-gap-percentiles"):
            append_failure_once(failures, failure)
    if not isinstance(replacement_overlap_ms, dict):
        append_failure_once(failures, "baseline-replacement-overlap-percentiles")
    else:
        for failure in _validate_percentiles(
            replacement_overlap_ms,
            ("p50", "p95", "p99"),
            "baseline-replacement-overlap-percentiles",
        ):
            append_failure_once(failures, failure)

    max_parallel_per_destination = _read_int(aggregate_stats, "max_parallel_per_destination")
    if max_parallel_per_destination is None or not 1 <= max_parallel_per_destination <= 128:
        append_failure_once(failures, "baseline-max-parallel-per-destination")
    return failures


def _percentile(values: list[int], ratio: float) -> int:
    if not values:
        return 0
    sorted_values = sorted(values)
    index = int(math.ceil(ratio * len(sorted_values))) - 1
    index = max(0, min(index, len(sorted_values) - 1))
    return int(sorted_values[index])


def _validate_overlap_schema(report: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    connections = report.get("connections")
    if not isinstance(connections, list):
        return failures
    for entry in connections:
        if not isinstance(entry, dict):
            continue
        overlap_ms = entry.get("overlap_ms", 0)
        if isinstance(overlap_ms, bool) or not isinstance(overlap_ms, int) or overlap_ms < 0:
            append_failure_once(failures, "report-overlap-ms")
            break
    return failures


def _max_parallel_per_endpoint(connections: list[dict[str, Any]]) -> int:
    by_endpoint: dict[str, list[tuple[int, int]]] = {}
    for connection in connections:
        endpoint = str(connection["destination"]).partition("|")[0]
        by_endpoint.setdefault(endpoint, []).append((int(connection["started_at_ms"]), int(connection["ended_at_ms"])))

    best = 0
    for ranges in by_endpoint.values():
        events: list[tuple[int, int]] = []
        for started_at_ms, ended_at_ms in ranges:
            events.append((started_at_ms, 1))
            events.append((ended_at_ms, -1))
        active = 0
        for _, delta in sorted(events, key=lambda item: (item[0], item[1])):
            active += delta
            best = max(best, active)
    return best


def check_connection_lifetime_distribution(
    report: dict[str, Any],
    baseline: dict[str, Any],
    policy: dict[str, Any] | None = None,
) -> list[str]:
    effective_policy = dict(DEFAULT_POLICY)
    if policy:
        effective_policy.update(policy)

    failures = check_flow_behavior(report, effective_policy)
    for failure in validate_connection_lifetime_baseline(baseline):
        append_failure_once(failures, failure)
    for failure in _validate_overlap_schema(report):
        append_failure_once(failures, failure)
    if failures:
        return failures

    connections = report.get("connections")
    assert isinstance(connections, list)
    typed_connections = [entry for entry in connections if isinstance(entry, dict)]
    lifetimes = [int(entry["ended_at_ms"]) - int(entry["started_at_ms"]) for entry in typed_connections]
    overlaps = [int(entry.get("overlap_ms", 0)) for entry in typed_connections]

    aggregate_stats = baseline["aggregate_stats"]
    lifetime_ms = aggregate_stats["lifetime_ms"]
    replacement_overlap_ms = aggregate_stats["replacement_overlap_ms"]
    max_parallel_per_destination = int(aggregate_stats["max_parallel_per_destination"])

    if lifetimes and statistics.median(lifetimes) < int(lifetime_ms["p10"]):
        append_failure_once(failures, "baseline-median-lifetime")
    if lifetimes and _percentile(lifetimes, 0.90) > int(lifetime_ms["p99"]):
        append_failure_once(failures, "baseline-upper-lifetime")
    if overlaps and _percentile(overlaps, 0.95) > int(replacement_overlap_ms["p95"]):
        append_failure_once(failures, "replacement-overlap-spike")
    if _max_parallel_per_endpoint(typed_connections) > max_parallel_per_destination:
        append_failure_once(failures, "parallel-destination-cap")

    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate connection lifetime reports against a baseline artifact.")
    parser.add_argument("--artifact", required=True, help="Path to a JSON flow/lifecycle artifact")
    parser.add_argument("--baseline-json", required=True, help="Path to a JSON baseline artifact")
    parser.add_argument("--policy-json", help="Optional path to a JSON policy override")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = load_json_report(args.artifact)
    baseline = load_json_report(args.baseline_json)
    policy = None
    if args.policy_json:
        loaded_policy = load_json_report(args.policy_json)
        policy = loaded_policy if isinstance(loaded_policy, dict) else None

    failures = check_connection_lifetime_distribution(report, baseline, policy)
    output = {
        "ok": not failures,
        "failures": failures,
        "connection_count": len(report.get("connections", [])) if isinstance(report.get("connections"), list) else 0,
    }
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(output, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())