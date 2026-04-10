#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import pathlib
import subprocess
from typing import Any


PARSER_VERSION = "tls-record-size-parser-v1"
DEFAULT_DISPLAY_FILTER = "tcp && tls.record.content_type == 23"
DEFAULT_SMALL_RECORD_THRESHOLD = 200
MAX_TLS_RECORD_SIZE = 16640
KNOWN_TLS_SERVER_PORTS = frozenset({443, 8443, 9443})


def run_command(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(argv)}\n{result.stderr.strip()}")
    return result.stdout


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def tshark_version() -> str:
    return run_command(["tshark", "-v"]).splitlines()[0].strip()


def _parse_record_sizes(raw_value: str, line: str) -> list[int]:
    sizes: list[int] = []
    for part in raw_value.split(","):
        value = part.strip()
        if not value:
            continue
        tls_record_size = int(value)
        if tls_record_size <= 0:
            raise ValueError(f"tls record size must be positive: {line}")
        if tls_record_size > MAX_TLS_RECORD_SIZE:
            raise ValueError(f"tls record size exceeds TLS maximum: {line}")
        sizes.append(tls_record_size)
    if not sizes:
        raise ValueError(f"missing tls record size: {line}")
    return sizes


def _parse_row(line: str) -> tuple[int, float, tuple[str, int], tuple[str, int], list[int], int | None]:
    parts = line.split("|")
    if len(parts) not in (7, 8):
        raise ValueError(f"unexpected tshark row format: {line}")
    frame_number = int(parts[0])
    timestamp = float(parts[1])
    src = (parts[2], int(parts[4]))
    dst = (parts[3], int(parts[5]))
    if len(parts) == 8:
        tls_record_sizes = _parse_record_sizes(parts[6], line)
        tcp_stream = int(parts[7])
    else:
        tls_record_sizes = _parse_record_sizes(parts[6], line)
        tcp_stream = None
    return frame_number, timestamp, src, dst, tls_record_sizes, tcp_stream


def _connection_key(src: tuple[str, int], dst: tuple[str, int]) -> tuple[tuple[str, int], tuple[str, int]]:
    if src <= dst:
        return src, dst
    return dst, src


def _infer_client_server_endpoints(
    src: tuple[str, int], dst: tuple[str, int]
) -> tuple[tuple[str, int], tuple[str, int]]:
    src_port = src[1]
    dst_port = dst[1]

    if src_port in KNOWN_TLS_SERVER_PORTS and dst_port not in KNOWN_TLS_SERVER_PORTS:
        return dst, src
    if dst_port in KNOWN_TLS_SERVER_PORTS and src_port not in KNOWN_TLS_SERVER_PORTS:
        return src, dst

    if src_port <= 1024 < dst_port:
        return dst, src
    if dst_port <= 1024 < src_port:
        return src, dst

    if src_port < 32768 <= dst_port:
        return dst, src
    if dst_port < 32768 <= src_port:
        return src, dst

    return src, dst


def _percentiles(values: list[int]) -> dict[str, int]:
    if not values:
        return {"p5": 0, "p25": 0, "p50": 0, "p75": 0, "p95": 0}
    sorted_values = sorted(values)

    def percentile(ratio: float) -> int:
        index = int(math.ceil(ratio * len(sorted_values))) - 1
        index = max(0, min(index, len(sorted_values) - 1))
        return int(sorted_values[index])

    return {
        "p5": percentile(0.05),
        "p25": percentile(0.25),
        "p50": percentile(0.50),
        "p75": percentile(0.75),
        "p95": percentile(0.95),
    }


def parse_tshark_tls_record_rows(output: str) -> list[dict[str, Any]]:
    connections: dict[tuple[tuple[str, int], tuple[str, int]], dict[str, Any]] = {}
    order: list[tuple[tuple[str, int], tuple[str, int]] | tuple[tuple[tuple[str, int], tuple[str, int]], int]] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        frame_number, timestamp, src, dst, tls_record_sizes, tcp_stream = _parse_row(line)
        base_key = _connection_key(src, dst)
        key = (base_key, tcp_stream) if tcp_stream is not None else base_key
        if key not in connections:
            client_endpoint, server_endpoint = _infer_client_server_endpoints(src, dst)
            connections[key] = {
                "client_endpoint": client_endpoint,
                "server_endpoint": server_endpoint,
                "tcp_stream_id": tcp_stream,
                "first_timestamp": timestamp,
                "last_timestamp": timestamp,
                "records": [],
                "frame_numbers": [],
            }
            order.append(key)
        connection = connections[key]
        if timestamp < connection["last_timestamp"]:
            raise ValueError(f"timestamps must be non-decreasing within a connection: {line}")
        direction = "c2s" if src == connection["client_endpoint"] else "s2c"
        relative_time_us = int(round((timestamp - connection["first_timestamp"]) * 1_000_000.0))
        for tls_record_size in tls_record_sizes:
            connection["records"].append(
                {
                    "seq": len(connection["records"]),
                    "direction": direction,
                    "tls_record_size": tls_record_size,
                    "relative_time_us": relative_time_us,
                    "frame_number": frame_number,
                }
            )
        connection["frame_numbers"].append(frame_number)
        connection["last_timestamp"] = timestamp

    parsed_connections: list[dict[str, Any]] = []
    for key in order:
        connection = connections[key]
        duration_ms = int(round((connection["last_timestamp"] - connection["first_timestamp"]) * 1000.0))
        parsed_connections.append(
            {
                "client_endpoint": list(connection["client_endpoint"]),
                "server_endpoint": list(connection["server_endpoint"]),
                "tcp_stream_id": connection["tcp_stream_id"],
                "server_name": "",
                "tls_version": "",
                "alpn_negotiated": "",
                "duration_ms": max(0, duration_ms),
                "records": connection["records"],
            }
        )
    return parsed_connections


def build_record_size_artifact(
    *,
    source_pcap: str,
    platform: str,
    browser_family: str,
    tshark_output: str,
    capture_date: str,
    tshark_version: str,
    display_filter: str = DEFAULT_DISPLAY_FILTER,
    small_record_threshold: int = DEFAULT_SMALL_RECORD_THRESHOLD,
) -> dict[str, Any]:
    connections = parse_tshark_tls_record_rows(tshark_output)
    all_records = [record for connection in connections for record in connection["records"]]
    c2s_sizes = [record["tls_record_size"] for record in all_records if record["direction"] == "c2s"]
    s2c_sizes = [record["tls_record_size"] for record in all_records if record["direction"] == "s2c"]
    total_records = len(all_records)
    small_record_count = sum(1 for record in all_records if record["tls_record_size"] < small_record_threshold)
    first_flight_c2s_sizes = [
        [record["tls_record_size"] for record in connection["records"] if record["direction"] == "c2s"][:5]
        for connection in connections
    ]
    return {
        "source_pcap": source_pcap,
        "platform": platform,
        "browser_family": browser_family,
        "extraction_date": capture_date,
        "parser_version": PARSER_VERSION,
        "extractor": {
            "tshark_version": tshark_version,
            "display_filter": display_filter,
        },
        "connections": connections,
        "aggregate_stats": {
            "total_connections": len(connections),
            "total_records": total_records,
            "c2s_size_percentiles": _percentiles(c2s_sizes),
            "s2c_size_percentiles": _percentiles(s2c_sizes),
            "small_record_fraction": (float(small_record_count) / float(total_records)) if total_records else 0.0,
            "first_flight_c2s_sizes": first_flight_c2s_sizes,
        },
    }


def extract_record_size_artifact(
    pcap_path: pathlib.Path,
    platform: str,
    browser_family: str,
    *,
    display_filter: str = DEFAULT_DISPLAY_FILTER,
) -> dict[str, Any]:
    tshark_output = run_command(
        [
            "tshark",
            "-r",
            str(pcap_path),
            "-Y",
            display_filter,
            "-T",
            "fields",
            "-E",
            "separator=|",
            "-e",
            "frame.number",
            "-e",
            "frame.time_relative",
            "-e",
            "ip.src",
            "-e",
            "ip.dst",
            "-e",
            "tcp.srcport",
            "-e",
            "tcp.dstport",
            "-e",
            "tls.record.length",
            "-e",
            "tcp.stream",
        ]
    )
    return build_record_size_artifact(
        source_pcap=pcap_path.name,
        platform=platform,
        browser_family=browser_family,
        tshark_output=tshark_output,
        capture_date=utc_now(),
        tshark_version=tshark_version(),
        display_filter=display_filter,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract TLS Application Data record size artifacts using tshark.")
    parser.add_argument("--pcap", required=True, help="Path to a pcap/pcapng file")
    parser.add_argument("--platform", required=True, help="Platform label, for example android or linux_desktop")
    parser.add_argument("--browser-family", required=True, help="Browser family label, for example chrome_146")
    parser.add_argument("--out", required=True, help="Output artifact JSON path")
    parser.add_argument("--display-filter", default=DEFAULT_DISPLAY_FILTER, help="tshark display filter")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifact = extract_record_size_artifact(
        pathlib.Path(args.pcap),
        args.platform,
        args.browser_family,
        display_filter=args.display_filter,
    )
    output_path = pathlib.Path(args.out)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())