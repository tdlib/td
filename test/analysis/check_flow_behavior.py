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

from common_smoke import append_failure_once, load_json_report, validate_smoke_scenario


DEFAULT_POLICY = {
    "max_connects_per_10s_per_destination": 6,
    "min_reuse_ratio": 0.55,
    "min_conn_lifetime_ms": 1500,
    "max_conn_lifetime_ms": 180000,
    "max_destination_share": 0.70,
    "sticky_domain_rotation_window_sec": 900,
    "anti_churn_min_reconnect_interval_ms": 300,
}


def _read_int(mapping: dict[str, Any], field: str) -> int | None:
    value = mapping.get(field)
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    return None


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


def validate_flow_policy(policy: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    max_connects = _read_int(policy, "max_connects_per_10s_per_destination")
    min_reuse_ratio = _read_float(policy, "min_reuse_ratio")
    min_conn_lifetime_ms = _read_int(policy, "min_conn_lifetime_ms")
    max_conn_lifetime_ms = _read_int(policy, "max_conn_lifetime_ms")
    max_destination_share = _read_float(policy, "max_destination_share")
    sticky_domain_rotation_window_sec = _read_int(policy, "sticky_domain_rotation_window_sec")
    anti_churn_min_reconnect_interval_ms = _read_int(policy, "anti_churn_min_reconnect_interval_ms")

    if max_connects is None or not 1 <= max_connects <= 30:
        append_failure_once(failures, "policy-max-connects")
    if min_reuse_ratio is None or not 0.0 <= min_reuse_ratio <= 1.0:
        append_failure_once(failures, "policy-min-reuse-ratio")
    if min_conn_lifetime_ms is None or not 200 <= min_conn_lifetime_ms <= 600000:
        append_failure_once(failures, "policy-min-conn-lifetime")
    if max_conn_lifetime_ms is None or not 200 <= max_conn_lifetime_ms <= 3600000:
        append_failure_once(failures, "policy-max-conn-lifetime")
    elif min_conn_lifetime_ms is not None and max_conn_lifetime_ms < min_conn_lifetime_ms:
        append_failure_once(failures, "policy-max-conn-lifetime")
    if max_destination_share is None or not 0.0 < max_destination_share <= 1.0:
        append_failure_once(failures, "policy-max-destination-share")
    if sticky_domain_rotation_window_sec is None or not 60 <= sticky_domain_rotation_window_sec <= 86400:
        append_failure_once(failures, "policy-sticky-domain-rotation-window")
    if anti_churn_min_reconnect_interval_ms is None or not 50 <= anti_churn_min_reconnect_interval_ms <= 60000:
        append_failure_once(failures, "policy-anti-churn")
    return failures


def _validate_connections(report: dict[str, Any]) -> tuple[list[dict[str, Any]], list[str]]:
    failures: list[str] = []
    connections = report.get("connections")
    if not isinstance(connections, list) or not connections:
        return [], ["connections-present"]

    parsed: list[dict[str, Any]] = []
    for entry in connections:
        if not isinstance(entry, dict):
            append_failure_once(failures, "connection-schema")
            continue
        destination = entry.get("destination")
        started_at_ms = _read_int(entry, "started_at_ms")
        ended_at_ms = _read_int(entry, "ended_at_ms")
        bytes_sent = _read_int(entry, "bytes_sent")
        reused = entry.get("reused")
        if not isinstance(destination, str) or not destination:
            append_failure_once(failures, "connection-schema")
            continue
        endpoint, separator, domain = destination.partition("|")
        if separator != "|" or not endpoint or not domain:
            append_failure_once(failures, "connection-schema")
            continue
        if started_at_ms is None or ended_at_ms is None or ended_at_ms < started_at_ms:
            append_failure_once(failures, "connection-schema")
            continue
        if bytes_sent is None or bytes_sent < 0 or not isinstance(reused, bool):
            append_failure_once(failures, "connection-schema")
            continue
        parsed.append(
            {
                "destination": destination,
                "endpoint": endpoint,
                "domain": domain,
                "started_at_ms": started_at_ms,
                "ended_at_ms": ended_at_ms,
                "bytes_sent": bytes_sent,
                "reused": reused,
            }
        )
    if not parsed:
        append_failure_once(failures, "connections-present")
    parsed.sort(key=lambda item: item["started_at_ms"])
    return parsed, failures


def _max_connects_per_destination(connections: list[dict[str, Any]]) -> dict[str, int]:
    by_destination: dict[str, list[int]] = {}
    for connection in connections:
        by_destination.setdefault(connection["endpoint"], []).append(connection["started_at_ms"])

    maxima: dict[str, int] = {}
    for destination, starts in by_destination.items():
        left = 0
        best = 0
        for right, started_at_ms in enumerate(starts):
            while started_at_ms - starts[left] > 10000:
                left += 1
            best = max(best, right - left + 1)
        maxima[destination] = best
    return maxima


def _violates_destination_share(connections: list[dict[str, Any]], max_destination_share: float) -> bool:
    starts = [connection["started_at_ms"] for connection in connections]
    for anchor in starts:
        window_connections = [
            connection for connection in connections if 0 <= connection["started_at_ms"] - anchor <= 10000
        ]
        if len(window_connections) < 2:
            continue
        by_destination: dict[str, int] = {}
        for connection in window_connections:
            by_destination[connection["destination"]] = by_destination.get(connection["destination"], 0) + 1
        if len(by_destination) < 2:
            continue
        total = len(window_connections)
        for count in by_destination.values():
            if count / total > max_destination_share:
                return True
    return False


def _violates_sticky_domain_rotation(
    connections: list[dict[str, Any]], sticky_domain_rotation_window_sec: int, max_destination_share: float
) -> bool:
    destinations = {connection["destination"] for connection in connections}
    if len(destinations) < 2:
        return False

    window_ms = sticky_domain_rotation_window_sec * 1000
    starts = [connection["started_at_ms"] for connection in connections]
    for anchor in starts:
        window_connections = [
            connection for connection in connections if 0 <= connection["started_at_ms"] - anchor <= window_ms
        ]
        if len(window_connections) < 2:
            continue
        if max(connection["ended_at_ms"] for connection in window_connections) - anchor < window_ms:
            continue

        by_destination: dict[str, int] = {}
        for connection in window_connections:
            by_destination[connection["destination"]] = by_destination.get(connection["destination"], 0) + 1
        if len(by_destination) < 2:
            continue

        total = len(window_connections)
        for count in by_destination.values():
            if count / total > max_destination_share:
                return True
    return False


def _violates_anti_churn(connections: list[dict[str, Any]], anti_churn_min_reconnect_interval_ms: int) -> bool:
    by_destination: dict[str, list[int]] = {}
    for connection in connections:
        by_destination.setdefault(connection["destination"], []).append(connection["started_at_ms"])
    for starts in by_destination.values():
        for previous, current in zip(starts, starts[1:]):
            if current - previous < anti_churn_min_reconnect_interval_ms:
                return True
    return False


def check_flow_behavior(report: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    failures = validate_smoke_scenario(report)
    failures.extend(validate_flow_policy(policy))
    connections, connection_failures = _validate_connections(report)
    for failure in connection_failures:
        append_failure_once(failures, failure)
    if failures:
        return failures

    max_connects = int(policy["max_connects_per_10s_per_destination"])
    min_reuse_ratio = float(policy["min_reuse_ratio"])
    min_conn_lifetime_ms = int(policy["min_conn_lifetime_ms"])
    max_conn_lifetime_ms = int(policy["max_conn_lifetime_ms"])
    max_destination_share = float(policy["max_destination_share"])
    sticky_domain_rotation_window_sec = int(policy["sticky_domain_rotation_window_sec"])
    anti_churn_min_reconnect_interval_ms = int(policy["anti_churn_min_reconnect_interval_ms"])

    for count in _max_connects_per_destination(connections).values():
        if count > max_connects:
            append_failure_once(failures, "reconnect-storm")
            break

    reuse_ratio = sum(1 for connection in connections if connection["reused"]) / len(connections)
    if reuse_ratio < min_reuse_ratio:
        append_failure_once(failures, "reuse-ratio")

    lifetimes = [connection["ended_at_ms"] - connection["started_at_ms"] for connection in connections]
    if statistics.median(lifetimes) < min_conn_lifetime_ms:
        append_failure_once(failures, "median-connection-lifetime")

    if any(lifetime > max_conn_lifetime_ms and connection["bytes_sent"] > 0 for lifetime, connection in zip(lifetimes, connections)):
        append_failure_once(failures, "pinned-socket-anomaly")

    if _violates_destination_share(connections, max_destination_share):
        append_failure_once(failures, "destination-share")

    if _violates_sticky_domain_rotation(connections, sticky_domain_rotation_window_sec, max_destination_share):
        append_failure_once(failures, "sticky-domain-concentration")

    if _violates_anti_churn(connections, anti_churn_min_reconnect_interval_ms):
        append_failure_once(failures, "anti-churn")

    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate PR-9 flow-behavior smoke artifacts.")
    parser.add_argument("--artifact", required=True, help="Path to a JSON flow-behavior artifact")
    parser.add_argument("--policy-json", help="Optional path to a JSON policy override")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = load_json_report(args.artifact)
    policy = dict(DEFAULT_POLICY)
    if args.policy_json:
        policy.update(load_json_report(args.policy_json))
    failures = check_flow_behavior(report, policy)
    output = {
        "ok": not failures,
        "failures": failures,
        "connection_count": len(report.get("connections", [])) if isinstance(report.get("connections"), list) else 0,
        "policy": policy,
    }
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(output, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())