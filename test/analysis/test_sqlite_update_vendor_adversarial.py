# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import argparse
import io
import pathlib
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

import update_vendor


class FakeResponse(io.BytesIO):
    def __init__(self, payload: bytes, final_url: str) -> None:
        super().__init__(payload)
        self._final_url = final_url

    def geturl(self) -> str:
        return self._final_url

    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        self.close()
        return False


def create_tarball(path: pathlib.Path, entries: list[dict[str, object]]) -> None:
    with tarfile.open(path, "w:gz") as archive:
        for entry in entries:
            info = tarfile.TarInfo(str(entry["name"]))
            info.type = entry.get("type", tarfile.REGTYPE)
            info.mode = 0o644

            if info.type == tarfile.DIRTYPE:
                archive.addfile(info)
                continue

            if info.type in (tarfile.SYMTYPE, tarfile.LNKTYPE):
                info.linkname = str(entry["linkname"])
                archive.addfile(info)
                continue

            content = entry.get("content", b"")
            if isinstance(content, str):
                content = content.encode("utf-8")
            info.size = len(content)
            archive.addfile(info, io.BytesIO(content))


def populate_required_source_tree(source_dir: pathlib.Path) -> None:
    for relative_path in update_vendor.UPSTREAM_COPY_MAP:
        absolute_path = source_dir / relative_path
        absolute_path.parent.mkdir(parents=True, exist_ok=True)
        absolute_path.write_text(f"payload for {relative_path.as_posix()}\n", encoding="utf-8")


class SqliteUpdateVendorAdversarialTest(unittest.TestCase):
    def test_download_tarball_rejects_redirected_final_url_outside_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            destination = pathlib.Path(temp_dir) / "sqlcipher.tar.gz"
            allowed_hosts = update_vendor._resolve_allowed_source_tarball_hosts(
                argparse.Namespace(allow_source_host=[])
            )
            fake_response = FakeResponse(
                b"tarball payload",
                "https://evil.example/sqlcipher-v4.14.0.tar.gz",
            )
            fake_opener = mock.Mock()
            fake_opener.open.return_value = fake_response

            with mock.patch.object(update_vendor.urllib.request, "build_opener", return_value=fake_opener), mock.patch.object(
                update_vendor.urllib.request, "urlopen", return_value=fake_response
            ):
                with self.assertRaisesRegex(update_vendor.VendorUpdateError, "allowlisted|host"):
                    update_vendor.download_tarball(
                        "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v4.14.0",
                        destination,
                        allowed_hosts,
                    )

            self.assertFalse(destination.exists(), msg="rejected redirect must not write a tarball to disk")

    def test_download_tarball_accepts_allowlisted_final_url(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            destination = pathlib.Path(temp_dir) / "sqlcipher.tar.gz"
            allowed_hosts = update_vendor._resolve_allowed_source_tarball_hosts(
                argparse.Namespace(allow_source_host=[])
            )
            fake_response = FakeResponse(
                b"tarball payload",
                "https://codeload.github.com/sqlcipher/sqlcipher/legacy.tar.gz/refs/tags/v4.14.0",
            )
            fake_opener = mock.Mock()
            fake_opener.open.return_value = fake_response

            with mock.patch.object(update_vendor.urllib.request, "build_opener", return_value=fake_opener), mock.patch.object(
                update_vendor.urllib.request, "urlopen", return_value=fake_response
            ):
                written_path = update_vendor.download_tarball(
                    "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v4.14.0",
                    destination,
                    allowed_hosts,
                )

            self.assertEqual(destination, written_path)
            self.assertEqual(b"tarball payload", destination.read_bytes())

    def test_snapshot_managed_repo_outputs_rejects_symlinked_managed_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            repo_root = root / "repo"
            outside_target = root / "outside-vendor.json"
            managed_path = repo_root / "sqlite" / "VENDOR.json"

            outside_target.write_text("outside manifest\n", encoding="utf-8")
            managed_path.parent.mkdir(parents=True, exist_ok=True)
            managed_path.symlink_to(outside_target)

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "symlink|managed output|repo"):
                update_vendor.snapshot_managed_repo_outputs(repo_root)

    def test_verify_downloaded_tarball_sha256_rejects_digest_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            tarball_path = pathlib.Path(temp_dir) / "sqlcipher.tar.gz"
            tarball_path.write_bytes(b"sqlcipher payload")

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "sha256|digest"):
                update_vendor.verify_downloaded_tarball_sha256(tarball_path, "0" * 64)

    def test_resolve_source_tarball_sha256_rejects_missing_pin_for_unknown_release(self) -> None:
        args = argparse.Namespace(
            source_tarball_sha256=None,
            source_tarball_url="https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v0.0.0-test",
            release_tag="v0.0.0-test",
        )

        with self.assertRaisesRegex(update_vendor.VendorUpdateError, "sha256|pinned"):
            update_vendor.resolve_source_tarball_sha256(
                args,
                REPO_ROOT,
                "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/v0.0.0-test",
            )

    def test_acquire_source_tree_rejects_unsafe_source_tarball_urls_before_download(self) -> None:
        unsafe_urls = (
            "file:///etc/passwd",
            "http://127.0.0.1/sqlcipher.tar.gz",
            "https://169.254.169.254/sqlcipher.tar.gz",
            "https://evil.example/sqlcipher.tar.gz",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            work_dir = pathlib.Path(temp_dir)
            for url in unsafe_urls:
                args = argparse.Namespace(
                    source_dir=None,
                    source_tarball_url=url,
                    source_tarball_sha256=None,
                    release_tag="v4.14.0",
                )

                with self.subTest(url=url), mock.patch.object(
                    update_vendor, "download_tarball", side_effect=AssertionError("unsafe URL reached download")
                ) as download_tarball:
                    with self.assertRaisesRegex(update_vendor.VendorUpdateError, "https|host|unsafe|allow"):
                        update_vendor.acquire_source_tree(args, work_dir)
                    download_tarball.assert_not_called()

    def test_acquire_source_tree_rejects_embedded_credentials_and_non_default_https_ports(self) -> None:
        unsafe_urls = (
            "https://user:pass@api.github.com/repos/sqlcipher/sqlcipher/tarball/v4.14.0",
            "https://api.github.com:444/repos/sqlcipher/sqlcipher/tarball/v4.14.0",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            work_dir = pathlib.Path(temp_dir)
            for url in unsafe_urls:
                args = argparse.Namespace(
                    source_dir=None,
                    source_tarball_url=url,
                    source_tarball_sha256=None,
                    release_tag="v4.14.0",
                    allow_source_host=[],
                )

                with self.subTest(url=url), mock.patch.object(
                    update_vendor, "download_tarball", side_effect=AssertionError("unsafe URL reached download")
                ) as download_tarball:
                    with self.assertRaisesRegex(update_vendor.VendorUpdateError, "credentials|port"):
                        update_vendor.acquire_source_tree(args, work_dir)
                    download_tarball.assert_not_called()

    def test_acquire_source_tree_rejects_malformed_allowlisted_host_entries(self) -> None:
        args = argparse.Namespace(
            source_dir=None,
            source_tarball_url="https://mirror.example/sqlcipher.tar.gz",
            source_tarball_sha256=None,
            release_tag="v4.14.0",
            allow_source_host=["mirror example"],
        )

        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            update_vendor, "download_tarball", side_effect=AssertionError("unsafe allowlist reached download")
        ) as download_tarball:
            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "allowlisted source host"):
                update_vendor.acquire_source_tree(args, pathlib.Path(temp_dir))
            download_tarball.assert_not_called()

    def test_extract_tarball_rejects_member_that_escapes_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            tarball_path = root / "malicious.tar.gz"
            destination = root / "dest"
            escaped_path = root / "escape.txt"

            create_tarball(
                tarball_path,
                [
                    {"name": "sqlcipher-src", "type": tarfile.DIRTYPE},
                    {"name": "sqlcipher-src/../escape.txt", "content": b"owned"},
                ],
            )

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "unsafe|path traversal"):
                update_vendor.extract_tarball(tarball_path, destination)

            self.assertFalse(escaped_path.exists(), msg="malicious archive must not write outside destination")

    def test_extract_tarball_rejects_symbolic_link_members(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            tarball_path = root / "symlink.tar.gz"
            destination = root / "dest"

            create_tarball(
                tarball_path,
                [
                    {"name": "sqlcipher-src", "type": tarfile.DIRTYPE},
                    {
                        "name": "sqlcipher-src/sqlite3.h",
                        "type": tarfile.SYMTYPE,
                        "linkname": "../../outside-header.h",
                    },
                ],
            )

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "symlink|unsupported|unsafe"):
                update_vendor.extract_tarball(tarball_path, destination)

            self.assertFalse((destination / "sqlite3.h").exists())

    def test_extract_tarball_rejects_hard_link_members(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            tarball_path = root / "hardlink.tar.gz"
            destination = root / "dest"

            create_tarball(
                tarball_path,
                [
                    {"name": "sqlcipher-src", "type": tarfile.DIRTYPE},
                    {"name": "sqlcipher-src/sqlite3.c", "content": b"select 1;"},
                    {
                        "name": "sqlcipher-src/sqlite3.h",
                        "type": tarfile.LNKTYPE,
                        "linkname": "sqlcipher-src/sqlite3.c",
                    },
                ],
            )

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "hard link|unsupported|unsafe"):
                update_vendor.extract_tarball(tarball_path, destination)

    def test_copy_vendor_files_rejects_symlinked_required_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            repo_root = root / "repo"
            source_dir = root / "source"
            escape_source = root / "escape.txt"
            escape_source.write_text("outside payload\n", encoding="utf-8")

            populate_required_source_tree(source_dir)
            symlink_target = source_dir / "sqlite3.h"
            symlink_target.unlink()
            symlink_target.symlink_to(escape_source)

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "regular file|symlink|unsafe"):
                update_vendor.copy_vendor_files(repo_root, source_dir)

            self.assertFalse((repo_root / "sqlite" / "upstream" / "sqlite3.h").exists())

    def test_copy_vendor_files_rejects_required_sources_reached_through_symlinked_parent(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            repo_root = root / "repo"
            source_dir = root / "source"
            outside_ext_dir = root / "outside-ext"
            outside_session_path = outside_ext_dir / "session" / "sqlite3session.h"

            populate_required_source_tree(source_dir)
            outside_session_path.parent.mkdir(parents=True, exist_ok=True)
            outside_session_path.write_text("outside session header\n", encoding="utf-8")

            shutil_target = source_dir / "ext"
            if shutil_target.exists():
                for child in sorted(shutil_target.rglob("*"), reverse=True):
                    if child.is_file() or child.is_symlink():
                        child.unlink()
                    else:
                        child.rmdir()
                shutil_target.rmdir()
            shutil_target.symlink_to(outside_ext_dir)

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "escaped source tree|unsafe"):
                update_vendor.copy_vendor_files(repo_root, source_dir)

            self.assertFalse((repo_root / "sqlite" / "upstream" / "sqlite3session.h").exists())

    def test_copy_vendor_files_rejects_symlinked_managed_repo_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            repo_root = root / "repo"
            source_dir = root / "source"
            outside_target = root / "outside-sqlite3.h"
            managed_destination = repo_root / "sqlite" / "upstream" / "sqlite3.h"

            populate_required_source_tree(source_dir)
            outside_target.write_text("do not overwrite\n", encoding="utf-8")
            managed_destination.parent.mkdir(parents=True, exist_ok=True)
            managed_destination.symlink_to(outside_target)

            with self.assertRaisesRegex(update_vendor.VendorUpdateError, "symlink|managed output|repo"):
                update_vendor.copy_vendor_files(repo_root, source_dir)

            self.assertEqual("do not overwrite\n", outside_target.read_text(encoding="utf-8"))
