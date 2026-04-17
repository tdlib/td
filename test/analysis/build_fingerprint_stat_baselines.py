#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Thin entry-point that delegates baseline generation to the canonical
build_family_lane_baselines pipeline and additionally emits the
fingerprint_stat_baselines.json artifact expected by the plan.

All real logic lives in build_family_lane_baselines.py. This script only
provides the plan-specified CLI surface and wires it to the real generator.

Usage:
  python3 test/analysis/build_fingerprint_stat_baselines.py \\
      --input test/analysis/fixtures/clienthello \\
      --output-header test/stealth/ReviewedFingerprintStatBaselines.h \\
      --output-json test/analysis/fingerprint_stat_baselines.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

_HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

import build_family_lane_baselines as _real  # noqa: E402


def _baselines_to_json(baselines: list) -> str:
    """Serialize baseline list to the plan-specified JSON format.

    The JSON is machine-readable and contains the same information as the
    generated C++ header. Keys are named after the plan's terminology
    (fingerprint_stat_baselines) but the underlying data is identical to
    the family-lane baseline catalog.
    """
    records: list[dict] = []
    for b in baselines:
        records.append(
            {
                "family_id": b["family_id"],
                "route_lane": b["route_lane"],
                "tier": b["tier"],
                "raw_tier": b.get("raw_tier", b["tier"]),
                "sample_count": b.get("sample_count", 0),
                "authoritative_sample_count": b.get("authoritative_sample_count", 0),
                "num_sources": b.get("num_sources", 0),
                "num_sessions": b.get("num_sessions", 0),
                "stale_over_90_days": b.get("stale_over_90_days", False),
                "stale_over_180_days": b.get("stale_over_180_days", False),
                "invariants": b.get("invariants", {}),
                "set_catalog": b.get("set_catalog", {}),
            }
        )
    return json.dumps({"schema_version": 1, "baselines": records}, indent=2, ensure_ascii=False) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Emit reviewed family/lane baseline artifacts from frozen ClientHello fixtures. "
            "Delegates to build_family_lane_baselines for all real computation."
        )
    )
    parser.add_argument(
        "--input",
        default="test/analysis/fixtures/clienthello",
        help="Directory containing *.clienthello.json fixtures.",
    )
    parser.add_argument(
        "--output-header",
        default="test/stealth/ReviewedFingerprintStatBaselines.h",
        help="Path for the generated C++ header (forwarding wrapper).",
    )
    parser.add_argument(
        "--output-json",
        default=None,
        help="Path for the generated JSON artifact (fingerprint_stat_baselines.json).",
    )
    args = parser.parse_args()

    input_dir = pathlib.Path(args.input).resolve()
    header_path = pathlib.Path(args.output_header).resolve()

    # The canonical generator produces full baseline data and the C++ header.
    samples = _real.load_samples(input_dir)
    baselines = _real.build_baselines(samples)

    # Emit the C++ header as a thin forwarding wrapper; the real constants
    # live in ReviewedFamilyLaneBaselines.h to avoid duplicating large data.
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(_render_forwarding_header(), encoding="utf-8", newline="\n")
    print(f"[fingerprint_stat_baselines] wrote header -> {header_path}", file=sys.stderr)

    # Optionally emit the JSON artifact.
    if args.output_json:
        json_path = pathlib.Path(args.output_json).resolve()
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(_baselines_to_json(baselines), encoding="utf-8", newline="\n")
        print(f"[fingerprint_stat_baselines] wrote json  -> {json_path}", file=sys.stderr)


def _render_forwarding_header() -> str:
    return (
        "// SPDX-FileCopyrightText: Copyright 2026 telemt community\n"
        "// SPDX-License-Identifier: MIT\n"
        "// telemt: https://github.com/telemt\n"
        "// telemt: https://t.me/telemtrs\n"
        "//\n"
        "\n"
        "// This file is the plan-specified fingerprint_stat_baselines header.\n"
        "// All data constants live in ReviewedFamilyLaneBaselines.h, which is\n"
        "// the authoritative generated artifact. This header forwards everything\n"
        "// so that callers using the plan's naming compile without changes.\n"
        "\n"
        "#pragma once\n"
        "\n"
        "#include \"test/stealth/ReviewedFamilyLaneBaselines.h\"\n"
    )


if __name__ == "__main__":
    main()
