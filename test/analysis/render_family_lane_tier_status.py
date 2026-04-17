#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Render per-family per-lane tier status evidence for nightly reporting.

The report is intentionally derived from the same inputs as
`build_family_lane_baselines.py` so we do not create a second source of truth.
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import sys
from typing import Any

import build_family_lane_baselines as baselines


# Tier thresholds from Workstream B/G policy.
_TIER_THRESHOLDS: dict[str, tuple[int, int, int]] = {
    "Tier1": (1, 1, 1),
    "Tier2": (3, 2, 2),
    "Tier3": (15, 3, 2),
    "Tier4": (200, 3, 2),
}


def _tier_rank(value: str) -> int:
    if value == "Tier0":
        return 0
    if value == "Tier1":
        return 1
    if value == "Tier2":
        return 2
    if value == "Tier3":
        return 3
    if value == "Tier4":
        return 4
    return -1


def _ordered_baselines(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted(items, key=lambda entry: (str(entry.get("family_id", "")), str(entry.get("route_lane", ""))))


def active_gates_for_lane(entry: dict[str, Any]) -> list[str]:
    tier = str(entry.get("tier", "Tier0"))
    if tier == "Tier0":
        return ["non_release_fail_closed"]

    gates = [
        "exact_invariants",
        "deterministic_rules",
        "generator_envelope_containment",
        "handshake_acceptance",
    ]
    if _tier_rank(tier) >= _tier_rank("Tier2"):
        gates.extend(
            [
                "empirical_envelope",
                "set_membership_coverage",
                "joint_state_membership",
                "cross_family_disjointness",
                "serverhello_corroboration",
            ]
        )
    if _tier_rank(tier) >= _tier_rank("Tier3"):
        gates.extend(
            [
                "distributional_one_sample",
                "classifier_loocv_gate",
            ]
        )
    if _tier_rank(tier) >= _tier_rank("Tier4"):
        gates.append("tost_equivalence")
    return gates


def _next_tier_name(current: str) -> str | None:
    if current == "Tier0":
        return "Tier1"
    if current == "Tier1":
        return "Tier2"
    if current == "Tier2":
        return "Tier3"
    if current == "Tier3":
        return "Tier4"
    return None


def next_tier_delta(entry: dict[str, Any]) -> str:
    current = str(entry.get("tier", "Tier0"))
    next_tier = _next_tier_name(current)
    if next_tier is None:
        return "Already Tier4 (highest)"

    if current == "Tier0" and int(entry.get("sample_count", 0)) == 0:
        return "Tier1: non-release fail-closed lane; add authoritative captures for this lane before promotion"

    need_auth, need_sources, need_sessions = _TIER_THRESHOLDS[next_tier]
    have_auth = int(entry.get("authoritative_sample_count", 0))
    have_sources = int(entry.get("num_sources", 0))
    have_sessions = int(entry.get("num_sessions", 0))

    miss_auth = max(0, need_auth - have_auth)
    miss_sources = max(0, need_sources - have_sources)
    miss_sessions = max(0, need_sessions - have_sessions)

    text = (
        f"{next_tier}: authoritative captures >= {need_auth} (have {have_auth}), "
        f"independent sources >= {need_sources} (have {have_sources}), "
        f"independent sessions >= {need_sessions} (have {have_sessions})"
    )

    missing_parts: list[str] = []
    if miss_auth > 0:
        missing_parts.append(f"{miss_auth} capture(s)")
    if miss_sources > 0:
        missing_parts.append(f"{miss_sources} source(s)")
    if miss_sessions > 0:
        missing_parts.append(f"{miss_sessions} session(s)")
    if missing_parts:
        text += "; missing " + ", ".join(missing_parts)
    else:
        text += "; thresholds met"

    if bool(entry.get("stale_over_180_days", False)):
        text += "; refresh captures (latest is >180 days old)"
    elif bool(entry.get("stale_over_90_days", False)):
        text += "; warning: latest capture is >90 days old"

    return text


def render_markdown_report(baselines: list[dict[str, Any]], generated_at_utc: str) -> str:
    ordered = _ordered_baselines(baselines)
    lines: list[str] = []
    lines.append("# Family-Lane Tier Status Report")
    lines.append("")
    lines.append(f"Generated at (UTC): {generated_at_utc}")
    lines.append("")
    lines.append(
        "| Family | Route Lane | Tier | Raw Tier | Authoritative Captures | Independent Sources | Independent Sessions | Active Gates | Next-Tier Delta |"
    )
    lines.append("|---|---|---|---|---:|---:|---:|---|---|")

    for entry in ordered:
        gates = ", ".join(active_gates_for_lane(entry))
        delta = next_tier_delta(entry)
        lines.append(
            "| "
            + str(entry.get("family_id", ""))
            + " | "
            + str(entry.get("route_lane", ""))
            + " | "
            + str(entry.get("tier", ""))
            + " | "
            + str(entry.get("raw_tier", ""))
            + " | "
            + str(int(entry.get("authoritative_sample_count", 0)))
            + " | "
            + str(int(entry.get("num_sources", 0)))
            + " | "
            + str(int(entry.get("num_sessions", 0)))
            + " | "
            + gates
            + " | "
            + delta
            + " |"
        )

    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render family-lane tier status report from reviewed clienthello fixtures.")
    parser.add_argument(
        "--input-dir",
        default="test/analysis/fixtures/clienthello",
        help="Directory with reviewed *.clienthello.json fixtures",
    )
    parser.add_argument(
        "--output",
        default="docs/Researches/FAMILY_LANE_TIER_STATUS.md",
        help="Output markdown path",
    )
    parser.add_argument(
        "--now-utc",
        default=None,
        help="Optional RFC3339 timestamp used as 'now' for staleness checks",
    )
    return parser.parse_args(argv)


def generate_report(input_dir: pathlib.Path, output: pathlib.Path, now_utc: str | None = None) -> str:
    samples = baselines.load_samples(input_dir)
    built = baselines.build_baselines(samples, now_utc=now_utc)
    generated_at = now_utc
    if generated_at is None:
        generated_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    rendered = render_markdown_report(built, generated_at_utc=generated_at)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(rendered, encoding="utf-8", newline="\n")
    return rendered


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    input_dir = pathlib.Path(args.input_dir).resolve()
    output = pathlib.Path(args.output).resolve()
    generate_report(input_dir=input_dir, output=output, now_utc=args.now_utc)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
