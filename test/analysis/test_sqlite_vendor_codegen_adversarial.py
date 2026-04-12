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

from generate_tdsqlite_rename import collect_sqlite_identifiers


class SqliteVendorCodegenAdversarialTest(unittest.TestCase):
    def test_ignores_comments_strings_and_partial_identifier_overlap(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "/* sqlite3_step(sqlite3_stmt*) must stay in comments only */\n"
                "const char *header_name = \"sqlite3.h sqlite3_open_v2\";\n"
                "const char *other = \"notsqlite3_open_v2\";\n"
                "int notsqlite3_open_v2 = 0;\n"
                "int sqlite3_prepare_v3(sqlite3 *db, const char *sql);\n",
                "#define SQLITE_EXTENSION_INIT1 const sqlite3_api_routines *sqlite3_api = 0;\n",
            ]
        )

        self.assertEqual(
            [
                "sqlite3",
                "sqlite3_api",
                "sqlite3_api_routines",
                "sqlite3_prepare_v3",
            ],
            identifiers,
        )

    def test_handles_repeated_symbols_across_multiple_inputs_without_reordering_instability(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "typedef struct sqlite3 sqlite3;\nint sqlite3_open_v2(void);\n",
                "int sqlite3_open_v2(void);\ntypedef struct sqlite3 sqlite3;\n",
            ]
        )

        self.assertEqual(["sqlite3", "sqlite3_open_v2"], identifiers)

    def test_excludes_preprocessor_macro_names_that_flip_conditional_compilation(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "#define sqlite3Fts5Parser_ENGINEALWAYSONSTACK 1\n"
                "#ifndef sqlite3Fts5Parser_ENGINEALWAYSONSTACK\n"
                "static void *sqlite3Fts5ParserAlloc(void);\n"
                "#endif\n"
                "#define SQLITE_EXTENSION_INIT1 const sqlite3_api_routines *sqlite3_api = 0;\n",
            ]
        )

        self.assertIn("sqlite3Fts5ParserAlloc", identifiers)
        self.assertIn("sqlite3_api", identifiers)
        self.assertIn("sqlite3_api_routines", identifiers)
        self.assertNotIn("sqlite3Fts5Parser_ENGINEALWAYSONSTACK", identifiers)

    def test_keeps_public_api_names_when_another_input_uses_them_in_extension_macros(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "SQLITE_API int sqlite3_exec(sqlite3*, const char*, void*, void*, char**);\n"
                "SQLITE_API int sqlite3_open_v2(const char*, sqlite3**, int, const char*);\n",
                "#define sqlite3_exec sqlite3_api->exec\n"
                "#define sqlite3_open_v2 sqlite3_api->open_v2\n",
            ]
        )

        self.assertIn("sqlite3_exec", identifiers)
        self.assertIn("sqlite3_open_v2", identifiers)

    def test_excludes_non_parenthesized_defined_guards_at_end_of_preprocessor_line(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "#if defined sqlite3Parser_ENGINEALWAYSONSTACK\n"
                "int sqlite3_open_v2(void);\n"
                "#endif\n",
            ]
        )

        self.assertEqual(["sqlite3_open_v2"], identifiers)


if __name__ == "__main__":
    unittest.main()