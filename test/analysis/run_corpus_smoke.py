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

from check_drs import check_drs
from check_record_size_distribution import check_record_size_distribution
from check_flow_behavior import DEFAULT_POLICY as DEFAULT_FLOW_POLICY, check_flow_behavior
from check_ipt import check_ipt
from check_server_hello_matrix import check_server_hello_matrix
from check_fingerprint import collect_fingerprint_telemetry, run_all_checks
from check_fixture_registry_complete import validate_registry_completeness
from common_smoke import load_json_report
from common_tls import ClientHello, load_clienthello_artifact, load_profile_registry, load_server_hello_artifact


def discover_clienthello_artifacts(fixtures_root: str | pathlib.Path) -> list[pathlib.Path]:
    root = pathlib.Path(fixtures_root)
    return sorted(path for path in root.rglob("*.json") if path.is_file())


def _profile_names(samples: list[ClientHello]) -> list[str]:
    return sorted({sample.profile for sample in samples})


def _effective_clienthello_family(sample: ClientHello, registry: dict[str, Any]) -> str:
    if sample.metadata.fixture_family_id:
        return sample.metadata.fixture_family_id
    if sample.metadata.fixture_id:
        fixture = registry.get("fixtures", {}).get(sample.metadata.fixture_id)
        if isinstance(fixture, dict):
            family = str(fixture.get("family", ""))
            if family:
                return family
    return ""


def _clienthello_family_index(samples: list[ClientHello], registry: dict[str, Any]) -> dict[tuple[str, str, str], set[str]]:
    index: dict[tuple[str, str, str], set[str]] = {}
    for sample in samples:
        family = _effective_clienthello_family(sample, registry)
        if not family:
            continue
        key = (
            sample.metadata.source_path,
            sample.metadata.source_sha256,
            sample.metadata.route_mode,
        )
        index.setdefault(key, set()).add(family)
    return index


def _clienthello_path_route_index(samples: list[ClientHello], registry: dict[str, Any]) -> dict[tuple[str, str], dict[str, set[str]]]:
    index: dict[tuple[str, str], dict[str, set[str]]] = {}
    for sample in samples:
        family = _effective_clienthello_family(sample, registry)
        if not family:
            continue
        key = (sample.metadata.source_path, sample.metadata.route_mode)
        sha_index = index.setdefault(key, {})
        sha_index.setdefault(sample.metadata.source_sha256, set()).add(family)
    return index


def _run_generic_smoke_stage(
    stage_name: str,
    fixtures_root: str | pathlib.Path | None,
    checker: Any,
    checker_policy: dict[str, Any],
) -> tuple[list[dict[str, Any]], int, list[str]]:
    artifact_reports: list[dict[str, Any]] = []
    sample_count = 0
    failures: list[str] = []
    if fixtures_root is None:
        return artifact_reports, sample_count, failures

    root = pathlib.Path(fixtures_root)
    artifacts = discover_clienthello_artifacts(root)
    if not artifacts:
        failures.append(f"{stage_name}: no {stage_name} artifacts found under {root}")

    for artifact_path in artifacts:
        artifact_report: dict[str, Any] = {
            "artifact_path": str(artifact_path),
            "ok": False,
            "sample_count": 0,
            "failures": [],
        }
        try:
            report = load_json_report(artifact_path)
        except Exception as exc:
            artifact_report["failures"] = [f"artifact-load: {exc}"]
            failures.append(f"{stage_name}[{artifact_path}]: artifact-load: {exc}")
            artifact_reports.append(artifact_report)
            continue

        if stage_name == "ipt":
            artifact_report["sample_count"] = len(report.get("runs", [])) if isinstance(report.get("runs"), list) else 0
        elif stage_name == "drs":
            artifact_report["sample_count"] = (
                len(report.get("record_payload_sizes", [])) if isinstance(report.get("record_payload_sizes"), list) else 0
            )
        elif stage_name == "flow":
            artifact_report["sample_count"] = len(report.get("connections", [])) if isinstance(report.get("connections"), list) else 0
        elif stage_name == "record_size":
            if isinstance(report.get("record_payload_sizes"), list):
                artifact_report["sample_count"] = len(report.get("record_payload_sizes", []))
            elif isinstance(report.get("connections"), list):
                artifact_report["sample_count"] = sum(
                    len(connection.get("records", []))
                    for connection in report.get("connections", [])
                    if isinstance(connection, dict) and isinstance(connection.get("records"), list)
                )
        sample_count += artifact_report["sample_count"]

        stage_failures = checker(report, checker_policy)
        artifact_report["ok"] = not stage_failures
        artifact_report["failures"] = stage_failures
        for failure in stage_failures:
            failures.append(f"{stage_name}[{artifact_path}]: {failure}")
        artifact_reports.append(artifact_report)

    return artifact_reports, sample_count, failures


def run_corpus_smoke(
    registry_path: str | pathlib.Path,
    fixtures_root: str | pathlib.Path,
    server_hello_fixtures_root: str | pathlib.Path | None = None,
    ipt_fixtures_root: str | pathlib.Path | None = None,
    drs_fixtures_root: str | pathlib.Path | None = None,
    flow_fixtures_root: str | pathlib.Path | None = None,
    record_size_fixtures_root: str | pathlib.Path | None = None,
) -> dict[str, Any]:
    registry = load_profile_registry(registry_path)
    registry_failures = validate_registry_completeness(registry)

    artifacts = discover_clienthello_artifacts(fixtures_root)
    artifact_reports: list[dict[str, Any]] = []
    all_samples: list[ClientHello] = []
    top_level_failures = [f"registry: {failure}" for failure in registry_failures]
    server_hello_artifact_reports: list[dict[str, Any]] = []
    server_hello_sample_count = 0

    if not artifacts:
        top_level_failures.append(f"artifacts: no clienthello artifacts found under {pathlib.Path(fixtures_root)}")

    for artifact_path in artifacts:
        artifact_report: dict[str, Any] = {
            "artifact_path": str(artifact_path),
            "ok": False,
            "sample_count": 0,
            "profiles": [],
            "failures": [],
        }
        try:
            samples = load_clienthello_artifact(artifact_path)
        except Exception as exc:
            artifact_report["failures"] = [f"artifact-load: {exc}"]
            top_level_failures.append(f"artifact[{artifact_path}]: artifact-load: {exc}")
            artifact_reports.append(artifact_report)
            continue

        if not samples:
            failure = "artifact contains no ClientHello samples"
            artifact_report["failures"] = [failure]
            top_level_failures.append(f"artifact[{artifact_path}]: {failure}")
            artifact_reports.append(artifact_report)
            continue

        artifact_report["sample_count"] = len(samples)
        artifact_report["profiles"] = _profile_names(samples)
        all_samples.extend(samples)

        ok, failures = run_all_checks(samples, registry)
        artifact_report["ok"] = ok
        artifact_report["failures"] = failures
        for failure in failures:
            top_level_failures.append(f"artifact[{artifact_path}]: {failure}")
        artifact_reports.append(artifact_report)

    clienthello_family_index = _clienthello_family_index(all_samples, registry)
    clienthello_path_route_index = _clienthello_path_route_index(all_samples, registry)

    if server_hello_fixtures_root is not None:
        server_hello_root = pathlib.Path(server_hello_fixtures_root)
        server_hello_artifacts = discover_clienthello_artifacts(server_hello_root)
        if not server_hello_artifacts:
            top_level_failures.append(f"serverhello: no server hello artifacts found under {server_hello_root}")

        for artifact_path in server_hello_artifacts:
            artifact_report: dict[str, Any] = {
                "artifact_path": str(artifact_path),
                "ok": False,
                "sample_count": 0,
                "failures": [],
            }
            try:
                samples = load_server_hello_artifact(artifact_path)
            except Exception as exc:
                artifact_report["failures"] = [f"artifact-load: {exc}"]
                top_level_failures.append(f"serverhello[{artifact_path}]: artifact-load: {exc}")
                server_hello_artifact_reports.append(artifact_report)
                continue

            artifact_report["sample_count"] = len(samples)
            server_hello_sample_count += len(samples)
            ok, failures = check_server_hello_matrix(samples, registry)
            batch_meta = samples[0].metadata
            capture_key = (batch_meta.source_path, batch_meta.source_sha256, batch_meta.route_mode)
            matching_clienthello_families = clienthello_family_index.get(capture_key)
            if not matching_clienthello_families:
                path_route_key = (batch_meta.source_path, batch_meta.route_mode)
                same_path_route = clienthello_path_route_index.get(path_route_key)
                if same_path_route:
                    expected_sha256_values = ", ".join(sorted(same_path_route))
                    failures = list(failures) + [
                        f"batch: ServerHello source_sha256 {batch_meta.source_sha256} does not match ClientHello capture metadata for source_path {batch_meta.source_path}; expected one of [{expected_sha256_values}]"
                    ]
                else:
                    failures = list(failures) + [
                        "batch: no matching ClientHello capture metadata for ServerHello artifact"
                    ]
                ok = False
            elif batch_meta.fixture_family_id not in matching_clienthello_families:
                expected = ", ".join(sorted(matching_clienthello_families))
                failures = list(failures) + [
                    f"batch: ServerHello family {batch_meta.fixture_family_id} does not match ClientHello families [{expected}]"
                ]
                ok = False
            artifact_report["ok"] = ok
            artifact_report["failures"] = failures
            for failure in failures:
                top_level_failures.append(f"serverhello[{artifact_path}]: {failure}")
            server_hello_artifact_reports.append(artifact_report)

    ipt_artifact_reports, ipt_sample_count, ipt_failures = _run_generic_smoke_stage(
        "ipt",
        ipt_fixtures_root,
        check_ipt,
        {"baseline_distance_threshold": 0.10},
    )
    top_level_failures.extend(ipt_failures)

    drs_artifact_reports, drs_sample_count, drs_failures = _run_generic_smoke_stage(
        "drs",
        drs_fixtures_root,
        check_drs,
        {"histogram_distance_threshold": 0.15},
    )
    top_level_failures.extend(drs_failures)

    flow_artifact_reports, flow_sample_count, flow_failures = _run_generic_smoke_stage(
        "flow",
        flow_fixtures_root,
        check_flow_behavior,
        dict(DEFAULT_FLOW_POLICY),
    )
    top_level_failures.extend(flow_failures)

    record_size_artifact_reports: list[dict[str, Any]] = []
    record_size_sample_count = 0
    if record_size_fixtures_root is not None:
        record_size_policy = registry.get("record_size_baseline_policy")
        if not isinstance(record_size_policy, dict):
            top_level_failures.append("record_size: registry missing record_size_baseline_policy")
        else:
            record_size_artifact_reports, record_size_sample_count, record_size_failures = _run_generic_smoke_stage(
                "record_size",
                record_size_fixtures_root,
                check_record_size_distribution,
                dict(record_size_policy),
            )
            top_level_failures.extend(record_size_failures)

    report = {
        "ok": not top_level_failures,
        "registry_failures": registry_failures,
        "artifacts": artifact_reports,
        "artifact_count": len(artifact_reports),
        "sample_count": len(all_samples),
        "telemetry": collect_fingerprint_telemetry(all_samples),
        "server_hello_artifacts": server_hello_artifact_reports,
        "server_hello_artifact_count": len(server_hello_artifact_reports),
        "server_hello_sample_count": server_hello_sample_count,
        "ipt_artifacts": ipt_artifact_reports,
        "ipt_artifact_count": len(ipt_artifact_reports),
        "ipt_sample_count": ipt_sample_count,
        "drs_artifacts": drs_artifact_reports,
        "drs_artifact_count": len(drs_artifact_reports),
        "drs_sample_count": drs_sample_count,
        "flow_artifacts": flow_artifact_reports,
        "flow_artifact_count": len(flow_artifact_reports),
        "flow_sample_count": flow_sample_count,
        "record_size_artifacts": record_size_artifact_reports,
        "record_size_artifact_count": len(record_size_artifact_reports),
        "record_size_sample_count": record_size_sample_count,
        "failures": top_level_failures,
    }
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run PR-9 corpus smoke checks over the checked-in ClientHello artifact tree.")
    parser.add_argument("--registry", required=True, help="Path to profiles_validation.json")
    parser.add_argument(
        "--fixtures-root",
        default="test/analysis/fixtures/clienthello",
        help="Root directory containing checked-in ClientHello artifact JSON files",
    )
    parser.add_argument(
        "--server-hello-fixtures-root",
        help="Optional root directory containing extracted ServerHello artifact JSON files",
    )
    parser.add_argument(
        "--ipt-fixtures-root",
        help="Optional root directory containing IPT smoke artifact JSON files",
    )
    parser.add_argument(
        "--drs-fixtures-root",
        help="Optional root directory containing DRS smoke artifact JSON files",
    )
    parser.add_argument(
        "--flow-fixtures-root",
        help="Optional root directory containing flow-behavior smoke artifact JSON files",
    )
    parser.add_argument(
        "--record-size-fixtures-root",
        help="Optional root directory containing TLS record-size artifact JSON files",
    )
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = run_corpus_smoke(
        args.registry,
        args.fixtures_root,
        args.server_hello_fixtures_root,
        args.ipt_fixtures_root,
        args.drs_fixtures_root,
        args.flow_fixtures_root,
        args.record_size_fixtures_root,
    )
    if args.report_out:
        pathlib.Path(args.report_out).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())