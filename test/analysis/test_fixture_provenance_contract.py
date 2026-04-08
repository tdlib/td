# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"
REGISTRY_PATH = THIS_DIR / "profiles_validation.json"
LEGACY_BATCH1_TOKEN = "batch1"
CANONICAL_ROUTE_MODES = {"unknown", "ru_egress", "non_ru_egress"}
ALLOWED_CAPTURE_ROOTS = {
    "/home/david_osipov/tdlib-obf/docs/Samples/Traffic dumps/Android/",
    "/home/david_osipov/tdlib-obf/docs/Samples/Traffic dumps/iOS/",
    "/home/david_osipov/tdlib-obf/docs/Samples/Traffic dumps/Linux, desktop/",
    "/home/david_osipov/tdlib-obf/docs/Samples/Traffic dumps/macOS/",
}


def load_json(path: pathlib.Path) -> dict:
    with path.open("r", encoding="utf-8") as infile:
        return json.load(infile)


class FixtureProvenanceContractTest(unittest.TestCase):
    def test_registry_source_entries_use_regrouped_platform_layout(self) -> None:
        registry = load_json(REGISTRY_PATH)

        source_entries = registry.get("source")
        self.assertIsInstance(source_entries, list)
        self.assertTrue(source_entries)

        for source_entry in source_entries:
            self.assertIsInstance(source_entry, str)
            self.assertNotIn(LEGACY_BATCH1_TOKEN, source_entry)
            resolved = sorted((THIS_DIR.parent.parent / source_entry).parent.glob(pathlib.Path(source_entry).name))
            self.assertTrue(resolved, msg=f"registry source glob resolved no files: {source_entry}")

    def test_checked_in_artifacts_use_canonical_route_modes_and_real_source_paths(self) -> None:
        artifact_paths = sorted(FIXTURES_ROOT.glob("*/*.json"))
        self.assertTrue(artifact_paths)

        for artifact_path in artifact_paths:
            artifact = load_json(artifact_path)
            route_mode = artifact.get("route_mode")
            source_path = artifact.get("source_path")

            self.assertIn(route_mode, CANONICAL_ROUTE_MODES, msg=f"{artifact_path.name} route_mode drifted")
            self.assertIsInstance(source_path, str)
            self.assertNotIn(LEGACY_BATCH1_TOKEN, artifact_path.as_posix())
            self.assertNotIn(LEGACY_BATCH1_TOKEN, source_path)
            self.assertTrue(any(source_path.startswith(root) for root in ALLOWED_CAPTURE_ROOTS), msg=source_path)
            self.assertTrue(pathlib.Path(source_path).exists(), msg=f"missing capture for {artifact_path.name}")


if __name__ == "__main__":
    unittest.main()