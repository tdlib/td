#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess


def load_json(path: pathlib.Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def authoritative_family_for_artifact(artifact: dict, registry: dict) -> str:
    fixture_id = str(artifact["samples"][0].get("fixture_id", ""))
    fixture = registry.get("fixtures", {}).get(fixture_id)
    if not isinstance(fixture, dict):
        raise ValueError(f"fixture id missing from registry: {fixture_id}")
    family = str(fixture.get("family", ""))
    if not family:
        raise ValueError(f"fixture family missing in registry for {fixture_id}")
    return family


def iter_clienthello_artifacts(fixtures_root: pathlib.Path) -> list[pathlib.Path]:
    return sorted(path for path in fixtures_root.rglob("*.clienthello.json") if path.is_file())


def scenario_id_for_artifact(artifact: dict) -> str:
    return f"{artifact['profile_id']}_serverhello"


def output_path_for_artifact(input_root: pathlib.Path, output_root: pathlib.Path, artifact_path: pathlib.Path) -> pathlib.Path:
    relative = artifact_path.relative_to(input_root)
    return output_root / relative.parent / relative.name.replace(".clienthello.json", ".serverhello.json")


def prune_stale_outputs(output_root: pathlib.Path, expected_paths: set[pathlib.Path]) -> None:
    for path in output_root.rglob("*.serverhello.json"):
        if path.is_file() and path not in expected_paths:
            path.unlink()


def run_generation(registry_path: pathlib.Path, input_root: pathlib.Path, output_root: pathlib.Path) -> None:
    registry = load_json(registry_path)
    artifact_paths = iter_clienthello_artifacts(input_root)
    expected_paths = {output_path_for_artifact(input_root, output_root, artifact_path) for artifact_path in artifact_paths}
    prune_stale_outputs(output_root, expected_paths)
    for artifact_path in artifact_paths:
        artifact = load_json(artifact_path)
        family = authoritative_family_for_artifact(artifact, registry)
        output_path = output_path_for_artifact(input_root, output_root, artifact_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "python3",
                "test/analysis/extract_server_hello_fixtures.py",
                "--pcap",
                str(artifact["source_path"]),
                "--route-mode",
                str(artifact["route_mode"]),
                "--scenario",
                scenario_id_for_artifact(artifact),
                "--family",
                family,
                "--out",
                str(output_path),
            ],
            check=True,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Regenerate the full ServerHello fixture corpus from the checked-in ClientHello artifact tree.")
    parser.add_argument("--registry", default="test/analysis/profiles_validation.json", help="Path to profiles_validation.json")
    parser.add_argument("--input-root", default="test/analysis/fixtures/clienthello", help="Root directory containing ClientHello fixture artifacts")
    parser.add_argument("--output-root", default="test/analysis/fixtures/serverhello", help="Root directory to write ServerHello fixture artifacts")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    run_generation(pathlib.Path(args.registry), pathlib.Path(args.input_root), pathlib.Path(args.output_root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())