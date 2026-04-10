# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

from typing import Any


def _fraction(values: list[int], threshold: int) -> float:
    if not values:
        return 0.0
    return sum(1 for value in values if value < threshold) / float(len(values))


def _source_pcap_for_connection(report: dict[str, Any], connection: dict[str, Any]) -> str:
    source_pcap = connection.get("source_pcap")
    if isinstance(source_pcap, str) and source_pcap:
        return source_pcap
    fallback = report.get("source_pcap")
    if isinstance(fallback, str):
        return fallback
    return ""


def infer_platform_from_source_pcap(source_pcap: str) -> str:
    lowered = source_pcap.casefold()
    if any(token in lowered for token in ("android", "андроид", "oxygenos", "oneplus", "pixel", "s25", "samsung galaxy")):
        return "android"
    if any(token in lowered for token in ("ios", "iphone", "ipad")):
        return "ios"
    if any(token in lowered for token in ("macos", "macbook", "os x")):
        return "macos"
    if any(token in lowered for token in ("linux", "ubuntu", "debian", "fedora", "arch", "tdesktop")):
        return "linux"
    return "unknown"


def infer_browser_family_from_source_pcap(source_pcap: str) -> str:
    lowered = source_pcap.casefold()
    if "samsung internet" in lowered:
        return "samsung_internet"
    if "яндекс" in lowered or "yandex" in lowered:
        return "yandex"
    if "firefox" in lowered:
        return "firefox"
    if "safari" in lowered:
        return "safari"
    if "brave" in lowered:
        return "brave"
    if "chrome" in lowered:
        return "chrome"
    return "unknown"


def _group_name_for_connection(report: dict[str, Any], connection: dict[str, Any], grouping: str) -> str:
    if grouping == "source_pcap":
        source_pcap = _source_pcap_for_connection(report, connection)
        return source_pcap if source_pcap else "unknown"

    if grouping == "platform":
        platform = connection.get("platform")
        if isinstance(platform, str) and platform:
            return platform
        report_platform = report.get("platform")
        if isinstance(report_platform, str) and report_platform and report_platform not in ("mixed", "aggregate"):
            return report_platform
        return infer_platform_from_source_pcap(_source_pcap_for_connection(report, connection))

    if grouping == "browser_family":
        browser_family = connection.get("browser_family")
        if isinstance(browser_family, str) and browser_family:
            return browser_family
        report_browser_family = report.get("browser_family")
        if isinstance(report_browser_family, str) and report_browser_family and report_browser_family != "capture_corpus_v1":
            return report_browser_family
        return infer_browser_family_from_source_pcap(_source_pcap_for_connection(report, connection))

    raise ValueError(f"unsupported grouping: {grouping}")


def split_record_size_corpus(report: dict[str, Any], *, grouping: str) -> dict[str, dict[str, Any]]:
    connections = report.get("connections")
    if not isinstance(connections, list):
        raise ValueError("report must contain a connections list")

    grouped: dict[str, dict[str, Any]] = {}
    for connection in connections:
        if not isinstance(connection, dict):
            continue
        group_name = _group_name_for_connection(report, connection, grouping)
        entry = grouped.setdefault(group_name, {"connections": []})
        entry["connections"].append(connection)
    return grouped


def audit_split_record_size_corpus(
    report: dict[str, Any], *, grouping: str, small_record_threshold: int = 200, greeting_record_count: int = 5,
    budgets: dict[str, float] | None = None,
) -> dict[str, dict[str, Any]]:
    grouped_reports = split_record_size_corpus(report, grouping=grouping)
    audits: dict[str, dict[str, Any]] = {}
    for group_name, grouped_report in grouped_reports.items():
        metrics, findings = audit_record_size_corpus(
            grouped_report,
            small_record_threshold=small_record_threshold,
            greeting_record_count=greeting_record_count,
            budgets=budgets,
        )
        audits[group_name] = {"metrics": metrics, "findings": findings}
    return audits


def compute_record_size_corpus_metrics(
    report: dict[str, Any], *, small_record_threshold: int = 200, greeting_record_count: int = 5
) -> dict[str, float | int]:
    connections = report.get("connections")
    if not isinstance(connections, list):
        raise ValueError("report must contain a connections list")

    all_sizes: list[int] = []
    c2s_sizes: list[int] = []
    s2c_sizes: list[int] = []
    first_two_any: list[int] = []
    post_first_five_any: list[int] = []
    first_five_c2s: list[int] = []
    post_first_five_c2s: list[int] = []
    short_connections = 0
    empty_c2s_first_flights = 0

    for connection in connections:
        if not isinstance(connection, dict):
            continue
        records = connection.get("records")
        if not isinstance(records, list):
            continue
        if len(records) < greeting_record_count:
            short_connections += 1

        c2s_sequence: list[int] = []
        for index, record in enumerate(records):
            if not isinstance(record, dict):
                continue
            size = record.get("tls_record_size")
            direction = record.get("direction")
            if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
                continue

            all_sizes.append(size)
            if index < 2:
                first_two_any.append(size)
            if index >= greeting_record_count:
                post_first_five_any.append(size)

            if direction == "c2s":
                c2s_sizes.append(size)
                if len(c2s_sequence) < greeting_record_count:
                    c2s_sequence.append(size)
                    first_five_c2s.append(size)
                else:
                    post_first_five_c2s.append(size)
            elif direction == "s2c":
                s2c_sizes.append(size)

        if not c2s_sequence:
            empty_c2s_first_flights += 1

    connection_count = len(connections)
    return {
        "connection_count": connection_count,
        "short_connection_count": short_connections,
        "empty_c2s_first_flight_count": empty_c2s_first_flights,
        "overall_small_fraction": _fraction(all_sizes, small_record_threshold),
        "c2s_small_fraction": _fraction(c2s_sizes, small_record_threshold),
        "s2c_small_fraction": _fraction(s2c_sizes, small_record_threshold),
        "first_two_any_small_fraction": _fraction(first_two_any, small_record_threshold),
        "post_first_five_any_small_fraction": _fraction(post_first_five_any, small_record_threshold),
        "first_five_c2s_small_fraction": _fraction(first_five_c2s, small_record_threshold),
        "post_first_five_c2s_small_fraction": _fraction(post_first_five_c2s, small_record_threshold),
        "short_connection_fraction": (float(short_connections) / float(connection_count)) if connection_count else 0.0,
    }


def audit_record_size_corpus(
    report: dict[str, Any],
    *,
    small_record_threshold: int = 200,
    greeting_record_count: int = 5,
    budgets: dict[str, float] | None = None,
) -> tuple[dict[str, float | int], list[str]]:
    metrics = compute_record_size_corpus_metrics(
        report,
        small_record_threshold=small_record_threshold,
        greeting_record_count=greeting_record_count,
    )

    effective_budgets = {
        "overall_small_fraction_max": 0.10,
        "c2s_small_fraction_max": 0.15,
        "s2c_small_fraction_max": 0.10,
        "post_first_five_c2s_small_fraction_max": 0.20,
        "short_connection_fraction_max": 0.25,
        "empty_c2s_first_flight_fraction_max": 0.05,
    }
    if budgets is not None:
        effective_budgets.update(budgets)

    findings: list[str] = []
    if metrics["overall_small_fraction"] > effective_budgets["overall_small_fraction_max"]:
        findings.append("overall-small-record-budget")
    if metrics["c2s_small_fraction"] > effective_budgets["c2s_small_fraction_max"]:
        findings.append("c2s-small-record-budget")
    if metrics["s2c_small_fraction"] > effective_budgets["s2c_small_fraction_max"]:
        findings.append("s2c-small-record-budget")
    if metrics["post_first_five_c2s_small_fraction"] > effective_budgets["post_first_five_c2s_small_fraction_max"]:
        findings.append("post-greeting-c2s-small-record-budget")
    if metrics["short_connection_fraction"] > effective_budgets["short_connection_fraction_max"]:
        findings.append("short-connection-share")

    empty_c2s_fraction = 0.0
    connection_count = int(metrics["connection_count"])
    if connection_count > 0:
        empty_c2s_fraction = int(metrics["empty_c2s_first_flight_count"]) / float(connection_count)
    if empty_c2s_fraction > effective_budgets["empty_c2s_first_flight_fraction_max"]:
        findings.append("empty-c2s-first-flight-share")

    return metrics, findings