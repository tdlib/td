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
from generate_tdsqlite_rename import render_rename_header


class SqliteVendorCodegenTest(unittest.TestCase):
    def test_collects_public_and_internal_sqlite_identifiers(self) -> None:
        identifiers = collect_sqlite_identifiers(
            [
                "typedef struct sqlite3 sqlite3;\n"
                "typedef struct sqlite3_stmt sqlite3_stmt;\n"
                "SQLITE_API int sqlite3_open_v2(const char*, sqlite3**, int, const char*);\n",
                "struct sqlite3_api_routines { const sqlite3_value *value; sqlite3_mutex *mutex; };\n"
                "int sqlite3session_create(sqlite3 *db, const char *zDb, void **ppSession);\n",
            ]
        )

        self.assertEqual(
            [
                "sqlite3",
                "sqlite3_api_routines",
                "sqlite3_mutex",
                "sqlite3_open_v2",
                "sqlite3_stmt",
                "sqlite3_value",
                "sqlite3session_create",
            ],
            identifiers,
        )

    def test_rendered_header_is_deterministic_and_deduplicated(self) -> None:
        rendered = render_rename_header(
            [
                "sqlite3_stmt",
                "sqlite3",
                "sqlite3_open_v2",
                "sqlite3",
            ],
            [pathlib.Path("sqlite/upstream/sqlite3.h"), pathlib.Path("sqlite/upstream/sqlite3.c")],
        )

        self.assertIn("#ifndef TDSQLITE_RENAME_H", rendered)
        self.assertIn("#define sqlite3 tdsqlite3", rendered)
        self.assertIn("#define sqlite3_open_v2 tdsqlite3_open_v2", rendered)
        self.assertIn("#define sqlite3_stmt tdsqlite3_stmt", rendered)
        self.assertEqual(1, rendered.count("#define sqlite3 tdsqlite3\n"))


if __name__ == "__main__":
    unittest.main()