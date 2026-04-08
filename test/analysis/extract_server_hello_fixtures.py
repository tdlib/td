#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import subprocess
from typing import Any

from common_tls import normalize_route_mode, read_sha256


PARSER_VERSION = "tls-serverhello-parser-v1"
DEFAULT_DISPLAY_FILTER = "tcp && tls.handshake.type == 2"


def run_command(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(argv)}\n{result.stderr.strip()}")
    return result.stdout


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def tshark_version() -> str:
    output = run_command(["tshark", "-v"])
    return output.splitlines()[0].strip()


def _parse_hex(value: str) -> str:
    return f"0x{int(value, 16):04X}"


def _parse_content_types(raw: str) -> list[int]:
    values = [value.strip() for value in raw.split(",") if value.strip()]
    if not values:
        raise ValueError("missing record content types")
    return [int(value) for value in values]


def _parse_extension_types(raw: str) -> list[str]:
    values = [value.strip() for value in raw.split(",") if value.strip()]
    return [f"0x{int(value):04X}" for value in values]


def parse_tshark_server_hello_rows(output: str, scenario_id: str, family: str) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("|")
        if len(parts) != 5:
            raise ValueError(f"unexpected tshark row format: {line}")
        frame_number = int(parts[0])
        record_layout_signature = _parse_content_types(parts[1])
        selected_version = _parse_hex(parts[2])
        cipher_suite = _parse_hex(parts[3])
        extensions = _parse_extension_types(parts[4])
        samples.append(
            {
                "fixture_id": f"{scenario_id}:frame{frame_number}",
                "fixture_family_id": family,
                "selected_version": selected_version,
                "cipher_suite": cipher_suite,
                "extensions": extensions,
                "record_layout_signature": record_layout_signature,
            }
        )
    if not samples:
        raise ValueError("no ServerHello samples found")
    return samples


def extract_server_hello_artifact(
    pcap_path: pathlib.Path,
    route_mode: str,
    scenario_id: str,
    family: str,
    source_kind: str,
    display_filter: str,
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
            "tls.record.content_type",
            "-e",
            "tls.handshake.extensions.supported_version",
            "-e",
            "tls.handshake.ciphersuite",
            "-e",
            "tls.handshake.extension.type",
        ]
    )
    return {
        "route_mode": normalize_route_mode(route_mode),
        "scenario_id": scenario_id,
        "source_path": str(pcap_path.resolve()),
        "source_sha256": read_sha256(pcap_path),
        "source_kind": source_kind,
        "parser_version": PARSER_VERSION,
        "transport": "tcp",
        "family": family,
        "capture_date_utc": utc_now(),
        "extractor": {
            "tshark_version": tshark_version(),
            "display_filter": display_filter,
        },
        "samples": parse_tshark_server_hello_rows(tshark_output, scenario_id, family),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract ServerHello fixtures from a capture using tshark.")
    parser.add_argument("--pcap", required=True, help="Path to a pcap/pcapng file")
    parser.add_argument("--route-mode", required=True, help="Route mode, for example non_ru_egress")
    parser.add_argument("--scenario", required=True, help="Scenario identifier used in fixture ids")
    parser.add_argument("--family", required=True, help="ServerHello family id")
    parser.add_argument("--out", required=True, help="Output artifact JSON path")
    parser.add_argument("--source-kind", default="browser_capture", help="Artifact source kind")
    parser.add_argument("--display-filter", default=DEFAULT_DISPLAY_FILTER, help="tshark display filter")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifact = extract_server_hello_artifact(
        pathlib.Path(args.pcap),
        args.route_mode,
        args.scenario,
        args.family,
        args.source_kind,
        args.display_filter,
    )
    output_path = pathlib.Path(args.out)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())