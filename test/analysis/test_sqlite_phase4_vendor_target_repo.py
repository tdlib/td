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

from audit_vendor import build_phase1_report


TARGET_SQLCIPHER_RELEASE = "v4.14.0"
TARGET_SQLITE_VERSION = "3.51.3"
LEGACY_SQLITE_SOURCE_ID_FRAGMENT = "b86alt1"
REQUIRED_COMPILE_DEFINITION_SUBSET = {
    "SQLITE_DEFAULT_MEMSTATUS=0",
    "SQLITE_DEFAULT_RECURSIVE_TRIGGERS=1",
    "SQLITE_DEFAULT_SYNCHRONOUS=1",
    "SQLITE_ENABLE_FTS5",
    "SQLITE_HAS_CODEC",
    "SQLITE_OMIT_LOAD_EXTENSION",
    "SQLITE_TEMP_STORE=2",
}


class SqlitePhase4VendorTargetRepoTest(unittest.TestCase):
    def test_repo_has_rebased_to_target_sqlcipher_sqlite_baseline(self) -> None:
        report = build_phase1_report(REPO_ROOT)

        self.assertEqual("phase2_scaffold", report["vendor_layout_mode"])
        self.assertEqual(TARGET_SQLITE_VERSION, report["sqlite_version"])
        self.assertNotIn(LEGACY_SQLITE_SOURCE_ID_FRAGMENT, report["sqlite_source_id"])

    def test_target_rebase_preserves_sqlcipher_markers_and_required_build_profile(self) -> None:
        report = build_phase1_report(REPO_ROOT)

        header_path = report["vendor_paths"]["header"]
        source_path = report["vendor_paths"]["source"]
        self.assertIn("BEGIN SQLCIPHER", report["sqlcipher_markers"][header_path])
        self.assertIn("BEGIN SQLCIPHER", report["sqlcipher_markers"][source_path])
        self.assertTrue(
            REQUIRED_COMPILE_DEFINITION_SUBSET.issubset(set(report["compile_definitions"])),
            msg=(
                f"phase 4 target {TARGET_SQLCIPHER_RELEASE} must preserve the required SQLite feature profile: "
                f"missing {sorted(REQUIRED_COMPILE_DEFINITION_SUBSET.difference(set(report['compile_definitions'])))}"
            ),
        )


if __name__ == "__main__":
    unittest.main()