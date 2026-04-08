#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any

from common_tls import ServerHello, load_server_hello_artifact


AUTHORITATIVE_SERVER_HELLO_SOURCE_KINDS = {"browser_capture", "pcap"}


def _parse_u16(value: Any) -> int:
    if isinstance(value, str):
        return int(value, 16)
    return int(value)


def compute_server_hello_key(sample: ServerHello) -> tuple[int, int, tuple[int, ...]]:
    return sample.selected_version, sample.cipher_suite, tuple(sample.extensions)


def _allowed_tuple_keys(policy: dict[str, Any]) -> set[tuple[int, int, tuple[int, ...]]]:
    keys: set[tuple[int, int, tuple[int, ...]]] = set()
    for entry in policy.get("allowed_tuples", []):
        if not isinstance(entry, dict):
            continue
        keys.add(
            (
                _parse_u16(entry.get("selected_version", 0)),
                _parse_u16(entry.get("cipher_suite", 0)),
                tuple(_parse_u16(value) for value in entry.get("extensions", [])),
            )
        )
    return keys


def _allowed_layouts(policy: dict[str, Any]) -> set[tuple[int, ...]]:
    return {tuple(int(value) for value in layout) for layout in policy.get("allowed_layout_signatures", [])}


def check_server_hello_matrix(samples: list[ServerHello], registry: dict[str, Any]) -> tuple[bool, list[str]]:
    failures: list[str] = []
    seen_fixture_ids: set[str] = set()
    matrix = registry.get("server_hello_matrix")
    if not isinstance(matrix, dict):
        return False, ["registry: missing server_hello_matrix"]
    if not samples:
        return False, ["batch: no ServerHello samples"]

    route_modes = {sample.metadata.route_mode for sample in samples}
    if len(route_modes) != 1:
        return False, ["batch: mixed route_mode values"]

    scenario_ids = {sample.metadata.scenario_id for sample in samples}
    if len(scenario_ids) != 1:
        return False, ["batch: mixed scenario_id values"]

    source_paths = {sample.metadata.source_path for sample in samples}
    if len(source_paths) != 1:
        return False, ["batch: mixed source_path values"]

    source_hashes = {sample.metadata.source_sha256 for sample in samples}
    if len(source_hashes) != 1:
        return False, ["batch: mixed source_sha256 values"]

    families = {sample.metadata.fixture_family_id for sample in samples}
    if len(families) != 1:
        return False, ["batch: mixed fixture_family_id values"]

    for index, sample in enumerate(samples):
        if not sample.metadata.fixture_id:
            failures.append(f"sample[{index}]: missing fixture_id")
            continue
        if sample.metadata.fixture_id in seen_fixture_ids:
            failures.append(f"sample[{index}]: duplicate fixture_id {sample.metadata.fixture_id}")
            continue
        seen_fixture_ids.add(sample.metadata.fixture_id)
        if not sample.metadata.parser_version:
            failures.append(f"sample[{index}]: missing parser_version")
            continue
        if not sample.metadata.source_path:
            failures.append(f"sample[{index}]: missing source_path")
            continue
        if not sample.metadata.source_sha256:
            failures.append(f"sample[{index}]: missing source_sha256")
            continue
        if sample.metadata.source_kind not in AUTHORITATIVE_SERVER_HELLO_SOURCE_KINDS:
            failures.append(
                f"sample[{index}]: non-authoritative ServerHello source_kind {sample.metadata.source_kind}"
            )
            continue

        family = sample.metadata.fixture_family_id
        policy = matrix.get(family)
        if not isinstance(policy, dict):
            failures.append(f"sample[{index}]: unknown ServerHello family {family}")
            continue

        if sample.metadata.parser_version != str(policy.get("parser_version", "")):
            failures.append(f"sample[{index}]: parser version mismatch for family {family}")
            continue

        if compute_server_hello_key(sample) not in _allowed_tuple_keys(policy):
            failures.append(f"sample[{index}]: ServerHello tuple not allowed for family {family}")

        if tuple(sample.record_layout_signature) not in _allowed_layouts(policy):
            failures.append(f"sample[{index}]: ServerHello layout not allowed for family {family}")

    layout_signatures = {tuple(sample.record_layout_signature) for sample in samples}
    if len(samples) >= 50 and len(layout_signatures) == 1:
        failures.append("batch: synthetic ServerHello layout collapse")

    return len(failures) == 0, failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run fail-closed ServerHello matrix checks against extracted response artifacts.")
    parser.add_argument("--artifact", action="append", required=True, help="Path to an extracted ServerHello artifact JSON file. May be repeated.")
    parser.add_argument("--registry", required=True, help="Path to registry JSON containing server_hello_matrix")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with pathlib.Path(args.registry).open("r", encoding="utf-8") as infile:
        registry = json.load(infile)

    samples: list[ServerHello] = []
    failures: list[str] = []
    for artifact_path in args.artifact:
        try:
            samples.extend(load_server_hello_artifact(artifact_path))
        except Exception as exc:
            failures.append(f"artifact[{artifact_path}]: {exc}")

    ok, matrix_failures = check_server_hello_matrix(samples, registry)
    failures.extend(matrix_failures)
    report = {"ok": ok and not failures, "sample_count": len(samples), "failures": failures}
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())