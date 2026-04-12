# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

from audit_vendor import VENDOR_MANIFEST_RELATIVE_PATH
from audit_vendor import build_phase1_report
from audit_vendor import load_vendor_manifest
from audit_vendor import verify_vendor_integrity


class SqliteVendorIntegrityRepoTest(unittest.TestCase):
    def test_vendor_manifest_exists_and_matches_current_rebased_vendor_metadata(self) -> None:
        manifest_path = REPO_ROOT / VENDOR_MANIFEST_RELATIVE_PATH
        self.assertTrue(manifest_path.exists(), msg=f"missing vendor manifest: {manifest_path}")

        manifest = load_vendor_manifest(REPO_ROOT)
        report = build_phase1_report(REPO_ROOT)

        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("sqlcipher", manifest["vendor"]["base"])
        self.assertEqual("v4.14.0", manifest["vendor"]["release_tag"])
        self.assertEqual("3.51.3", manifest["vendor"]["sqlite_version"])
        self.assertRegex(manifest["vendor"]["source_tarball_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(report["sqlite_source_id"], manifest["vendor"]["sqlite_source_id"])
        self.assertEqual(report["compile_definitions"], manifest["build"]["compile_definitions"])
        self.assertEqual("tools/sqlite/generate_tdsqlite_rename.py", manifest["generation"]["rename_generator"])
        self.assertEqual("tools/sqlite/update_vendor.py", manifest["generation"]["update_script"])
        self.assertIn("sqlite/upstream/sqlite3.c", manifest["hashes"]["sha256"])
        self.assertIn("sqlite/generated/tdsqlite_rename.h", manifest["hashes"]["sha256"])

    def test_vendor_integrity_check_passes_on_current_repo(self) -> None:
        self.assertEqual([], verify_vendor_integrity(REPO_ROOT))

    def test_update_vendor_script_exists(self) -> None:
        script_path = REPO_ROOT / "tools" / "sqlite" / "update_vendor.py"
        self.assertTrue(script_path.exists(), msg=f"missing vendor update script: {script_path}")


if __name__ == "__main__":
    unittest.main()