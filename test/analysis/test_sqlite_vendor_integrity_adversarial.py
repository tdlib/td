# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import hashlib
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

from audit_vendor import DEFAULT_SQLCIPHER_RELEASE_URL_TEMPLATE
from audit_vendor import DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE
from audit_vendor import MANIFEST_SCHEMA_VERSION
from audit_vendor import RENAME_GENERATOR_RELATIVE_PATH
from audit_vendor import UPDATE_VENDOR_SCRIPT_RELATIVE_PATH
from audit_vendor import WRAPPER_HEADER_RELATIVE_PATHS
from audit_vendor import rename_generator_input_relpaths
from audit_vendor import verify_manifest_hashes
from audit_vendor import verify_vendor_manifest_metadata


EXPECTED_MANAGED_HASH_PATHS = (
    "sqlite/upstream/sqlite3.c",
    "sqlite/upstream/sqlite3.h",
    "sqlite/upstream/sqlite3ext.h",
    "sqlite/upstream/sqlite3session.h",
    "sqlite/generated/tdsqlite_rename.h",
    *WRAPPER_HEADER_RELATIVE_PATHS,
    "sqlite/tdsqlite_amalgamation.c",
)


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def make_vendor_manifest(*, base: str = "sqlcipher", source_tarball_url: str | None = None) -> dict:
    release_tag = "v4.14.0"
    return {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "vendor": {
            "base": base,
            "release_tag": release_tag,
            "release_url": DEFAULT_SQLCIPHER_RELEASE_URL_TEMPLATE.format(release_tag=release_tag),
            "source_tarball_sha256": "a" * 64,
            "source_tarball_url": source_tarball_url
            or DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE.format(release_tag=release_tag),
            "sqlite_source_id": "source-id",
            "sqlite_version": "3.51.3",
        },
        "build": {
            "compile_definitions": ["SQLITE_HAS_CODEC"],
        },
        "generation": {
            "rename_generator": RENAME_GENERATOR_RELATIVE_PATH,
            "rename_generator_inputs": rename_generator_input_relpaths(),
            "update_script": UPDATE_VENDOR_SCRIPT_RELATIVE_PATH,
            "wrapper_headers": list(WRAPPER_HEADER_RELATIVE_PATHS),
            "amalgamation_translation_unit": "sqlite/tdsqlite_amalgamation.c",
        },
        "hashes": {
            "sha256": {path: "b" * 64 for path in EXPECTED_MANAGED_HASH_PATHS},
        },
    }


def make_phase1_report() -> dict:
    return {
        "vendor_paths": {
            "header": "sqlite/upstream/sqlite3.h",
            "source": "sqlite/upstream/sqlite3.c",
        },
        "sqlite_version": "3.51.3",
        "sqlite_source_id": "source-id",
        "compile_definitions": ["SQLITE_HAS_CODEC"],
    }


class SqliteVendorIntegrityAdversarialTest(unittest.TestCase):
    def test_verify_manifest_hashes_reports_missing_and_modified_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = pathlib.Path(temp_dir)
            upstream_path = repo_root / "sqlite" / "upstream" / "sqlite3.h"
            upstream_path.parent.mkdir(parents=True, exist_ok=True)
            upstream_path.write_text("#define SQLITE_VERSION \"3.51.3\"\n", encoding="utf-8")

            manifest = {
                "hashes": {
                    "sha256": {
                        "sqlite/upstream/sqlite3.h": sha256_text("#define SQLITE_VERSION \"3.51.2\"\n"),
                        "sqlite/generated/tdsqlite_rename.h": sha256_text("placeholder\n"),
                    }
                }
            }

            errors = verify_manifest_hashes(repo_root, manifest)

            self.assertEqual(2, len(errors))
            self.assertTrue(any("sqlite/upstream/sqlite3.h" in error and "hash mismatch" in error for error in errors))
            self.assertTrue(any("sqlite/generated/tdsqlite_rename.h" in error and "missing managed file" in error for error in errors))

    def test_verify_manifest_hashes_rejects_paths_that_escape_repo_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            repo_root = temp_root / "repo"
            repo_root.mkdir(parents=True, exist_ok=True)
            escaped_path = temp_root / "escaped.txt"
            escaped_path.write_text("outside repo\n", encoding="utf-8")

            manifest = {
                "hashes": {
                    "sha256": {
                        "../../escaped.txt": sha256_text("outside repo\n"),
                    }
                }
            }

            errors = verify_manifest_hashes(repo_root, manifest)

            self.assertTrue(
                any("../../escaped.txt" in error and "repo root" in error for error in errors),
                msg="manifest-managed paths must stay inside the repository root",
            )

    def test_verify_manifest_hashes_rejects_invalid_sha256_digests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = pathlib.Path(temp_dir)
            managed_path = repo_root / "sqlite" / "upstream" / "sqlite3.h"
            managed_path.parent.mkdir(parents=True, exist_ok=True)
            managed_path.write_text("#define SQLITE_VERSION \"3.51.3\"\n", encoding="utf-8")

            manifest = {
                "hashes": {
                    "sha256": {
                        "sqlite/upstream/sqlite3.h": "not-a-digest",
                    }
                }
            }

            errors = verify_manifest_hashes(repo_root, manifest)

            self.assertTrue(
                any("sqlite/upstream/sqlite3.h" in error and "sha256" in error for error in errors),
                msg="manifest-managed hashes must themselves be pinned to valid sha256 digests",
            )

    def test_verify_vendor_manifest_metadata_rejects_unexpected_vendor_base(self) -> None:
        manifest = make_vendor_manifest(base="sqlite")

        with mock.patch("audit_vendor.build_phase1_report", return_value=make_phase1_report()):
            errors = verify_vendor_manifest_metadata(pathlib.Path("/tmp/repo"), manifest)

        self.assertTrue(
            any("vendor base" in error for error in errors),
            msg="integrity checks must pin the expected SQLCipher vendor base",
        )

    def test_verify_vendor_manifest_metadata_rejects_unsafe_source_tarball_url(self) -> None:
        manifest = make_vendor_manifest(source_tarball_url="http://evil.example/sqlcipher.tar.gz")

        with mock.patch("audit_vendor.build_phase1_report", return_value=make_phase1_report()):
            errors = verify_vendor_manifest_metadata(pathlib.Path("/tmp/repo"), manifest)

        self.assertTrue(
            any("source_tarball_url" in error for error in errors),
            msg="integrity checks must reject unsafe manifest tarball URLs",
        )

    def test_verify_vendor_manifest_metadata_rejects_release_url_with_embedded_credentials(self) -> None:
        manifest = make_vendor_manifest()
        manifest["vendor"]["release_url"] = (
            "https://user:pass@mirror.example/sqlcipher/releases/tag/v4.14.0"
        )

        with mock.patch("audit_vendor.build_phase1_report", return_value=make_phase1_report()):
            errors = verify_vendor_manifest_metadata(pathlib.Path("/tmp/repo"), manifest)

        self.assertTrue(
            any("release_url" in error for error in errors),
            msg="integrity checks must reject unsafe manifest release URLs",
        )

    def test_verify_vendor_manifest_metadata_rejects_missing_required_managed_hash_pin(self) -> None:
        manifest = make_vendor_manifest()
        del manifest["hashes"]["sha256"]["sqlite/upstream/sqlite3.c"]

        with mock.patch("audit_vendor.build_phase1_report", return_value=make_phase1_report()):
            errors = verify_vendor_manifest_metadata(pathlib.Path("/tmp/repo"), manifest)

        self.assertTrue(
            any("hashes.sha256" in error and "sqlite/upstream/sqlite3.c" in error for error in errors),
            msg="integrity checks must fail when a required managed vendor file is not pinned in sqlite/VENDOR.json",
        )

    def test_verify_vendor_manifest_metadata_rejects_unexpected_managed_hash_path(self) -> None:
        manifest = make_vendor_manifest()
        manifest["hashes"]["sha256"]["README.md"] = "c" * 64

        with mock.patch("audit_vendor.build_phase1_report", return_value=make_phase1_report()):
            errors = verify_vendor_manifest_metadata(pathlib.Path("/tmp/repo"), manifest)

        self.assertTrue(
            any("hashes.sha256" in error and "README.md" in error for error in errors),
            msg="integrity checks must reject manifest hash pins for files outside the managed SQLite vendor surface",
        )


if __name__ == "__main__":
    unittest.main()