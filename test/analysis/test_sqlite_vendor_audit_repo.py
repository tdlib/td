# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

from audit_vendor import build_phase1_report
from audit_vendor import render_markdown_audit


class SqliteVendorAuditRepoTest(unittest.TestCase):
    def test_current_tree_report_captures_version_sqlcipher_and_wrapper_surface(self) -> None:
        report = build_phase1_report(REPO_ROOT)
        header_path = report["vendor_paths"]["header"]
        source_path = report["vendor_paths"]["source"]

        self.assertEqual("phase2_scaffold", report["vendor_layout_mode"])
        self.assertEqual("3.51.3", report["sqlite_version"])
        self.assertIn("SQLITE_HAS_CODEC", report["compile_definitions"])
        self.assertEqual("sqlite/upstream/sqlite3.h", header_path)
        self.assertEqual("sqlite/upstream/sqlite3.c", source_path)
        self.assertIn("BEGIN SQLCIPHER", report["sqlcipher_markers"][header_path])
        self.assertIn("BEGIN SQLCIPHER", report["sqlcipher_markers"][source_path])
        self.assertIn("PRAGMA key", report["wrapper_sqlcipher_features"])
        self.assertIn("SELECT sqlcipher_export", report["wrapper_sqlcipher_features"])

    def test_current_tree_report_marks_sqlite3session_surface_unused_outside_vendor_and_cmake(self) -> None:
        report = build_phase1_report(REPO_ROOT)

        self.assertEqual([], report["sqlite3session_external_usages"])

    def test_rendered_markdown_answers_what_is_truly_telegram_owned(self) -> None:
        report = build_phase1_report(REPO_ROOT)

        note = render_markdown_audit(report)

        self.assertIn("Telegram-owned customizations", note)
        self.assertIn("mechanical tdsqlite3 rename", note.lower())
        self.assertIn("SQLCipher-backed dependency delta", note)
        self.assertIn("sqlite3session.h", note)

    def test_report_is_json_serializable_and_deterministic(self) -> None:
        report_one = build_phase1_report(REPO_ROOT)
        report_two = build_phase1_report(REPO_ROOT)

        self.assertEqual(
            json.dumps(report_one, indent=2, sort_keys=True),
            json.dumps(report_two, indent=2, sort_keys=True),
        )


if __name__ == "__main__":
    unittest.main()