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

from generate_tdsqlite_rename import collect_sqlite_identifiers_from_files
from generate_tdsqlite_rename import render_rename_header


class SqliteVendorScaffoldRepoTest(unittest.TestCase):
    def test_phase2_scaffold_artifacts_exist(self) -> None:
        required_paths = [
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3.c",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3.h",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3ext.h",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3session.h",
            REPO_ROOT / "sqlite" / "generated" / "tdsqlite_rename.h",
            REPO_ROOT / "sqlite" / "tdsqlite_amalgamation.c",
        ]

        for path in required_paths:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), msg=f"missing scaffold artifact: {path}")

    def test_generated_rename_header_matches_current_upstream_inputs(self) -> None:
        source_paths = [
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3.h",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3ext.h",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3session.h",
            REPO_ROOT / "sqlite" / "upstream" / "sqlite3.c",
        ]
        expected = render_rename_header(collect_sqlite_identifiers_from_files(source_paths), source_paths)
        actual = (REPO_ROOT / "sqlite" / "generated" / "tdsqlite_rename.h").read_text(encoding="utf-8")

        self.assertEqual(expected, actual)

    def test_generated_rename_header_keeps_public_api_names_but_not_parser_guard_macros(self) -> None:
        text = (REPO_ROOT / "sqlite" / "generated" / "tdsqlite_rename.h").read_text(encoding="utf-8")

        self.assertIn("#define sqlite3_open_v2 tdsqlite3_open_v2", text)
        self.assertIn("#define sqlite3_prepare_v2 tdsqlite3_prepare_v2", text)
        self.assertNotIn("sqlite3Fts5Parser_ENGINEALWAYSONSTACK", text)
        self.assertNotIn("sqlite3Parser_ENGINEALWAYSONSTACK", text)

    def test_generated_rename_header_excludes_source_local_macro_names_that_upstream_redefines(self) -> None:
        text = (REPO_ROOT / "sqlite" / "generated" / "tdsqlite_rename.h").read_text(encoding="utf-8")

        self.assertNotIn("sqlite3ParserARG_SDECL", text)
        self.assertNotIn("sqlite3Fts5ParserARG_SDECL", text)
        self.assertNotIn("sqlite3ConnectionBlocked", text)

    def test_wrapper_headers_are_thin_compatibility_layers(self) -> None:
        wrapper_specs = [
            (REPO_ROOT / "sqlite" / "sqlite" / "sqlite3.h", "sqlite3.h"),
            (REPO_ROOT / "sqlite" / "sqlite" / "sqlite3ext.h", "sqlite3ext.h"),
            (REPO_ROOT / "sqlite" / "sqlite" / "sqlite3session.h", "sqlite3session.h"),
        ]

        for path, upstream_name in wrapper_specs:
            text = path.read_text(encoding="utf-8")
            with self.subTest(path=path):
                self.assertIn('#include "../generated/tdsqlite_rename.h"', text)
                self.assertIn(f'#include "../upstream/{upstream_name}"', text)
                self.assertNotIn("BEGIN SQLCIPHER", text)
                self.assertNotIn("tdsqlite3_open_v2", text)

    def test_cmake_builds_through_wrapper_translation_unit(self) -> None:
        cmake_text = (REPO_ROOT / "sqlite" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("tdsqlite_amalgamation.c", cmake_text)
        self.assertNotIn("sqlite/sqlite3.c", cmake_text)
        self.assertNotIn("sed -Ebi", cmake_text)

    def test_sqlite_subtree_has_local_readme_for_vendor_workflow(self) -> None:
        readme_path = REPO_ROOT / "sqlite" / "README.md"
        self.assertTrue(readme_path.exists(), msg=f"missing SQLite subtree README: {readme_path}")

        readme_text = readme_path.read_text(encoding="utf-8")

        self.assertIn("tools/sqlite/update_vendor.py", readme_text)
        self.assertIn("tools/sqlite/audit_vendor.py", readme_text)
        self.assertIn("sqlite/VENDOR.json", readme_text)
        self.assertIn("sqlite/upstream/", readme_text)
        self.assertIn("sqlite/generated/", readme_text)
        self.assertIn("tdsqlite_amalgamation.c", readme_text)


if __name__ == "__main__":
    unittest.main()