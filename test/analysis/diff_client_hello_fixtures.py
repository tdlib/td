#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import argparse
import json
import pathlib
import sys
from typing import Any


TOP_LEVEL_FIELDS = [
    "parser_version",
    "source_kind",
    "source_path",
    "source_sha256",
    "capture_date_utc",
    "scenario_id",
    "route_mode",
    "display_filter",
    "transport",
    "tls_handshake_type",
]

SAMPLE_FIELDS = [
    "frame_number",
    "tcp_stream",
    "sni",
    "cipher_suites",
    "non_grease_cipher_suites",
    "supported_groups",
    "non_grease_supported_groups",
    "extension_types",
    "non_grease_extensions_without_padding",
    "alpn_protocols",
    "compress_certificate_algorithms",
    "key_share_entries",
    "ech",
    "record_type",
    "record_version",
    "record_length",
    "handshake_type",
    "handshake_length",
    "legacy_version",
]


def load_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        return json.load(infile)


def format_value(value: Any) -> str:
    if isinstance(value, (dict, list)):
        return json.dumps(value, sort_keys=True)
    return str(value)


def sample_map(artifact: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {sample["fixture_id"]: sample for sample in artifact.get("samples", [])}


def report_line(lines: list[str], text: str) -> None:
    lines.append(text)


def compare_artifacts(old_artifact: dict[str, Any], new_artifact: dict[str, Any]) -> list[str]:
    lines: list[str] = []

    report_line(lines, "== Top-Level Provenance ==")
    top_level_changes = 0
    for field in TOP_LEVEL_FIELDS:
      old_value = old_artifact.get(field)
      new_value = new_artifact.get(field)
      if old_value != new_value:
          top_level_changes += 1
          report_line(lines, f"CHANGED {field}: {format_value(old_value)} -> {format_value(new_value)}")
    if top_level_changes == 0:
        report_line(lines, "No top-level provenance changes.")

    old_samples = sample_map(old_artifact)
    new_samples = sample_map(new_artifact)
    removed = sorted(set(old_samples) - set(new_samples))
    added = sorted(set(new_samples) - set(old_samples))
    common = sorted(set(old_samples) & set(new_samples))

    report_line(lines, "")
    report_line(lines, "== Fixture IDs ==")
    if not removed and not added:
        report_line(lines, "Fixture id set unchanged.")
    else:
        for fixture_id in removed:
            report_line(lines, f"REMOVED fixture_id: {fixture_id}")
        for fixture_id in added:
            report_line(lines, f"ADDED fixture_id: {fixture_id}")

    report_line(lines, "")
    report_line(lines, "== Sample Deltas ==")
    sample_change_count = 0
    for fixture_id in common:
        old_sample = old_samples[fixture_id]
        new_sample = new_samples[fixture_id]
        sample_lines: list[str] = []
        for field in SAMPLE_FIELDS:
            if old_sample.get(field) != new_sample.get(field):
                sample_lines.append(
                    f"  CHANGED {field}: {format_value(old_sample.get(field))} -> {format_value(new_sample.get(field))}"
                )
        if old_sample.get("tls_record_sha256") != new_sample.get("tls_record_sha256"):
            sample_lines.append(
                f"  CHANGED tls_record_sha256: {old_sample.get('tls_record_sha256')} -> {new_sample.get('tls_record_sha256')}"
            )
        if old_sample.get("client_hello_sha256") != new_sample.get("client_hello_sha256"):
            sample_lines.append(
                f"  CHANGED client_hello_sha256: {old_sample.get('client_hello_sha256')} -> {new_sample.get('client_hello_sha256')}"
            )
        if sample_lines:
            sample_change_count += 1
            report_line(lines, f"fixture_id={fixture_id}")
            lines.extend(sample_lines)
    if sample_change_count == 0:
        report_line(lines, "No sample payload changes.")

    report_line(lines, "")
    report_line(lines, "== Summary ==")
    report_line(
        lines,
        f"top_level_changes={top_level_changes} added={len(added)} removed={len(removed)} modified={sample_change_count}",
    )
    return lines


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Diff two extracted TLS ClientHello fixture artifacts and report provenance or wire-shape drift."
    )
    parser.add_argument("--old", required=True, help="Path to the previous JSON artifact")
    parser.add_argument("--new", required=True, help="Path to the new JSON artifact")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    old_artifact = load_json(pathlib.Path(args.old).resolve())
    new_artifact = load_json(pathlib.Path(args.new).resolve())
    lines = compare_artifacts(old_artifact, new_artifact)
    sys.stdout.write("\n".join(lines))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())