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

from audit_vendor import CATEGORY_IMPORTED_SQLCIPHER
from audit_vendor import CATEGORY_MECHANICAL_TDSQLITE_RENAME
from audit_vendor import CATEGORY_TELEGRAM_BUILD_CONFIGURATION
from audit_vendor import CATEGORY_TELEGRAM_WRAPPER_POLICY
from audit_vendor import CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH
from audit_vendor import classify_unified_diff
from audit_vendor import is_mechanical_tdsqlite_rename


def make_diff(path: str, body: str) -> str:
    return (
        f"diff --git a/{path} b/{path}\n"
        f"--- a/{path}\n"
        f"+++ b/{path}\n"
        "@@\n"
        f"{body}"
    )


class MechanicalTdsqliteRenameTest(unittest.TestCase):
    def test_accepts_exact_prefix_injection_for_public_symbols(self) -> None:
        cases = [
            (
                "SQLITE_API int sqlite3_open_v2(const char*, sqlite3**, int, const char*);",
                "SQLITE_API int tdsqlite3_open_v2(const char*, tdsqlite3**, int, const char*);",
            ),
            ("typedef struct sqlite3 sqlite3;", "typedef struct tdsqlite3 tdsqlite3;"),
            (
                "struct sqlite3_api_routines { const sqlite3_value *value; };",
                "struct tdsqlite3_api_routines { const tdsqlite3_value *value; };",
            ),
        ]

        for old_line, new_line in cases:
            with self.subTest(old_line=old_line, new_line=new_line):
                self.assertTrue(is_mechanical_tdsqlite_rename(old_line, new_line))

    def test_rejects_partial_identifier_overlap_and_semantic_changes(self) -> None:
        cases = [
            (
                "const char *symbol = \"notsqlite3_open_v2\";",
                "const char *symbol = \"nottdsqlite3_open_v2\";",
            ),
            ("return sqlite3_step(stmt);", "return tdsqlite3_step(stmt) + 1;"),
            ("const char *name = \"sqlite3.h\";", "const char *name = \"tdsqlite3.h\";"),
        ]

        for old_line, new_line in cases:
            with self.subTest(old_line=old_line, new_line=new_line):
                self.assertFalse(is_mechanical_tdsqlite_rename(old_line, new_line))


class ClassifyUnifiedDiffTest(unittest.TestCase):
    def test_classifies_tdsqlite_rename_hunks_without_unexplained_residue(self) -> None:
        diff_text = make_diff(
            "sqlite/sqlite/sqlite3.h",
            "-SQLITE_API int sqlite3_open_v2(const char*, sqlite3**, int, const char*);\n"
            "+SQLITE_API int tdsqlite3_open_v2(const char*, tdsqlite3**, int, const char*);\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(1, result.category_counts[CATEGORY_MECHANICAL_TDSQLITE_RENAME])
        self.assertEqual([], result.unexplained_entries)

    def test_classifies_sqlcipher_markers_as_imported_dependency_delta(self) -> None:
        diff_text = make_diff(
            "sqlite/sqlite/sqlite3.c",
            "+/* BEGIN SQLCIPHER */\n"
            "+int sqlite3_key_v2(sqlite3 *db, const char *zDbName, const void *pKey, int nKey);\n"
            "+int sqlcipher_exportFunc(sqlite3_context *ctx, int argc, sqlite3_value **argv);\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(3, result.category_counts[CATEGORY_IMPORTED_SQLCIPHER])
        self.assertEqual([], result.unexplained_entries)

    def test_classifies_sqlite_cmake_feature_profile_as_telegram_build_configuration(self) -> None:
        diff_text = make_diff(
            "sqlite/CMakeLists.txt",
            "+# all SQLite functions are moved to namespace tdsqlite3 by `sed -Ebi 's/sqlite3([^.]|$)/td&/g' *`\n"
            "+  -DSQLITE_HAS_CODEC\n"
            "+  -DSQLITE_OMIT_LOAD_EXTENSION\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(3, result.category_counts[CATEGORY_TELEGRAM_BUILD_CONFIGURATION])
        self.assertEqual([], result.unexplained_entries)

    def test_classifies_wrapper_policy_lines_outside_vendor_tree(self) -> None:
        diff_text = make_diff(
            "tddb/td/db/detail/RawSqliteDb.cpp",
            "+  if (code == SQLITE_CORRUPT) {\n"
            "+    destroy(path_).ignore();\n"
            "+  }\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(3, result.category_counts[CATEGORY_TELEGRAM_WRAPPER_POLICY])
        self.assertEqual([], result.unexplained_entries)

    def test_surfaces_unexpected_semantic_vendor_patch_without_relaxing_signal(self) -> None:
        diff_text = make_diff(
            "sqlite/sqlite/sqlite3.c",
            "-  if( zSql==0 ) return SQLITE_NOMEM_BKPT;\n"
            "+  if( zSql==0 ) return SQLITE_MISUSE_BKPT;\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(1, result.category_counts[CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH])
        self.assertEqual(1, len(result.unexplained_entries))
        self.assertEqual("sqlite/sqlite/sqlite3.c", result.unexplained_entries[0]["file_path"])

    def test_keeps_mixed_hunks_red_when_any_unexplained_delta_survives(self) -> None:
        diff_text = make_diff(
            "sqlite/sqlite/sqlite3.h",
            "-typedef struct sqlite3 sqlite3;\n"
            "+typedef struct tdsqlite3 tdsqlite3;\n"
            "+#define SQLITE_DANGEROUS_BACKDOOR 1\n",
        )

        result = classify_unified_diff(diff_text)

        self.assertEqual(1, result.category_counts[CATEGORY_MECHANICAL_TDSQLITE_RENAME])
        self.assertEqual(1, result.category_counts[CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH])
        self.assertEqual(1, len(result.unexplained_entries))


if __name__ == "__main__":
    unittest.main()