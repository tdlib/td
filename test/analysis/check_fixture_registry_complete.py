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

from common_tls import CANONICAL_DEVICE_CLASSES, load_profile_registry, read_sha256


PLACEHOLDER_FIXTURE_ID = "<explicit-fixture-id>"
NETWORK_DERIVED_SOURCE_KINDS = {"browser_capture", "curl_cffi_capture"}
CORROBORATING_SOURCE_KINDS = {"utls_snapshot", "curl_cffi_capture", "browser_capture"}
REQUIRED_FIXTURE_TAGS = ("source_kind", "family", "trust_tier", "transport", "platform_class", "tls_gen")
CANONICAL_TRANSPORTS = {"tcp", "udp_quic_tls"}
CANONICAL_TLS_GENS = {"tls12", "tls13"}


def _is_truthy_flag(value: Any, default: bool) -> bool:
    if value is None:
        return default
    return bool(value)


def _fixture_provenance_key(fixture: dict[str, Any]) -> tuple[str, str, str]:
    return (
        str(fixture.get("source_kind", "")),
        str(fixture.get("source_path", "")),
        str(fixture.get("source_sha256", "")),
    )


def validate_registry_completeness(registry: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    fixtures = registry.get("fixtures")
    profiles = registry.get("profiles")
    contamination_guard = registry.get("contamination_guard")
    if not isinstance(fixtures, dict):
        return ["registry must contain a top-level fixtures object"]
    if not isinstance(profiles, dict):
        return ["registry must contain a top-level profiles object"]
    if contamination_guard is not None and not isinstance(contamination_guard, dict):
        return ["registry contamination_guard must be an object"]

    allow_mixed_source_kind = _is_truthy_flag(
        contamination_guard.get("allow_mixed_source_kind_per_profile") if isinstance(contamination_guard, dict) else None,
        True,
    )
    allow_mixed_family = _is_truthy_flag(
        contamination_guard.get("allow_mixed_family_per_profile") if isinstance(contamination_guard, dict) else None,
        True,
    )
    allow_advisory_code_sample = _is_truthy_flag(
        contamination_guard.get("allow_advisory_code_sample_per_profile") if isinstance(contamination_guard, dict) else None,
        True,
    )
    fail_on_missing_required_tag = _is_truthy_flag(
        contamination_guard.get("fail_on_missing_required_tag") if isinstance(contamination_guard, dict) else None,
        True,
    )

    for profile_name, profile in profiles.items():
        if not isinstance(profile, dict):
            failures.append(f"profile {profile_name} must be an object")
            continue

        include_fixture_ids = profile.get("include_fixture_ids")
        if not isinstance(include_fixture_ids, list) or not include_fixture_ids:
            failures.append(f"profile {profile_name} must include a non-empty include_fixture_ids list")
            continue

        resolved_fixtures: list[dict[str, Any]] = []
        for fixture_id in include_fixture_ids:
            if fixture_id == PLACEHOLDER_FIXTURE_ID:
                failures.append(f"profile {profile_name} contains placeholder fixture id {PLACEHOLDER_FIXTURE_ID}")
                continue
            fixture = fixtures.get(fixture_id)
            if not isinstance(fixture, dict):
                failures.append(f"profile {profile_name} references unknown fixture id {fixture_id}")
                continue
            if fail_on_missing_required_tag:
                for required_tag in REQUIRED_FIXTURE_TAGS:
                    if not fixture.get(required_tag):
                        failures.append(f"fixture {fixture_id} is missing required tag {required_tag}")
            transport = fixture.get("transport")
            if transport and transport not in CANONICAL_TRANSPORTS:
                failures.append(f"fixture {fixture_id} has invalid transport {transport}")
            platform_class = fixture.get("platform_class")
            if platform_class and platform_class not in CANONICAL_DEVICE_CLASSES:
                failures.append(f"fixture {fixture_id} has invalid platform_class {platform_class}")
            tls_gen = fixture.get("tls_gen")
            if tls_gen and tls_gen not in CANONICAL_TLS_GENS:
                failures.append(f"fixture {fixture_id} has invalid tls_gen {tls_gen}")
            source_path = fixture.get("source_path")
            if not source_path:
                failures.append(f"fixture {fixture_id} is missing source_path")
            else:
                source_file = pathlib.Path(source_path)
                if not source_file.exists():
                    failures.append(f"fixture {fixture_id} source_path does not exist: {source_path}")
                elif fixture.get("source_sha256"):
                    actual_sha256 = read_sha256(source_file)
                    if actual_sha256 != fixture.get("source_sha256"):
                        failures.append(f"fixture {fixture_id} source_sha256 mismatch for {source_path}")
            if not fixture.get("source_sha256"):
                failures.append(f"fixture {fixture_id} is missing source_sha256")
            resolved_fixtures.append(fixture)

        if not profile.get("release_gating", False):
            continue

        source_kinds = {fixture.get("source_kind") for fixture in resolved_fixtures if fixture.get("source_kind")}
        if not allow_mixed_source_kind and len(source_kinds) > 1:
            failures.append(f"release-gating profile {profile_name} mixes source_kind values")

        families = {fixture.get("family") for fixture in resolved_fixtures if fixture.get("family")}
        if not allow_mixed_family and len(families) > 1:
            failures.append(f"release-gating profile {profile_name} mixes fixture families")

        for fixture_id in include_fixture_ids:
            fixture = fixtures.get(fixture_id)
            if not isinstance(fixture, dict):
                continue
            if not allow_advisory_code_sample and fixture.get("source_kind") == "advisory_code_sample":
                failures.append(f"release-gating profile {profile_name} includes advisory_code_sample fixture {fixture_id}")
            if fixture.get("trust_tier") != "verified":
                failures.append(f"release-gating profile {profile_name} includes non-verified fixture {fixture_id}")

        network_fixtures = [
            fixture for fixture in resolved_fixtures if fixture.get("source_kind") in NETWORK_DERIVED_SOURCE_KINDS
        ]
        if not network_fixtures:
            failures.append(f"release-gating profile {profile_name} has no network-derived fixture")

        corroborating_provenance = {
            _fixture_provenance_key(fixture)
            for fixture in resolved_fixtures
            if fixture.get("source_kind") in CORROBORATING_SOURCE_KINDS
        }
        has_independent_corroboration = len(corroborating_provenance) >= 2
        if not has_independent_corroboration:
            failures.append(f"release-gating profile {profile_name} has no independent corroborating fixture")

    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fail closed if profiles_validation.json still contains placeholder or incomplete fixture wiring."
    )
    parser.add_argument("--registry", required=True, help="Path to profiles_validation.json")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    registry = load_profile_registry(args.registry)
    failures = validate_registry_completeness(registry)
    report = {
        "ok": not failures,
        "failures": failures,
    }
    if args.report_out:
        report_path = pathlib.Path(args.report_out)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())