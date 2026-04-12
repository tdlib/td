# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import tempfile
import textwrap
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

from audit_vendor import CATEGORY_IMPORTED_SQLCIPHER
from audit_vendor import CATEGORY_MECHANICAL_TDSQLITE_RENAME
from audit_vendor import CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH
from audit_vendor import build_phase1_report
from audit_vendor import classify_unified_diff


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


class SqliteVendorAuditAdversarialTest(unittest.TestCase):
    def test_report_ignores_documentation_and_audit_artifacts_but_catches_real_session_consumers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = pathlib.Path(temp_dir)
            write_text(
                repo_root / "sqlite/CMakeLists.txt",
                """
                target_compile_definitions(tdsqlite PRIVATE
                  -DSQLITE_HAS_CODEC
                )
                """,
            )
            write_text(
                repo_root / "sqlite/sqlite/sqlite3.h",
                """
                #define SQLITE_VERSION "3.31.0"
                #define SQLITE_SOURCE_ID "2020-01-22 18:38:59 b86e75a273"
                /* BEGIN SQLCIPHER */
                """,
            )
            write_text(
                repo_root / "sqlite/sqlite/sqlite3.c",
                """
                /* BEGIN SQLCIPHER */
                int tdsqlite3_key_v2(void);
                """,
            )
            write_text(
                repo_root / "tddb/td/db/SqliteDb.cpp",
                """
                const char *kOne = "PRAGMA key";
                const char *kTwo = "SELECT sqlcipher_export";
                """,
            )
            write_text(
                repo_root / "docs/Plans/plan.md",
                """
                sqlite3session.h should be reviewed.
                tdsqlite3session_external_usages is just a report field.
                """,
            )
            write_text(
                repo_root / "tools/sqlite/audit_vendor.py",
                """
                sqlite3session_external_usages = []
                """,
            )
            write_text(
                repo_root / "td/consumer/use_session.cpp",
                '"""\n#include "sqlite/sqlite3session.h"\n"""\n',
            )

            report = build_phase1_report(repo_root)

            self.assertEqual(["td/consumer/use_session.cpp"], report["sqlite3session_external_usages"])

    def test_classifier_does_not_pair_changes_across_hunk_boundaries(self) -> None:
        diff_text = textwrap.dedent(
            """
            diff --git a/sqlite/sqlite/sqlite3.h b/sqlite/sqlite/sqlite3.h
            --- a/sqlite/sqlite/sqlite3.h
            +++ b/sqlite/sqlite/sqlite3.h
            @@
            -typedef struct sqlite3 sqlite3;
            @@
            +typedef struct tdsqlite3 tdsqlite3;
            """
        ).lstrip()

        result = classify_unified_diff(diff_text)

        self.assertEqual(0, result.category_counts[CATEGORY_MECHANICAL_TDSQLITE_RENAME])
        self.assertEqual(2, result.category_counts[CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH])
        self.assertEqual(2, len(result.unexplained_entries))

    def test_classifier_handles_interleaved_vendor_delta_classes_in_one_hunk(self) -> None:
        diff_text = textwrap.dedent(
            """
            diff --git a/sqlite/sqlite/sqlite3.c b/sqlite/sqlite/sqlite3.c
            --- a/sqlite/sqlite/sqlite3.c
            +++ b/sqlite/sqlite/sqlite3.c
            @@
            -typedef struct sqlite3 sqlite3;
            +typedef struct tdsqlite3 tdsqlite3;
            +/* BEGIN SQLCIPHER */
            +return SQLITE_MISUSE_BKPT;
            """
        ).lstrip()

        result = classify_unified_diff(diff_text)

        self.assertEqual(1, result.category_counts[CATEGORY_MECHANICAL_TDSQLITE_RENAME])
        self.assertEqual(1, result.category_counts[CATEGORY_IMPORTED_SQLCIPHER])
        self.assertEqual(1, result.category_counts[CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH])
        self.assertEqual(1, len(result.unexplained_entries))


if __name__ == "__main__":
    unittest.main()