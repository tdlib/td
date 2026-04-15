# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_CATALOG_SYNC_DIR = REPO_ROOT / "tools" / "catalog_sync"
GITIGNORE_PATH = REPO_ROOT / ".gitignore"
if str(TOOLS_CATALOG_SYNC_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_CATALOG_SYNC_DIR))

import refresh_static_tables


class StaticTableCodegenContractTest(unittest.TestCase):
    def test_tool_scaffold_exists(self) -> None:
        self.assertTrue(
            (TOOLS_CATALOG_SYNC_DIR / "refresh_static_tables.py").exists(),
            msg="static table refresh tool must exist in tools/catalog_sync",
        )
        self.assertTrue(
            (TOOLS_CATALOG_SYNC_DIR / "requirements.txt").exists(),
            msg="static table refresh tool must declare its Python dependencies",
        )

    def test_manifest_path_is_gitignored(self) -> None:
        gitignore_text = GITIGNORE_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "tools/catalog_sync/table_manifest.json",
            gitignore_text,
            msg="generated table manifests must not be tracked in git",
        )

    def test_current_repo_inputs_match_checked_in_tables(self) -> None:
        inputs = refresh_static_tables.load_repo_material(REPO_ROOT)
        actual = refresh_static_tables.load_repo_table_data(REPO_ROOT)
        expected_fingerprints = refresh_static_tables.compute_slot_fingerprints(inputs.public_keys)
        expected_checks = refresh_static_tables.build_check_tables(expected_fingerprints, inputs.sentinels)
        recovered_fingerprints = refresh_static_tables.recover_slot_fingerprints(actual.shards, inputs.sentinels)

        self.assertEqual(actual.sentinels, inputs.sentinels)
        self.assertEqual(actual.cross_checks, expected_checks)
        self.assertEqual(recovered_fingerprints, expected_fingerprints)

    def test_manifest_tracks_only_non_secret_metadata(self) -> None:
        inputs = refresh_static_tables.load_repo_material(REPO_ROOT)
        generated = refresh_static_tables.build_static_table_artifacts(inputs.public_keys, inputs.sentinels)
        manifest_text = refresh_static_tables.render_state_manifest_json(generated.manifest)

        for role_name, fingerprint in {
            "primary": "0xd09d1d85de64fd85",
            "secondary": "0xb25898df208d2603",
            "auxiliary": "0x6f3a701151477715",
        }.items():
            with self.subTest(role_name=role_name):
                self.assertEqual(fingerprint, generated.manifest["roles"][role_name]["fingerprint"])

        self.assertNotIn("BEGIN RSA PUBLIC KEY", manifest_text)
        self.assertNotIn("END RSA PUBLIC KEY", manifest_text)


if __name__ == "__main__":
    unittest.main()