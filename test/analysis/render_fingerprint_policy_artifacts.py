#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Canonical trust-tier and release-evidence policy artifact generator.

This module keeps fingerprint trust-tier semantics in one machine-readable
source and propagates them into documentation and generated release-evidence
policy artifacts.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import sys
from typing import Any


GENERATED_BLOCK_BEGIN = "<!-- BEGIN GENERATED TRUST TIER BLOCK -->"
GENERATED_BLOCK_END = "<!-- END GENERATED TRUST TIER BLOCK -->"

EXPECTED_TIERS = ("Tier0", "Tier1", "Tier2", "Tier3", "Tier4")
REQUIRED_TRANSPORT_METRICS = (
    "ttl_bucket_match_rate",
    "syn_option_order_class_match_rate",
    "mss_window_scale_bucket_match_rate",
    "first_flight_segmentation_signature_match_rate",
)
ALLOWED_STATUS_VALUES = {"pass", "fail", "pending"}
_MINIMUM_CAPTURE_THRESHOLDS = {
    "Tier0": 0,
    "Tier1": 1,
    "Tier2": 3,
    "Tier3": 15,
    "Tier4": 200,
}


def wilson_lower_bound(x_pass: int, n_cluster: int, z: float = 1.96) -> float | None:
    if n_cluster <= 0:
        return None
    p_hat = x_pass / n_cluster
    denominator = 1 + z * z / n_cluster
    center = p_hat + z * z / (2 * n_cluster)
    spread = z * ((p_hat * (1 - p_hat) / n_cluster + z * z / (4 * n_cluster * n_cluster)) ** 0.5)
    return (center - spread) / denominator


def load_tier_spec(spec_path: pathlib.Path) -> dict[str, Any]:
    with spec_path.open("r", encoding="utf-8") as infile:
        loaded = json.load(infile)
    if not isinstance(loaded, dict):
        raise ValueError("tier spec must be a JSON object")
    return loaded


def _must_be_int_non_negative(value: Any, field_name: str) -> int:
    if not isinstance(value, int):
        raise ValueError(f"{field_name} must be an integer")
    if value < 0:
        raise ValueError(f"{field_name} must be non-negative")
    return value


def validate_tier_spec(spec: dict[str, Any]) -> None:
    version = spec.get("version")
    if version != 1:
        raise ValueError("tier spec version must be 1")

    tiers = spec.get("tiers")
    if not isinstance(tiers, list):
        raise ValueError("tier spec must contain list field 'tiers'")

    by_tier: dict[str, dict[str, Any]] = {}
    for row in tiers:
        if not isinstance(row, dict):
            raise ValueError("each tier row must be an object")
        tier = row.get("tier")
        if not isinstance(tier, str):
            raise ValueError("tier row missing string field 'tier'")
        if tier in by_tier:
            raise ValueError(f"duplicate tier row {tier}")
        by_tier[tier] = row

    if tuple(by_tier.keys()) != EXPECTED_TIERS:
        raise ValueError(
            "tiers must be present exactly once in canonical order: "
            + ", ".join(EXPECTED_TIERS)
        )

    previous_capture_threshold = -1
    previous_sources_threshold = -1
    previous_sessions_threshold = -1
    for tier_name in EXPECTED_TIERS:
        row = by_tier[tier_name]

        captures = _must_be_int_non_negative(row.get("min_authoritative_captures"), f"{tier_name}.min_authoritative_captures")
        sources = _must_be_int_non_negative(row.get("min_independent_sources"), f"{tier_name}.min_independent_sources")
        sessions = _must_be_int_non_negative(row.get("min_independent_sessions"), f"{tier_name}.min_independent_sessions")

        if captures < previous_capture_threshold:
            raise ValueError(f"{tier_name} authoritative capture threshold regressed")
        if sources < previous_sources_threshold:
            raise ValueError(f"{tier_name} source threshold regressed")
        if sessions < previous_sessions_threshold:
            raise ValueError(f"{tier_name} session threshold regressed")

        floor = _MINIMUM_CAPTURE_THRESHOLDS[tier_name]
        if captures < floor:
            raise ValueError(
                f"{tier_name} authoritative capture threshold {captures} is below policy floor {floor}"
            )

        previous_capture_threshold = captures
        previous_sources_threshold = sources
        previous_sessions_threshold = sessions


def render_trust_tier_markdown_block(spec: dict[str, Any]) -> str:
    validate_tier_spec(spec)
    lines: list[str] = []
    lines.append(GENERATED_BLOCK_BEGIN)
    lines.append("Canonical source: test/analysis/fingerprint_trust_tiers.json")
    lines.append("Do not edit this block manually; regenerate via render_fingerprint_policy_artifacts.py.")
    lines.append("")
    for row in spec["tiers"]:
        tier = str(row["tier"])
        label = str(row.get("label", ""))
        captures = int(row["min_authoritative_captures"])
        sources = int(row["min_independent_sources"])
        sessions = int(row["min_independent_sessions"])
        release_gating = bool(row.get("release_gating", False))
        description = str(row.get("description", ""))
        lines.append(
            f"- {tier} ({label}): captures >= {captures}, independent sources >= {sources}, independent sessions >= {sessions}, release_gating={str(release_gating).lower()}. {description}"
        )
    lines.append(GENERATED_BLOCK_END)
    return "\n".join(lines) + "\n"


def extract_generated_trust_tier_block(text: str) -> str:
    begin = text.find(GENERATED_BLOCK_BEGIN)
    end = text.find(GENERATED_BLOCK_END)
    if begin < 0 or end < 0 or end < begin:
        raise ValueError("missing generated trust-tier block markers")
    end_inclusive = end + len(GENERATED_BLOCK_END)
    return text[begin:end_inclusive]


def inject_generated_trust_tier_block(text: str, generated_block: str) -> str:
    begin = text.find(GENERATED_BLOCK_BEGIN)
    end = text.find(GENERATED_BLOCK_END)
    if begin < 0 or end < 0 or end < begin:
        raise ValueError("missing generated trust-tier block markers")
    end_inclusive = end + len(GENERATED_BLOCK_END)
    return text[:begin] + generated_block.rstrip("\n") + text[end_inclusive:]


def _now_utc_string(now_utc: str | None = None) -> str:
    if now_utc is not None:
        return now_utc
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _must_be_mapping(value: Any, field_name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{field_name} must be an object")
    return value


def _must_be_string(value: Any, field_name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{field_name} must be a non-empty string")
    return value


def _must_be_number(value: Any, field_name: str) -> float:
    if not isinstance(value, (int, float)):
        raise ValueError(f"{field_name} must be numeric")
    number = float(value)
    if number < 0.0 or number > 1.0:
        raise ValueError(f"{field_name} must be in [0.0, 1.0]")
    return number


def load_transport_coherence_status(path: pathlib.Path) -> dict[str, Any]:
    loaded = _must_be_mapping(json.loads(path.read_text(encoding="utf-8")), "transport coherence status")
    status = _must_be_string(loaded.get("status"), "transport.status")
    if status not in ALLOWED_STATUS_VALUES:
        raise ValueError("transport.status must be one of pass/fail/pending")
    _must_be_string(loaded.get("generated_at_utc"), "transport.generated_at_utc")
    metrics = _must_be_mapping(loaded.get("metrics"), "transport.metrics")
    metric_availability = _must_be_mapping(loaded.get("metric_availability"), "transport.metric_availability")
    required_metrics = loaded.get("required_metrics")
    if not isinstance(required_metrics, list):
        raise ValueError("transport.required_metrics must be a list")
    if tuple(required_metrics) != REQUIRED_TRANSPORT_METRICS:
        raise ValueError("transport.required_metrics does not match required policy metrics")
    for metric_name in REQUIRED_TRANSPORT_METRICS:
        availability_entry = _must_be_mapping(
            metric_availability.get(metric_name), f"transport.metric_availability.{metric_name}"
        )
        availability_value = _must_be_string(
            availability_entry.get("availability"),
            f"transport.metric_availability.{metric_name}.availability",
        )
        if availability_value == "available":
            _must_be_number(metrics.get(metric_name), f"transport.metrics.{metric_name}")
        elif availability_value == "unavailable":
            if metrics.get(metric_name) is not None:
                raise ValueError(f"transport.metrics.{metric_name} must be null when unavailable")
            _must_be_string(
                availability_entry.get("reason"),
                f"transport.metric_availability.{metric_name}.reason",
            )
        else:
            raise ValueError(
                f"transport.metric_availability.{metric_name}.availability must be available/unavailable"
            )
    return loaded


def load_active_probing_status(path: pathlib.Path) -> dict[str, Any]:
    loaded = _must_be_mapping(json.loads(path.read_text(encoding="utf-8")), "active probing status")
    status = _must_be_string(loaded.get("status"), "active_probing.status")
    if status not in ALLOWED_STATUS_VALUES:
        raise ValueError("active_probing.status must be one of pass/fail/pending")
    _must_be_string(loaded.get("generated_at_utc"), "active_probing.generated_at_utc")
    scenarios = _must_be_mapping(loaded.get("scenarios"), "active_probing.scenarios")
    if not scenarios:
        raise ValueError("active_probing.scenarios must contain at least one scenario")
    for scenario_name, scenario_payload in scenarios.items():
        scenario = _must_be_mapping(scenario_payload, f"active_probing.scenarios.{scenario_name}")
        passed = scenario.get("passed")
        failed = scenario.get("failed")
        if not isinstance(passed, int) or passed < 0:
            raise ValueError(f"active_probing.scenarios.{scenario_name}.passed must be non-negative int")
        if not isinstance(failed, int) or failed < 0:
            raise ValueError(f"active_probing.scenarios.{scenario_name}.failed must be non-negative int")
    return loaded


def render_release_evidence_summary(
    spec: dict[str, Any],
    now_utc: str | None = None,
    transport_status: dict[str, Any] | None = None,
    active_probing_status: dict[str, Any] | None = None,
) -> dict[str, Any]:
    validate_tier_spec(spec)
    resolved_transport_status = {
        "status": "pending",
        "source_of_truth": "docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json",
        "artifact_path": None,
        "last_observed_at_utc": None,
        "required_metrics": list(REQUIRED_TRANSPORT_METRICS),
        "metrics": None,
        "notes": "Generate transport coherence status artifact via build_transport_coherence_status.py.",
    }
    if transport_status is not None:
        resolved_transport_status = {
            "status": str(transport_status["status"]),
            "source_of_truth": "docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json",
            "artifact_path": "docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json",
            "last_observed_at_utc": str(transport_status["generated_at_utc"]),
            "required_metrics": list(REQUIRED_TRANSPORT_METRICS),
            "metrics": dict(transport_status["metrics"]),
            "notes": str(transport_status.get("notes", "")),
        }

    resolved_active_probing_status = {
        "status": "pending",
        "artifact_path": None,
        "last_observed_at_utc": None,
        "source_of_truth": "docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json",
        "notes": "Generate active probing status artifact via build_active_probing_status.py.",
    }
    if active_probing_status is not None:
        resolved_active_probing_status = {
            "status": str(active_probing_status["status"]),
            "artifact_path": "docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json",
            "last_observed_at_utc": str(active_probing_status["generated_at_utc"]),
            "source_of_truth": "docs/Generated/FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json",
            "notes": str(active_probing_status.get("notes", "")),
            "scenarios": active_probing_status.get("scenarios", {}),
        }

    return {
        "generated_at_utc": _now_utc_string(now_utc),
        "release_gating_lane": "reviewed",
        "workflow_contract": {
            "workflow_path": ".github/workflows/fingerprint-policy-integrity.yml",
            "reviewed_job_name": "reviewed_corpus_smoke",
            "imported_job_name": "imported_corpus_smoke",
            "tier_drift_job_name": "tier_semantics_drift_check",
            "runtime_gate_job_name": "cxx_stealth_runtime_gate",
        },
        "required_release_checks": [
            "reviewed_corpus_smoke",
            "tier_semantics_drift_check",
            "reviewed_vs_imported_lane_guardrail",
            "cxx_stealth_runtime_gate",
        ],
        "informational_checks": [
            "imported_corpus_smoke",
            "active_probing_nightly",
            "transport_coherence_status",
        ],
        "reviewed_smoke_mandatory": True,
        "imported_lane_release_blocking": False,
        "advisory_profiles_count_as_tier2_or_higher": False,
        "ru_unknown_ech_fail_closed": True,
        "ru_to_non_ru_quic_blocked": True,
        "transport_layer_coverage_complete": False,
        "transport_layer_limitations": [
            "TLS corpus fidelity does not imply full TCP/IP fingerprint coherence",
            "Transport coherence metrics are tracked separately and must be attached in release evidence",
        ],
        "active_probing_nightly": resolved_active_probing_status,
        "transport_coherence_status": resolved_transport_status,
        "tier_policy": spec,
        "statistical_metadata": {
            "ci_method": "wilson",
            "cluster_level": "capture",
            "z_score": 1.96,
            "bootstrap_resamples": None,
            "note": "Cluster-collapsed Wilson score interval on capture-level Bernoulli outcomes. Bootstrap reserved for non-binary distributional metrics.",
        },
    }


def load_release_evidence_summary(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        loaded = json.load(infile)
    if not isinstance(loaded, dict):
        raise ValueError("release evidence summary must be a JSON object")
    return loaded


def generate_artifacts(repo_root: pathlib.Path, now_utc: str | None = None) -> dict[str, str]:
    spec_path = repo_root / "test" / "analysis" / "fingerprint_trust_tiers.json"
    docs = [
        repo_root / "docs" / "Documentation" / "FINGERPRINT_DOCUMENTATION_INDEX.md",
        repo_root / "docs" / "Documentation" / "FINGERPRINT_GENERATION_PIPELINE.md",
        repo_root / "docs" / "Documentation" / "FINGERPRINT_OPERATIONS_GUIDE.md",
    ]
    summary_path = repo_root / "docs" / "Generated" / "FINGERPRINT_RELEASE_EVIDENCE_POLICY.generated.json"
    transport_status_path = repo_root / "docs" / "Generated" / "FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json"
    active_probing_status_path = repo_root / "docs" / "Generated" / "FINGERPRINT_ACTIVE_PROBING_NIGHTLY_STATUS.generated.json"
    markdown_summary_path = repo_root / "docs" / "Generated" / "FINGERPRINT_TRUST_TIERS.generated.md"

    spec = load_tier_spec(spec_path)
    validate_tier_spec(spec)
    generated_block = render_trust_tier_markdown_block(spec)

    for doc_path in docs:
        content = doc_path.read_text(encoding="utf-8")
        updated = inject_generated_trust_tier_block(content, generated_block)
        doc_path.write_text(updated, encoding="utf-8", newline="\n")

    markdown_summary_path.write_text(generated_block, encoding="utf-8", newline="\n")

    transport_status = None
    if transport_status_path.exists():
        transport_status = load_transport_coherence_status(transport_status_path)

    active_probing_status = None
    if active_probing_status_path.exists():
        active_probing_status = load_active_probing_status(active_probing_status_path)

    summary = render_release_evidence_summary(
        spec,
        now_utc=now_utc,
        transport_status=transport_status,
        active_probing_status=active_probing_status,
    )
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")

    return {
        "tier_spec_path": str(spec_path),
        "trust_tier_markdown_path": str(markdown_summary_path),
        "release_evidence_summary_path": str(summary_path),
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate trust-tier and release-evidence policy artifacts.")
    parser.add_argument("--repo-root", default=str(pathlib.Path(__file__).resolve().parents[2]), help="Repository root")
    parser.add_argument("--now-utc", default=None, help="Optional RFC3339 UTC timestamp")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = pathlib.Path(args.repo_root).resolve()
    generate_artifacts(repo_root=repo_root, now_utc=args.now_utc)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
