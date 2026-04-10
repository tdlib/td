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

from common_smoke import append_failure_once, load_json_report


def _read_int(mapping: dict[str, Any], field: str) -> int | None:
    value = mapping.get(field)
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def _read_float(mapping: dict[str, Any], field: str) -> float | None:
    value = mapping.get(field)
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        parsed = float(value)
        if math.isfinite(parsed):
            return parsed
    return None


def _read_number_list(mapping: dict[str, Any], field: str) -> list[int] | None:
    value = mapping.get(field)
    if not isinstance(value, list) or not value:
        return None
    parsed: list[int] = []
    for entry in value:
        if isinstance(entry, bool) or not isinstance(entry, int) or entry <= 0:
            return None
        parsed.append(entry)
    return parsed


def _read_ranges(mapping: dict[str, Any], field: str) -> list[tuple[int, int]] | None:
    value = mapping.get(field)
    if not isinstance(value, list) or not value:
        return None
    parsed: list[tuple[int, int]] = []
    for entry in value:
        if not isinstance(entry, (list, tuple)) or len(entry) != 2:
            return None
        lo, hi = entry
        if isinstance(lo, bool) or isinstance(hi, bool) or not isinstance(lo, int) or not isinstance(hi, int):
            return None
        if lo <= 0 or hi < lo:
            return None
        parsed.append((lo, hi))
    return parsed


def _normalized_record_report(report: dict[str, Any]) -> dict[str, Any]:
    if isinstance(report.get("record_payload_sizes"), list) and isinstance(report.get("first_flight_c2s_sizes"), list):
        return report

    connections = report.get("connections")
    if not isinstance(connections, list):
        return report

    record_payload_sizes: list[int] = []
    first_flight_c2s_sizes: list[list[int]] = []
    for connection in connections:
        if not isinstance(connection, dict):
            continue
        records = connection.get("records")
        if not isinstance(records, list):
            continue
        connection_first_flight: list[int] = []
        for record in records:
            if not isinstance(record, dict):
                continue
            size = record.get("tls_record_size")
            direction = record.get("direction")
            if isinstance(size, int) and not isinstance(size, bool) and size > 0:
                record_payload_sizes.append(size)
                if direction == "c2s" and len(connection_first_flight) < 5:
                    connection_first_flight.append(size)
        first_flight_c2s_sizes.append(connection_first_flight)

    normalized = dict(report)
    normalized["record_payload_sizes"] = record_payload_sizes
    normalized["first_flight_c2s_sizes"] = first_flight_c2s_sizes
    return normalized


def _normal_survival(z: float) -> float:
    return 0.5 * math.erfc(z / math.sqrt(2.0))


def _ks_pvalue(sample_a: list[int], sample_b: list[int]) -> float:
    sorted_a = sorted(sample_a)
    sorted_b = sorted(sample_b)
    n_a = len(sorted_a)
    n_b = len(sorted_b)
    index_a = 0
    index_b = 0
    d_stat = 0.0
    while index_a < n_a and index_b < n_b:
        current = min(sorted_a[index_a], sorted_b[index_b])
        while index_a < n_a and sorted_a[index_a] <= current:
            index_a += 1
        while index_b < n_b and sorted_b[index_b] <= current:
            index_b += 1
        d_stat = max(d_stat, abs((index_a / n_a) - (index_b / n_b)))
    effective_n = (n_a * n_b) / float(n_a + n_b)
    if effective_n <= 0.0:
        return 0.0
    root_n = math.sqrt(effective_n)
    lam = (root_n + 0.12 + (0.11 / root_n)) * d_stat
    series = 0.0
    for k in range(1, 6):
        term = math.exp(-2.0 * (k * k) * lam * lam)
        series += term if (k % 2) == 1 else -term
    return max(0.0, min(1.0, 2.0 * series))


def _chi_squared_pvalue(observed: list[int], reference: list[int], bin_width: int) -> float:
    if bin_width <= 0:
        return 0.0
    upper_bound = max(max(observed), max(reference))
    bin_count = max(1, (upper_bound // bin_width) + 1)
    observed_hist = [0] * bin_count
    reference_hist = [0] * bin_count
    for value in observed:
        observed_hist[min(bin_count - 1, value // bin_width)] += 1
    for value in reference:
        reference_hist[min(bin_count - 1, value // bin_width)] += 1

    total_observed = len(observed)
    total_reference = len(reference)
    chi_squared = 0.0
    contributing_bins = 0
    for observed_count, reference_count in zip(observed_hist, reference_hist):
        expected = ((reference_count + 1.0) / (total_reference + bin_count)) * total_observed
        if expected <= 0.0:
            continue
        contributing_bins += 1
        delta = observed_count - expected
        chi_squared += (delta * delta) / expected
    degrees_of_freedom = max(1, contributing_bins - 1)
    wilson_hilferty = ((chi_squared / degrees_of_freedom) ** (1.0 / 3.0) - (1.0 - (2.0 / (9.0 * degrees_of_freedom))))
    wilson_hilferty /= math.sqrt(2.0 / (9.0 * degrees_of_freedom))
    return max(0.0, min(1.0, _normal_survival(wilson_hilferty)))


def _bucket_quantization_ratio(
    sizes: list[int],
    *,
    boundary: int,
    overhead: int,
    tolerance: int,
) -> float:
    center = boundary + overhead
    center_lo = center - tolerance
    center_hi = center + tolerance
    neighbor_lo = center - (3 * tolerance)
    neighbor_hi = center + (3 * tolerance)
    center_count = sum(1 for value in sizes if center_lo <= value <= center_hi)
    neighbor_count = sum(
        1
        for value in sizes
        if neighbor_lo <= value <= neighbor_hi and not (center_lo <= value <= center_hi)
    )
    if center_count < 3:
        return 0.0
    center_width = (2 * tolerance) + 1
    neighbor_width = max(1, ((neighbor_hi - neighbor_lo) + 1) - center_width)
    expected = max(0.5, neighbor_count * (center_width / float(neighbor_width)))
    return center_count / expected


def _lag1_autocorrelation(sizes: list[int]) -> float:
    if len(sizes) < 24:
        return 0.0
    x = sizes[:-1]
    y = sizes[1:]
    mean_x = statistics.fmean(x)
    mean_y = statistics.fmean(y)
    cov = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y))
    var_x = sum((a - mean_x) ** 2 for a in x)
    var_y = sum((b - mean_y) ** 2 for b in y)
    if var_x <= 0.0 or var_y <= 0.0:
        return 1.0
    return cov / math.sqrt(var_x * var_y)


def _max_adjacent_ratio(sizes: list[int]) -> float:
    if len(sizes) < 2:
        return 1.0
    maximum = 1.0
    for left, right in zip(sizes, sizes[1:]):
        lo = min(left, right)
        hi = max(left, right)
        if lo <= 0:
            return math.inf
        maximum = max(maximum, hi / float(lo))
    return maximum


def check_record_size_distribution(report: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    report = _normalized_record_report(report)
    failures: list[str] = []
    record_sizes = _read_number_list(report, "record_payload_sizes")
    reference_sizes = _read_number_list(policy, "reference_sizes")
    greeting_ranges = _read_ranges(policy, "greeting_ranges")
    small_record_threshold = _read_int(policy, "small_record_threshold")
    small_record_max_fraction = _read_float(policy, "small_record_max_fraction")
    bulk_record_threshold = _read_int(policy, "bulk_record_threshold")
    bulk_record_min_fraction = _read_float(policy, "bulk_record_min_fraction")
    bucket_boundaries = _read_number_list(policy, "bucket_boundaries")
    bucket_overhead = _read_int(policy, "bucket_overhead")
    bucket_tolerance = _read_int(policy, "bucket_tolerance")
    bucket_ratio_threshold = _read_float(policy, "bucket_excess_ratio_threshold")
    max_lag1_autocorrelation_abs = _read_float(policy, "max_lag1_autocorrelation_abs")
    max_adjacent_size_ratio = _read_float(policy, "max_adjacent_size_ratio")
    ks_min_pvalue = _read_float(policy, "ks_min_pvalue")
    chi_squared_min_pvalue = _read_float(policy, "chi_squared_min_pvalue")
    bin_width = _read_int(policy, "bin_width")

    if record_sizes is None or reference_sizes is None or greeting_ranges is None:
        append_failure_once(failures, "schema")
        return failures
    if None in (
        small_record_threshold,
        small_record_max_fraction,
        bulk_record_threshold,
        bulk_record_min_fraction,
        bucket_boundaries,
        bucket_overhead,
        bucket_tolerance,
        bucket_ratio_threshold,
        max_lag1_autocorrelation_abs,
        max_adjacent_size_ratio,
        ks_min_pvalue,
        chi_squared_min_pvalue,
        bin_width,
    ):
        append_failure_once(failures, "policy")
        return failures

    ks_pvalue = _ks_pvalue(record_sizes, reference_sizes)
    if ks_pvalue < float(ks_min_pvalue):
        append_failure_once(failures, "ks-test")

    chi_squared_pvalue = _chi_squared_pvalue(record_sizes, reference_sizes, int(bin_width))
    if chi_squared_pvalue < float(chi_squared_min_pvalue):
        append_failure_once(failures, "chi-squared")

    small_record_fraction = sum(1 for value in record_sizes if value < int(small_record_threshold)) / float(len(record_sizes))
    if small_record_fraction > float(small_record_max_fraction):
        append_failure_once(failures, "small-record-frequency")

    bulk_record_fraction = sum(1 for value in record_sizes if value > int(bulk_record_threshold)) / float(len(record_sizes))
    if bulk_record_fraction < float(bulk_record_min_fraction):
        append_failure_once(failures, "large-record-presence")

    first_flights = report.get("first_flight_c2s_sizes")
    if not isinstance(first_flights, list) or not first_flights:
        append_failure_once(failures, "first-flight-template")
    else:
        valid_first_flight = True
        validated_sequence_count = 0
        for sequence in first_flights:
            if not isinstance(sequence, list):
                valid_first_flight = False
                break
            if len(sequence) < len(greeting_ranges):
                continue
            validated_sequence_count += 1
            for value, (lo, hi) in zip(sequence, greeting_ranges):
                if isinstance(value, bool) or not isinstance(value, int) or value < lo or value > hi:
                    valid_first_flight = False
                    break
            if not valid_first_flight:
                break
        if not valid_first_flight or validated_sequence_count == 0:
            append_failure_once(failures, "first-flight-template")

    bucket_quantized = False
    for boundary in bucket_boundaries:
        ratio = _bucket_quantization_ratio(
            record_sizes,
            boundary=boundary,
            overhead=int(bucket_overhead),
            tolerance=int(bucket_tolerance),
        )
        if ratio > float(bucket_ratio_threshold):
            bucket_quantized = True
            break
    if bucket_quantized:
        append_failure_once(failures, "bucket-quantization")

    lag1 = _lag1_autocorrelation(record_sizes)
    if abs(lag1) > float(max_lag1_autocorrelation_abs):
        append_failure_once(failures, "lag1-autocorrelation")

    if _max_adjacent_ratio(record_sizes) > float(max_adjacent_size_ratio):
        append_failure_once(failures, "phase-transition-smoothness")

    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate TLS record size distributions against a browser baseline.")
    parser.add_argument("--artifact", required=True, help="Path to a JSON record-size artifact")
    parser.add_argument("--reference", required=True, help="Path to a JSON reference policy")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifact = load_json_report(args.artifact)
    policy = load_json_report(args.reference)
    failures = check_record_size_distribution(artifact, policy)
    report = {
        "ok": not failures,
        "failures": failures,
        "record_count": len(artifact.get("record_payload_sizes", [])) if isinstance(artifact.get("record_payload_sizes"), list) else 0,
    }
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())