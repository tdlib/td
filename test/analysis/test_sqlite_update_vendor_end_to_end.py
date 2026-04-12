# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

import update_vendor


MANAGED_OUTPUT_RELATIVE_PATHS = tuple(
    sorted(
        {
            *(path.as_posix() for path in update_vendor.UPSTREAM_COPY_MAP.values()),
            "sqlite/generated/tdsqlite_rename.h",
            "sqlite/sqlite/sqlite3.h",
            "sqlite/sqlite/sqlite3ext.h",
            "sqlite/sqlite/sqlite3session.h",
            "sqlite/tdsqlite_amalgamation.c",
            "sqlite/VENDOR.json",
        }
    )
)


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_minimal_repo_scaffold(repo_root: pathlib.Path) -> None:
    session_symbol = "sqlite3" + "session_legacy"

    write_text(
        repo_root / "sqlite" / "CMakeLists.txt",
        "target_compile_definitions(tdsqlite PRIVATE\n"
        "  -DSQLITE_HAS_CODEC\n"
        ")\n",
    )
    write_text(
        repo_root / "tddb" / "td" / "db" / "SqliteDb.cpp",
        "const char *kSqlcipher_features = \"PRAGMA key; PRAGMA cipher_compatibility; "
        "PRAGMA rekey; SELECT sqlcipher_export('encrypted');\";\n",
    )

    baseline_files = {
        "sqlite/upstream/sqlite3.c": "legacy source\n",
        "sqlite/upstream/sqlite3.h": (
            '#define SQLITE_VERSION "3.50.0"\n'
            '#define SQLITE_SOURCE_ID "legacy-source-id"\n'
            'typedef struct sqlite3 sqlite3;\n'
        ),
        "sqlite/upstream/sqlite3ext.h": "struct sqlite3_api_routines { int (*legacy)(void); };\n",
        "sqlite/upstream/sqlite3session.h": f"int {session_symbol}(void);\n",
        "sqlite/generated/tdsqlite_rename.h": "legacy rename layer\n",
        "sqlite/sqlite/sqlite3.h": "legacy wrapper sqlite3.h\n",
        "sqlite/sqlite/sqlite3ext.h": "legacy wrapper sqlite3ext.h\n",
        "sqlite/sqlite/sqlite3session.h": "legacy wrapper sqlite3session.h\n",
        "sqlite/tdsqlite_amalgamation.c": "legacy amalgamation\n",
        "sqlite/VENDOR.json": json.dumps({"schema_version": 0}, sort_keys=True) + "\n",
    }
    for relative_path, content in baseline_files.items():
        write_text(repo_root / relative_path, content)


def write_minimal_source_tree(source_dir: pathlib.Path) -> None:
    session_symbol = "sqlite3" + "session_create"

    write_text(
        source_dir / "sqlite3.h",
        '#define SQLITE_VERSION "3.51.3"\n'
        '#define SQLITE_SOURCE_ID "2026-03-13 source-id"\n'
        'typedef struct sqlite3 sqlite3;\n'
        'typedef struct sqlite3_stmt sqlite3_stmt;\n'
        'const char *sqlite3_libversion(void);\n'
        'int sqlite3_open_v2(const char *, sqlite3 **, int, const char *);\n'
        'int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);\n',
    )
    write_text(
        source_dir / "sqlite3.c",
        "/* BEGIN SQLCIPHER */\n"
        "int sqlite3_key(sqlite3 *db, const void *key, int key_len);\n"
        "int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int n, sqlite3_stmt **stmt, const char **tail);\n",
    )
    write_text(
        source_dir / "sqlite3ext.h",
        "struct sqlite3_api_routines {\n"
        "  int (*open_v2)(const char *, sqlite3 **, int, const char *);\n"
        "};\n",
    )
    write_text(
        source_dir / "ext" / "session" / "sqlite3session.h",
        f"int {session_symbol}(sqlite3 *db, const char *name, void **session);\n",
    )


def snapshot_managed_outputs(repo_root: pathlib.Path) -> dict[str, bytes | None]:
    snapshot: dict[str, bytes | None] = {}
    for relative_path in MANAGED_OUTPUT_RELATIVE_PATHS:
        absolute_path = repo_root / relative_path
        snapshot[relative_path] = absolute_path.read_bytes() if absolute_path.exists() else None
    return snapshot


class SqliteUpdateVendorEndToEndTest(unittest.TestCase):
    def test_main_persists_custom_https_override_urls_into_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            repo_root = temp_root / "repo"
            source_dir = temp_root / "source"
            write_minimal_repo_scaffold(repo_root)
            write_minimal_source_tree(source_dir)

            custom_tarball_url = "https://mirror.example/sqlcipher-v4.14.0.tar.gz"
            custom_release_url = "https://mirror.example/sqlcipher/releases/tag/v4.14.0"
            fake_tcl_env = update_vendor.TclEnvironment(pathlib.Path("/usr/bin/tclsh"), {})

            with mock.patch.object(update_vendor, "resolve_tcl_environment", return_value=fake_tcl_env), mock.patch.object(
                update_vendor,
                "acquire_source_tree",
                return_value=(source_dir, custom_tarball_url, "7" * 64),
            ), mock.patch.object(update_vendor, "generate_sqlcipher_amalgamation"):
                exit_code = update_vendor.main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "--release-tag",
                        "v4.14.0",
                        "--source-tarball-url",
                        custom_tarball_url,
                        "--allow-source-host",
                        "mirror.example",
                        "--source-tarball-sha256",
                        "7" * 64,
                        "--release-url",
                        custom_release_url,
                        "--skip-verify",
                    ]
                )

            self.assertEqual(0, exit_code)
            manifest = json.loads((repo_root / "sqlite" / "VENDOR.json").read_text(encoding="utf-8"))
            self.assertEqual(custom_tarball_url, manifest["vendor"]["source_tarball_url"])
            self.assertEqual(custom_release_url, manifest["vendor"]["release_url"])
            self.assertEqual("7" * 64, manifest["vendor"]["source_tarball_sha256"])
            self.assertIn("#define sqlite3 tdsqlite3", (repo_root / "sqlite" / "generated" / "tdsqlite_rename.h").read_text(encoding="utf-8"))

    def test_main_restores_managed_outputs_when_manifest_regeneration_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            repo_root = temp_root / "repo"
            source_dir = temp_root / "source"
            write_minimal_repo_scaffold(repo_root)
            write_minimal_source_tree(source_dir)
            baseline_snapshot = snapshot_managed_outputs(repo_root)
            fake_tcl_env = update_vendor.TclEnvironment(pathlib.Path("/usr/bin/tclsh"), {})

            with mock.patch.object(update_vendor, "resolve_tcl_environment", return_value=fake_tcl_env), mock.patch.object(
                update_vendor,
                "acquire_source_tree",
                return_value=(source_dir, "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v4.14.0", "8" * 64),
            ), mock.patch.object(update_vendor, "generate_sqlcipher_amalgamation"), mock.patch.object(
                update_vendor,
                "build_vendor_manifest",
                side_effect=update_vendor.VendorUpdateError("manifest regeneration failed"),
            ):
                with self.assertRaisesRegex(update_vendor.VendorUpdateError, "manifest regeneration failed"):
                    update_vendor.main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "--release-tag",
                            "v4.14.0",
                            "--source-tarball-sha256",
                            "8" * 64,
                            "--skip-verify",
                        ]
                    )

            self.assertEqual(
                baseline_snapshot,
                snapshot_managed_outputs(repo_root),
                msg="failed manifest regeneration must not leave copied upstream files or generated sidecars behind",
            )

    def test_main_restores_managed_outputs_when_post_write_verification_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            repo_root = temp_root / "repo"
            source_dir = temp_root / "source"
            write_minimal_repo_scaffold(repo_root)
            write_minimal_source_tree(source_dir)
            baseline_snapshot = snapshot_managed_outputs(repo_root)
            fake_tcl_env = update_vendor.TclEnvironment(pathlib.Path("/usr/bin/tclsh"), {})

            with mock.patch.object(update_vendor, "resolve_tcl_environment", return_value=fake_tcl_env), mock.patch.object(
                update_vendor,
                "acquire_source_tree",
                return_value=(source_dir, "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v4.14.0", "9" * 64),
            ), mock.patch.object(update_vendor, "generate_sqlcipher_amalgamation"), mock.patch.object(
                update_vendor,
                "run_small_verification_suite",
                side_effect=update_vendor.VendorUpdateError("post-write verification failed"),
            ):
                with self.assertRaisesRegex(update_vendor.VendorUpdateError, "post-write verification failed"):
                    update_vendor.main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "--release-tag",
                            "v4.14.0",
                            "--source-tarball-sha256",
                            "9" * 64,
                        ]
                    )

            self.assertEqual(
                baseline_snapshot,
                snapshot_managed_outputs(repo_root),
                msg="failed post-write verification must roll the managed vendor files back to the last known-good state",
            )


if __name__ == "__main__":
    unittest.main()