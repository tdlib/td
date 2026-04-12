# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
from dataclasses import dataclass

from audit_vendor import DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE
from audit_vendor import build_vendor_manifest
from audit_vendor import compute_sha256
from audit_vendor import load_vendor_manifest
from audit_vendor import render_expected_generated_files
from audit_vendor import verify_vendor_integrity
from audit_vendor import write_vendor_manifest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ALLOWED_SOURCE_TARBALL_HOSTS = frozenset({"api.github.com", "github.com", "codeload.github.com"})
SHA256_HEX_RE = re.compile(r"^[0-9a-f]{64}$")
SQLCIPHER_SESSION_HEADER_RELATIVE_PATH = pathlib.Path("ext/session/sqlite3session.h")
UPSTREAM_COPY_MAP = {
    pathlib.Path("sqlite3.c"): pathlib.Path("sqlite/upstream/sqlite3.c"),
    pathlib.Path("sqlite3.h"): pathlib.Path("sqlite/upstream/sqlite3.h"),
    pathlib.Path("sqlite3ext.h"): pathlib.Path("sqlite/upstream/sqlite3ext.h"),
    SQLCIPHER_SESSION_HEADER_RELATIVE_PATH: pathlib.Path("sqlite/upstream/sqlite3session.h"),
}
GENERATED_SIDECAR_RELATIVE_PATHS = (
    pathlib.Path("sqlite/generated/tdsqlite_rename.h"),
    pathlib.Path("sqlite/sqlite/sqlite3.h"),
    pathlib.Path("sqlite/sqlite/sqlite3ext.h"),
    pathlib.Path("sqlite/sqlite/sqlite3session.h"),
    pathlib.Path("sqlite/tdsqlite_amalgamation.c"),
)
MANAGED_REPO_RELATIVE_PATHS = tuple(
    sorted(
        {
            *(destination_path.as_posix() for destination_path in UPSTREAM_COPY_MAP.values()),
            *(path.as_posix() for path in GENERATED_SIDECAR_RELATIVE_PATHS),
            "sqlite/VENDOR.json",
        }
    )
)


class VendorUpdateError(RuntimeError):
    pass


@dataclass(frozen=True)
class TclEnvironment:
    tclsh_path: pathlib.Path
    env: dict[str, str]


def _resolve_managed_repo_output_path(repo_root: pathlib.Path, relative_path: str) -> pathlib.Path:
    relative_repo_path = pathlib.PurePosixPath(relative_path)
    if relative_repo_path.is_absolute() or not relative_repo_path.parts:
        raise VendorUpdateError(f"managed output path must stay within the repository root: {relative_path}")
    if any(part in ("", ".", "..") for part in relative_repo_path.parts):
        raise VendorUpdateError(f"managed output path must stay within the repository root: {relative_path}")

    resolved_repo_root = repo_root.resolve()
    absolute_path = repo_root / pathlib.Path(*relative_repo_path.parts)
    current_path = resolved_repo_root
    for index, part in enumerate(relative_repo_path.parts):
        current_path = current_path / part
        if not current_path.exists():
            break
        if current_path.is_symlink():
            raise VendorUpdateError(f"managed output path must not use symlinks: {relative_path}")
        if not current_path.resolve().is_relative_to(resolved_repo_root):
            raise VendorUpdateError(f"managed output path escaped the repository root: {relative_path}")
        if index + 1 < len(relative_repo_path.parts) and not current_path.is_dir():
            raise VendorUpdateError(f"managed output parent is not a directory: {relative_path}")

    return absolute_path


def snapshot_managed_repo_outputs(repo_root: pathlib.Path) -> dict[str, bytes | None]:
    snapshot: dict[str, bytes | None] = {}
    for relative_path in MANAGED_REPO_RELATIVE_PATHS:
        absolute_path = _resolve_managed_repo_output_path(repo_root, relative_path)
        snapshot[relative_path] = absolute_path.read_bytes() if absolute_path.exists() else None
    return snapshot


def _remove_empty_parent_directories(path: pathlib.Path, stop_dir: pathlib.Path) -> None:
    current = path
    resolved_stop_dir = stop_dir.resolve()
    while current.exists() and current.is_dir() and current.resolve() != resolved_stop_dir:
        try:
            current.rmdir()
        except OSError:
            break
        current = current.parent


def restore_managed_repo_outputs(repo_root: pathlib.Path, snapshot: dict[str, bytes | None]) -> None:
    for relative_path, content in snapshot.items():
        absolute_path = _resolve_managed_repo_output_path(repo_root, relative_path)
        if content is None:
            if absolute_path.exists():
                if absolute_path.is_dir():
                    shutil.rmtree(absolute_path)
                else:
                    absolute_path.unlink()
                _remove_empty_parent_directories(absolute_path.parent, repo_root)
            continue

        absolute_path.parent.mkdir(parents=True, exist_ok=True)
        absolute_path.write_bytes(content)


def run_checked(command: list[str], cwd: pathlib.Path, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=str(cwd), env=env, check=True)


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def find_system_tclsh() -> pathlib.Path | None:
    candidates = ("tclsh", "tclsh8.6", "tclsh8.7")
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved is not None:
            return pathlib.Path(resolved).resolve()
    return None


def bootstrap_local_tcl(work_dir: pathlib.Path) -> TclEnvironment:
    apt_get = shutil.which("apt-get")
    dpkg_deb = shutil.which("dpkg-deb")
    if apt_get is None or dpkg_deb is None:
        raise VendorUpdateError(
            "tclsh is not available and local bootstrap requires both apt-get and dpkg-deb"
        )

    bootstrap_dir = work_dir / "local-tcl"
    bootstrap_dir.mkdir(parents=True, exist_ok=True)
    run_checked([apt_get, "download", "tcl8.6", "libtcl8.6"], bootstrap_dir)

    root_dir = bootstrap_dir / "root"
    if root_dir.exists():
        shutil.rmtree(root_dir)
    root_dir.mkdir(parents=True, exist_ok=True)
    for package in bootstrap_dir.glob("*.deb"):
        run_checked([dpkg_deb, "-x", str(package), str(root_dir)], bootstrap_dir)

    tclsh_candidates = [root_dir / "usr/bin/tclsh8.6", root_dir / "usr/bin/tclsh"]
    tclsh_path = next((candidate for candidate in tclsh_candidates if candidate.exists()), None)
    if tclsh_path is None:
        raise VendorUpdateError("local tcl bootstrap completed, but no tclsh binary was unpacked")

    env = os.environ.copy()
    env["PATH"] = str(tclsh_path.parent) + os.pathsep + env.get("PATH", "")
    lib_dirs = [
        root_dir / "usr/lib/x86_64-linux-gnu",
        root_dir / "usr/lib64",
        root_dir / "usr/lib",
    ]
    existing_lib_dirs = [directory for directory in lib_dirs if directory.exists()]
    if existing_lib_dirs:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(str(directory) for directory in existing_lib_dirs)
    tcl_library = root_dir / "usr/share/tcltk/tcl8.6"
    if tcl_library.exists():
        env["TCL_LIBRARY"] = str(tcl_library)
    env["TCLSH_CMD"] = str(tclsh_path)
    return TclEnvironment(tclsh_path=tclsh_path, env=env)


def resolve_tcl_environment(args: argparse.Namespace, work_dir: pathlib.Path) -> TclEnvironment:
    if args.tclsh is not None:
        tclsh_path = args.tclsh.resolve()
        if not tclsh_path.exists():
            raise VendorUpdateError(f"specified tclsh does not exist: {tclsh_path}")
        env = os.environ.copy()
        env["TCLSH_CMD"] = str(tclsh_path)
        env["PATH"] = str(tclsh_path.parent) + os.pathsep + env.get("PATH", "")
        return TclEnvironment(tclsh_path=tclsh_path, env=env)

    system_tclsh = find_system_tclsh()
    if system_tclsh is not None:
        env = os.environ.copy()
        env["TCLSH_CMD"] = str(system_tclsh)
        return TclEnvironment(tclsh_path=system_tclsh, env=env)

    return bootstrap_local_tcl(work_dir)


class _AllowlistedRedirectHandler(urllib.request.HTTPRedirectHandler):
    def __init__(self, allowed_hosts: set[str]) -> None:
        super().__init__()
        self._allowed_hosts = frozenset(allowed_hosts)

    def redirect_request(self, req, fp, code, msg, headers, newurl):  # type: ignore[override]
        validate_source_tarball_url(newurl, set(self._allowed_hosts))
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download_tarball(url: str, destination: pathlib.Path, allowed_hosts: set[str]) -> pathlib.Path:
    validated_url = validate_source_tarball_url(url, allowed_hosts)
    destination.parent.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(_AllowlistedRedirectHandler(allowed_hosts))
    with opener.open(validated_url) as response:
        validate_source_tarball_url(response.geturl(), allowed_hosts)
        with destination.open("wb") as output:
            shutil.copyfileobj(response, output)
    return destination


def _normalize_sha256(value: str, label: str) -> str:
    normalized_value = value.strip().lower()
    if SHA256_HEX_RE.fullmatch(normalized_value) is None:
        raise VendorUpdateError(f"{label} must be a 64-character lowercase hex sha256 digest")
    return normalized_value


def _normalize_host(host: str) -> str:
    return host.strip().lower().rstrip(".")


def _resolve_allowed_source_tarball_hosts(args: argparse.Namespace) -> set[str]:
    hosts = set(DEFAULT_ALLOWED_SOURCE_TARBALL_HOSTS)
    for host in getattr(args, "allow_source_host", []) or []:
        normalized_host = _normalize_host(host)
        if not normalized_host or any(character.isspace() for character in normalized_host):
            raise VendorUpdateError(f"unsafe allowlisted source host: {host!r}")
        hosts.add(normalized_host)
    return hosts


def validate_source_tarball_url(url: str, allowed_hosts: set[str]) -> str:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https":
        raise VendorUpdateError("source tarball URL must use https")
    if parsed.username is not None or parsed.password is not None:
        raise VendorUpdateError("source tarball URL must not contain embedded credentials")
    if parsed.port not in (None, 443):
        raise VendorUpdateError("source tarball URL must use the default https port")
    if parsed.query or parsed.fragment:
        raise VendorUpdateError("source tarball URL must not contain query or fragment components")

    hostname = parsed.hostname
    if hostname is None:
        raise VendorUpdateError("source tarball URL must include an allowlisted host")

    normalized_host = _normalize_host(hostname)
    if normalized_host not in allowed_hosts:
        raise VendorUpdateError(f"source tarball URL host is not allowlisted: {normalized_host}")
    if not parsed.path or parsed.path == "/":
        raise VendorUpdateError("source tarball URL must include a concrete archive path")

    return url


def resolve_source_tarball_sha256(
    args: argparse.Namespace, repo_root: pathlib.Path, tarball_url: str
) -> str:
    explicit_sha256 = getattr(args, "source_tarball_sha256", None)
    if explicit_sha256 is not None:
        return _normalize_sha256(explicit_sha256, "--source-tarball-sha256")

    try:
        manifest = load_vendor_manifest(repo_root)
    except FileNotFoundError:
        manifest = None

    if manifest is not None:
        vendor = manifest.get("vendor", {})
        if vendor.get("release_tag") == args.release_tag and vendor.get("source_tarball_url") == tarball_url:
            pinned_sha256 = vendor.get("source_tarball_sha256")
            if pinned_sha256 is None:
                raise VendorUpdateError(
                    "sqlite/VENDOR.json is missing vendor.source_tarball_sha256 for the requested release"
                )
            return _normalize_sha256(
                pinned_sha256, "sqlite/VENDOR.json vendor.source_tarball_sha256"
            )

    raise VendorUpdateError(
        "source tarball sha256 must be provided with --source-tarball-sha256 or pinned in sqlite/VENDOR.json"
    )


def verify_downloaded_tarball_sha256(tarball_path: pathlib.Path, expected_sha256: str) -> None:
    normalized_expected_sha256 = _normalize_sha256(expected_sha256, "source tarball sha256")
    actual_sha256 = compute_sha256(tarball_path)
    if actual_sha256 != normalized_expected_sha256:
        raise VendorUpdateError(
            "downloaded source tarball sha256 mismatch: "
            f"expected {normalized_expected_sha256}, got {actual_sha256}"
        )


def _tar_member_top_level_name(member_name: str) -> str:
    member_path = pathlib.PurePosixPath(member_name)
    if member_path.is_absolute() or not member_path.parts:
        raise VendorUpdateError(f"unsafe archive member path: {member_name}")

    top_level_name = member_path.parts[0]
    if top_level_name in (".", ".."):
        raise VendorUpdateError(f"unsafe archive member path: {member_name}")
    return top_level_name


def _safe_archive_relative_path(member_name: str, top_level_name: str) -> pathlib.Path | None:
    member_path = pathlib.PurePosixPath(member_name)
    if member_path.is_absolute():
        raise VendorUpdateError(f"unsafe archive member path: {member_name}")

    try:
        relative_path = member_path.relative_to(pathlib.PurePosixPath(top_level_name))
    except ValueError as error:
        raise VendorUpdateError(f"archive member escaped top-level directory: {member_name}") from error

    if not relative_path.parts:
        return None
    if any(part in ("", ".", "..") for part in relative_path.parts):
        raise VendorUpdateError(f"unsafe archive member path traversal: {member_name}")

    return pathlib.Path(*relative_path.parts)


def _extract_regular_member(archive: tarfile.TarFile, member: tarfile.TarInfo, destination_path: pathlib.Path) -> None:
    extracted_file = archive.extractfile(member)
    if extracted_file is None:
        raise VendorUpdateError(f"failed to read archive member: {member.name}")

    destination_path.parent.mkdir(parents=True, exist_ok=True)
    with extracted_file, destination_path.open("wb") as output:
        shutil.copyfileobj(extracted_file, output)


def extract_tarball(tarball_path: pathlib.Path, destination: pathlib.Path) -> pathlib.Path:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)

    with tarfile.open(tarball_path, "r:gz") as archive:
        members = archive.getmembers()
        top_level_names = {_tar_member_top_level_name(member.name) for member in members if member.name}
        if len(top_level_names) != 1:
            raise VendorUpdateError("expected a single top-level directory in the SQLCipher tarball")
        top_level_name = next(iter(top_level_names))
        for member in members:
            relative_name = _safe_archive_relative_path(member.name, top_level_name)
            if relative_name is None:
                continue

            destination_path = destination / relative_name
            if member.isdir():
                destination_path.mkdir(parents=True, exist_ok=True)
                continue
            if member.issym():
                raise VendorUpdateError(f"unsafe archive symlink member: {member.name}")
            if member.islnk():
                raise VendorUpdateError(f"unsafe archive hard link member: {member.name}")
            if not member.isreg():
                raise VendorUpdateError(f"unsupported archive member type: {member.name}")

            _extract_regular_member(archive, member, destination_path)
    return destination


def acquire_source_tree(
    args: argparse.Namespace, work_dir: pathlib.Path, repo_root: pathlib.Path = REPO_ROOT
) -> tuple[pathlib.Path, str, str]:
    tarball_url = args.source_tarball_url or DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE.format(
        release_tag=args.release_tag
    )
    allowed_hosts = _resolve_allowed_source_tarball_hosts(args)
    tarball_url = validate_source_tarball_url(tarball_url, allowed_hosts)
    source_tarball_sha256 = resolve_source_tarball_sha256(args, repo_root, tarball_url)

    if args.source_dir is not None:
        source_dir = args.source_dir.resolve()
        if not source_dir.exists():
            raise VendorUpdateError(f"source directory does not exist: {source_dir}")
        return source_dir, tarball_url, source_tarball_sha256

    tarball_path = download_tarball(tarball_url, work_dir / "sqlcipher.tar.gz", allowed_hosts)
    verify_downloaded_tarball_sha256(tarball_path, source_tarball_sha256)
    source_dir = extract_tarball(tarball_path, work_dir / "sqlcipher-src")
    return source_dir, tarball_url, source_tarball_sha256


def generate_sqlcipher_amalgamation(source_dir: pathlib.Path, tcl_env: TclEnvironment) -> None:
    if shutil.which("make") is None:
        raise VendorUpdateError("make is required to generate the SQLCipher amalgamation")
    if not (source_dir / "configure").exists():
        raise VendorUpdateError(f"source tree is missing configure: {source_dir}")

    run_checked(["./configure"], source_dir, env=tcl_env.env)
    run_checked(["make", "sqlite3.c", "sqlite3.h"], source_dir, env=tcl_env.env)


def copy_vendor_files(repo_root: pathlib.Path, source_dir: pathlib.Path) -> None:
    resolved_source_root = source_dir.resolve()
    for source_relative_path, destination_relative_path in UPSTREAM_COPY_MAP.items():
        source_path = source_dir / source_relative_path
        if not source_path.exists():
            raise VendorUpdateError(f"generated SQLCipher source is missing required file: {source_path}")

        resolved_source_path = source_path.resolve()
        if not resolved_source_path.is_relative_to(resolved_source_root):
            raise VendorUpdateError(f"unsafe required source path escaped source tree: {source_path}")
        if source_path.is_symlink():
            raise VendorUpdateError(f"required vendor input must be a regular file, not a symlink: {source_path}")
        if not source_path.is_file():
            raise VendorUpdateError(f"required vendor input must be a regular file: {source_path}")

        destination_path = _resolve_managed_repo_output_path(repo_root, destination_relative_path.as_posix())
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(resolved_source_path, destination_path)


def write_generated_sidecars(repo_root: pathlib.Path) -> None:
    for relative_path, content in render_expected_generated_files(repo_root).items():
        write_text(_resolve_managed_repo_output_path(repo_root, relative_path), content)


def run_small_verification_suite(repo_root: pathlib.Path) -> None:
    integrity_errors = verify_vendor_integrity(repo_root)
    if integrity_errors:
        raise VendorUpdateError("vendor integrity verification failed:\n- " + "\n- ".join(integrity_errors))
    run_checked(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "-s",
            "test/analysis",
            "-p",
            "test_sqlite*.py",
        ],
        repo_root,
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Refresh the vendored SQLCipher/SQLite baseline and local wrappers.")
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT, help="Repository root to update.")
    parser.add_argument("--release-tag", required=True, help="SQLCipher release tag to vendor, for example v4.14.0.")
    parser.add_argument("--source-dir", type=pathlib.Path, help="Optional existing SQLCipher source tree to reuse.")
    parser.add_argument("--source-tarball-url", help="Optional tarball URL override for the SQLCipher source tree.")
    parser.add_argument(
        "--source-tarball-sha256",
        help="Pinned SHA-256 for the source tarball. Required unless sqlite/VENDOR.json already pins the requested release and URL.",
    )
    parser.add_argument(
        "--allow-source-host",
        action="append",
        default=[],
        help="Additional HTTPS host allowed for --source-tarball-url downloads. May be specified more than once.",
    )
    parser.add_argument("--release-url", help="Optional release URL override written into sqlite/VENDOR.json.")
    parser.add_argument("--tclsh", type=pathlib.Path, help="Optional explicit path to the tclsh binary used for amalgamation generation.")
    parser.add_argument("--work-dir", type=pathlib.Path, help="Optional working directory used for tarball extraction and Tcl bootstrap.")
    parser.add_argument("--skip-verify", action="store_true", help="Skip the post-update Python integrity suite.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    repo_root = args.repo_root.resolve()

    if args.work_dir is not None:
        work_dir = args.work_dir.resolve()
        work_dir.mkdir(parents=True, exist_ok=True)
        cleanup_work_dir = None
    else:
        cleanup_work_dir = tempfile.TemporaryDirectory(prefix="tdsqlite-vendor-")
        work_dir = pathlib.Path(cleanup_work_dir.name)

    try:
        tcl_env = resolve_tcl_environment(args, work_dir)
        source_dir, tarball_url, source_tarball_sha256 = acquire_source_tree(args, work_dir, repo_root)
        generate_sqlcipher_amalgamation(source_dir, tcl_env)
        managed_output_snapshot = snapshot_managed_repo_outputs(repo_root)
        try:
            copy_vendor_files(repo_root, source_dir)
            write_generated_sidecars(repo_root)

            manifest = build_vendor_manifest(
                repo_root,
                release_tag=args.release_tag,
                source_tarball_sha256=source_tarball_sha256,
                source_tarball_url=tarball_url,
                release_url=args.release_url,
            )
            _resolve_managed_repo_output_path(repo_root, "sqlite/VENDOR.json")
            write_vendor_manifest(repo_root, manifest)

            if not args.skip_verify:
                run_small_verification_suite(repo_root)
        except Exception:
            restore_managed_repo_outputs(repo_root, managed_output_snapshot)
            raise
    finally:
        if cleanup_work_dir is not None:
            cleanup_work_dir.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())