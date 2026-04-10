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
import re
import subprocess
from typing import Any


PARSER_VERSION = "connection-lifetime-baseline-v1"
DEFAULT_DISPLAY_FILTER = "tcp"
_COVER_FAMILY_TOKENS = frozenset({"http1", "http11", "http2", "websocket", "grpc", "sse"})


def run_command(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(argv)}\n{result.stderr.strip()}")
    return result.stdout


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def tshark_version() -> str:
    return run_command(["tshark", "-v"]).splitlines()[0].strip()


def _to_bool_flag(value: str) -> bool:
    normalized = value.strip()
    if normalized in ("", "0"):
        return False
    if normalized == "1":
        return True
    raise ValueError(f"unexpected TCP flag value: {value}")


def _parse_optional_record_sizes(raw_value: str, line: str) -> list[int]:
    value = raw_value.strip()
    if value in ("", "0"):
        return []

    sizes: list[int] = []
    for part in value.split(","):
        token = part.strip()
        if token in ("", "0"):
            continue
        size = int(token)
        if size < 0:
            raise ValueError(f"TLS record size must be non-negative: {line}")
        sizes.append(size)
    return sizes


def _parse_row(line: str) -> dict[str, Any]:
    parts = line.split("|")
    if len(parts) != 12:
        raise ValueError(f"unexpected tshark row format: {line}")

    tcp_len = int(parts[10])
    if tcp_len < 0:
        raise ValueError(f"TCP payload length must be non-negative: {line}")

    return {
        "frame_number": int(parts[0]),
        "timestamp_ms": int(round(float(parts[1]) * 1000.0)),
        "src": (parts[2], int(parts[4])),
        "dst": (parts[3], int(parts[5])),
        "syn": _to_bool_flag(parts[6]),
        "fin": _to_bool_flag(parts[7]),
        "rst": _to_bool_flag(parts[8]),
        "tls_record_sizes": _parse_optional_record_sizes(parts[9], line),
        "tcp_len": tcp_len,
        "tcp_stream_id": int(parts[11]),
    }


def _percentile(values: list[int], ratio: float) -> int:
    if not values:
        return 0
    sorted_values = sorted(values)
    index = int(math.ceil(ratio * len(sorted_values))) - 1
    index = max(0, min(index, len(sorted_values) - 1))
    return int(sorted_values[index])


def _profile_family_tokens(profile_family: str) -> set[str]:
    return {token for token in re.split(r"[^a-z0-9]+", profile_family.lower()) if token in _COVER_FAMILY_TOKENS}


def _validate_profile_family(profile_family: str) -> None:
    tokens = _profile_family_tokens(profile_family)
    if len(tokens) > 1:
        raise ValueError("profile family mixes incompatible cover families")


def _connection_key(row: dict[str, Any]) -> int:
    return int(row["tcp_stream_id"])


def _record_payload_event(connection: dict[str, Any], row: dict[str, Any]) -> None:
    if row["tcp_len"] <= 0 and not row["tls_record_sizes"]:
        return

    event_size = sum(row["tls_record_sizes"]) if row["tls_record_sizes"] else row["tcp_len"]
    direction = "c2s" if row["src"] == connection["client_endpoint"] else "s2c"
    connection["payload_events"].append(
        {
            "timestamp_ms": row["timestamp_ms"],
            "direction": direction,
            "size": event_size,
            "frame_number": row["frame_number"],
        }
    )
    if direction == "c2s":
        connection["bytes_sent"] += row["tcp_len"]
    else:
        connection["bytes_received"] += row["tcp_len"]


def parse_tshark_connection_lifetime_rows(output: str) -> list[dict[str, Any]]:
    rows = [_parse_row(raw_line.strip()) for raw_line in output.splitlines() if raw_line.strip()]
    if not rows:
        raise ValueError("connection lifetime corpus is empty")

    connections: dict[int, dict[str, Any]] = {}
    for row in rows:
        key = _connection_key(row)
        connection = connections.get(key)
        if connection is None:
            connection = {
                "tcp_stream_id": key,
                "opened_at_ms": None,
                "closed_at_ms": None,
                "close_reason": "",
                "client_endpoint": None,
                "server_endpoint": None,
                "payload_events": [],
                "bytes_sent": 0,
                "bytes_received": 0,
                "frame_numbers": [],
            }
            connections[key] = connection

        connection["frame_numbers"].append(row["frame_number"])
        if row["syn"] and connection["opened_at_ms"] is None:
            connection["opened_at_ms"] = row["timestamp_ms"]
            connection["client_endpoint"] = row["src"]
            connection["server_endpoint"] = row["dst"]

        if connection["client_endpoint"] is not None:
            _record_payload_event(connection, row)

        if row["rst"]:
            connection["closed_at_ms"] = row["timestamp_ms"]
            connection["close_reason"] = "rst"
        elif row["fin"] and connection["close_reason"] != "rst":
            connection["closed_at_ms"] = row["timestamp_ms"]
            connection["close_reason"] = "fin"

    parsed_connections: list[dict[str, Any]] = []
    for connection in sorted(connections.values(), key=lambda item: item["opened_at_ms"] if item["opened_at_ms"] is not None else -1):
        if connection["opened_at_ms"] is None:
            raise ValueError(f"missing TCP open marker for stream {connection['tcp_stream_id']}")
        if connection["closed_at_ms"] is None:
            raise ValueError(f"missing TCP close or timeout marker for stream {connection['tcp_stream_id']}")
        if connection["closed_at_ms"] < connection["opened_at_ms"]:
            raise ValueError(f"connection close precedes open for stream {connection['tcp_stream_id']}")
        if not connection["payload_events"]:
            continue

        first_payload_at_ms = connection["payload_events"][0]["timestamp_ms"]
        server_ip, server_port = connection["server_endpoint"]
        destination = f"{server_ip}:{server_port}|{server_ip}"
        parsed_connections.append(
            {
                "tcp_stream_id": connection["tcp_stream_id"],
                "destination": destination,
                "proxy_id": "",
                "role": "unknown",
                "started_at_ms": connection["opened_at_ms"],
                "first_payload_at_ms": first_payload_at_ms,
                "ended_at_ms": connection["closed_at_ms"],
                "bytes_sent": connection["bytes_sent"],
                "bytes_received": connection["bytes_received"],
                "reused": False,
                "rotation_reason": "",
                "successor_opened_at_ms": 0,
                "overlap_ms": 0,
                "over_age_exemption": "",
                "close_reason": connection["close_reason"],
                "payload_events": connection["payload_events"],
            }
        )

    if not parsed_connections:
        raise ValueError("connection lifetime corpus contains no payload-bearing TCP connections")
    return parsed_connections


def _assign_reuse_and_overlap(connections: list[dict[str, Any]]) -> tuple[list[int], int]:
    by_destination: dict[str, list[dict[str, Any]]] = {}
    for connection in connections:
        by_destination.setdefault(connection["destination"], []).append(connection)

    overlaps: list[int] = []
    max_parallel_per_destination = 1
    for destination_connections in by_destination.values():
        destination_connections.sort(key=lambda item: item["started_at_ms"])
        for index, connection in enumerate(destination_connections):
            connection["reused"] = index > 0
            if index + 1 >= len(destination_connections):
                continue
            successor = destination_connections[index + 1]
            if successor["started_at_ms"] < connection["ended_at_ms"]:
                overlap_ms = connection["ended_at_ms"] - successor["started_at_ms"]
                connection["successor_opened_at_ms"] = successor["started_at_ms"]
                connection["overlap_ms"] = overlap_ms
                overlaps.append(overlap_ms)

        events: list[tuple[int, int]] = []
        for connection in destination_connections:
            events.append((connection["started_at_ms"], 1))
            events.append((connection["ended_at_ms"], -1))
        active = 0
        for _, delta in sorted(events, key=lambda item: (item[0], item[1])):
            active += delta
            max_parallel_per_destination = max(max_parallel_per_destination, active)
    return overlaps, max_parallel_per_destination


def build_connection_lifetime_baseline_artifact(
    *,
    source_pcap: str,
    profile_family: str,
    tshark_output: str,
    capture_date: str,
    tshark_version: str,
    display_filter: str = DEFAULT_DISPLAY_FILTER,
) -> dict[str, Any]:
    _validate_profile_family(profile_family)
    connections = parse_tshark_connection_lifetime_rows(tshark_output)
    overlaps, max_parallel_per_destination = _assign_reuse_and_overlap(connections)

    lifetimes = [connection["ended_at_ms"] - connection["started_at_ms"] for connection in connections]
    idle_gaps: list[int] = []
    for connection in connections:
        payload_timestamps = [event["timestamp_ms"] for event in connection["payload_events"]]
        idle_gaps.extend(
            current - previous for previous, current in zip(payload_timestamps, payload_timestamps[1:]) if current >= previous
        )

    aggregate_connections: list[dict[str, Any]] = []
    for connection in connections:
        aggregate_connections.append(
            {
                key: value
                for key, value in connection.items()
                if key != "payload_events"
            }
        )

    return {
        "source_pcap": source_pcap,
        "profile_family": profile_family,
        "extraction_date": capture_date,
        "parser_version": PARSER_VERSION,
        "extractor": {
            "tshark_version": tshark_version,
            "display_filter": display_filter,
        },
        "connections": aggregate_connections,
        "aggregate_stats": {
            "total_connections": len(aggregate_connections),
            "lifetime_ms": {
                "p10": _percentile(lifetimes, 0.10),
                "p50": _percentile(lifetimes, 0.50),
                "p90": _percentile(lifetimes, 0.90),
                "p99": _percentile(lifetimes, 0.99),
            },
            "idle_gap_ms": {
                "p50": _percentile(idle_gaps, 0.50),
                "p90": _percentile(idle_gaps, 0.90),
                "p99": _percentile(idle_gaps, 0.99),
            },
            "replacement_overlap_ms": {
                "p50": _percentile(overlaps, 0.50),
                "p95": _percentile(overlaps, 0.95),
                "p99": _percentile(overlaps, 0.99),
            },
            "max_parallel_per_destination": max_parallel_per_destination,
        },
    }


def extract_connection_lifetime_baseline_artifact(
    pcap_path: pathlib.Path,
    profile_family: str,
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
            "tcp.flags.syn",
            "-e",
            "tcp.flags.fin",
            "-e",
            "tcp.flags.reset",
            "-e",
            "tls.record.length",
            "-e",
            "tcp.len",
            "-e",
            "tcp.stream",
        ]
    )
    return build_connection_lifetime_baseline_artifact(
        source_pcap=pcap_path.name,
        profile_family=profile_family,
        tshark_output=tshark_output,
        capture_date=utc_now(),
        tshark_version=tshark_version(),
        display_filter=display_filter,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract connection lifetime baseline artifacts using tshark.")
    parser.add_argument("--pcap", required=True, help="Path to a pcap/pcapng file")
    parser.add_argument("--profile-family", required=True, help="Cover-family label, for example desktop_http1_proxy_like")
    parser.add_argument("--out", required=True, help="Output artifact JSON path")
    parser.add_argument("--display-filter", default=DEFAULT_DISPLAY_FILTER, help="tshark display filter")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifact = extract_connection_lifetime_baseline_artifact(
        pathlib.Path(args.pcap),
        args.profile_family,
        display_filter=args.display_filter,
    )
    output_path = pathlib.Path(args.out)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())