# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_CATALOG_SYNC_DIR = REPO_ROOT / "tools" / "catalog_sync"
if str(TOOLS_CATALOG_SYNC_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_CATALOG_SYNC_DIR))

import refresh_static_tables


class StaticTableCodegenIntegrationTest(unittest.TestCase):
    def test_rendered_headers_roundtrip_through_repo_loader(self) -> None:
        inputs = refresh_static_tables.load_repo_material(REPO_ROOT)
        derived_sentinels = refresh_static_tables.derive_seed_rows_from_seed(bytes.fromhex("11" * 32))
        generated = refresh_static_tables.build_static_table_artifacts(inputs.public_keys, derived_sentinels)
        header_files = refresh_static_tables.render_header_files(generated)

        with tempfile.TemporaryDirectory() as temp_dir:
            output_root = pathlib.Path(temp_dir)
            for relative_path, content in header_files.items():
                destination = output_root / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(content, encoding="utf-8")

            loaded = refresh_static_tables.load_repo_table_data(output_root)
            expected_fingerprints = refresh_static_tables.compute_slot_fingerprints(inputs.public_keys)

            self.assertEqual(derived_sentinels, loaded.sentinels)
            self.assertEqual(generated.cross_checks, loaded.cross_checks)
            self.assertEqual(expected_fingerprints, refresh_static_tables.recover_slot_fingerprints(loaded.shards, loaded.sentinels))


if __name__ == "__main__":
    unittest.main()