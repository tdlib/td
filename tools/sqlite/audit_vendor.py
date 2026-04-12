# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import urllib.parse
from collections import Counter
from dataclasses import dataclass
from typing import Any

from generate_tdsqlite_rename import collect_sqlite_identifiers_from_files
from generate_tdsqlite_rename import render_rename_header


CATEGORY_IMPORTED_SQLCIPHER = "imported_sqlcipher"
CATEGORY_MECHANICAL_TDSQLITE_RENAME = "mechanical_tdsqlite_rename"
CATEGORY_TELEGRAM_BUILD_CONFIGURATION = "telegram_build_configuration"
CATEGORY_TELEGRAM_WRAPPER_POLICY = "telegram_wrapper_policy"
CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH = "unexpected_semantic_local_patch"

CATEGORY_ORDER = (
    CATEGORY_IMPORTED_SQLCIPHER,
    CATEGORY_MECHANICAL_TDSQLITE_RENAME,
    CATEGORY_TELEGRAM_BUILD_CONFIGURATION,
    CATEGORY_TELEGRAM_WRAPPER_POLICY,
    CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH,
)

SQLCIPHER_MARKERS = (
    "BEGIN SQLCIPHER",
    "sqlcipher",
    "SQLITE_HAS_CODEC",
    "sqlite3_key",
    "sqlite3_key_v2",
    "tdsqlite3_key",
    "tdsqlite3_key_v2",
    "sqlcipher_export",
    "cipher_compatibility",
)
WRAPPER_SQLCIPHER_FEATURES = (
    "PRAGMA key",
    "PRAGMA cipher_compatibility",
    "PRAGMA rekey",
    "SELECT sqlcipher_export",
)
COMPILE_DEFINITION_RE = re.compile(r"^\s+-D([A-Z0-9_]+(?:=[^\s)]+)?)\s*$", re.MULTILINE)
SQLITE_VERSION_RE = re.compile(r'^#define SQLITE_VERSION\s+"([^"]+)"', re.MULTILINE)
SQLITE_SOURCE_ID_RE = re.compile(r'^#define SQLITE_SOURCE_ID\s+"([^"]+)"', re.MULTILINE)
SQLITE_RENAME_TOKEN_RE = re.compile(r"(?<![A-Za-z0-9_])sqlite3(?!\.)")
SQLITE_CMAKE_RELATIVE_PATH = "sqlite/CMakeLists.txt"
SQLITE_VENDOR_RELATIVE_PREFIX = "sqlite/sqlite/"
WRAPPER_RELATIVE_PREFIX = "tddb/td/db/"
VENDOR_MANIFEST_RELATIVE_PATH = "sqlite/VENDOR.json"
RENAME_GENERATOR_RELATIVE_PATH = "tools/sqlite/generate_tdsqlite_rename.py"
UPDATE_VENDOR_SCRIPT_RELATIVE_PATH = "tools/sqlite/update_vendor.py"
MANIFEST_SCHEMA_VERSION = 1
DEFAULT_SQLCIPHER_RELEASE_URL_TEMPLATE = "https://github.com/sqlcipher/sqlcipher/releases/tag/{release_tag}"
DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE = "https://api.github.com/repos/sqlcipher/sqlcipher/tarball/{release_tag}"
IGNORED_USAGE_DIRECTORIES = {
    ".git",
    "build",
    "build-asan",
    "build-ninja",
    "build-stealth",
}
SEARCHABLE_SUFFIXES = {
    ".c",
    ".cc",
    ".cmake",
    ".cpp",
    ".h",
    ".hpp",
    ".inc",
    ".json",
    ".md",
    ".py",
    ".txt",
}
SQLITE3SESSION_USAGE_RE = re.compile(
    r'#include\s+[<"][^\n"]*sqlite3session\.h[>"]|\b(?:td)?sqlite3session_[A-Za-z0-9_]+\b'
)
SHA256_HEX_RE = re.compile(r"^[0-9a-f]{64}$")
IGNORED_SQLITE3SESSION_USAGE_PREFIXES = (
    "docs/",
    "sqlite/generated/",
    "sqlite/sqlite/",
    "sqlite/upstream/",
    "test/analysis/test_sqlite_vendor_audit",
    "test/analysis/test_sqlite_vendor_",
    "tools/sqlite/",
)
WRAPPER_HEADER_RELATIVE_PATHS = (
    "sqlite/sqlite/sqlite3.h",
    "sqlite/sqlite/sqlite3ext.h",
    "sqlite/sqlite/sqlite3session.h",
)
RENAME_GENERATOR_INPUT_FILENAMES = (
    "sqlite3.h",
    "sqlite3ext.h",
    "sqlite3session.h",
    "sqlite3.c",
)


@dataclass(frozen=True)
class ClassifiedEntry:
    category: str
    file_path: str
    old_line: str | None
    new_line: str | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "category": self.category,
            "file_path": self.file_path,
            "old_line": self.old_line,
            "new_line": self.new_line,
        }


@dataclass(frozen=True)
class AuditResult:
    category_counts: dict[str, int]
    unexplained_entries: list[dict[str, Any]]

    def to_dict(self) -> dict[str, Any]:
        return {
            "category_counts": self.category_counts,
            "unexplained_entries": self.unexplained_entries,
        }


def _read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _read_bytes(path: pathlib.Path) -> bytes:
    return path.read_bytes()


def _relative_path(path: pathlib.Path, repo_root: pathlib.Path) -> str:
    return path.resolve().relative_to(repo_root.resolve()).as_posix()


def compute_sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(_read_bytes(path)).hexdigest()


def _normalize_repo_root(repo_root: pathlib.Path | str) -> pathlib.Path:
    return pathlib.Path(repo_root).resolve()


def _rename_generator_input_paths(resolved_root: pathlib.Path) -> list[pathlib.Path]:
    return [resolved_root / "sqlite" / "upstream" / filename for filename in RENAME_GENERATOR_INPUT_FILENAMES]


def rename_generator_input_relpaths() -> list[str]:
    return [f"sqlite/upstream/{filename}" for filename in RENAME_GENERATOR_INPUT_FILENAMES]


def render_wrapper_header(upstream_name: str) -> str:
    return "\n".join(
        [
            "// SPDX-FileCopyrightText: Copyright 2026 telemt community",
            "// SPDX-License-Identifier: MIT",
            "// telemt: https://github.com/telemt",
            "// telemt: https://t.me/telemtrs",
            "",
            "#pragma once",
            "",
            '#include "../generated/tdsqlite_rename.h"',
            f'#include "../upstream/{upstream_name}"',
            "",
        ]
    )


def render_amalgamation_translation_unit() -> str:
    return "\n".join(
        [
            "// SPDX-FileCopyrightText: Copyright 2026 telemt community",
            "// SPDX-License-Identifier: MIT",
            "// telemt: https://github.com/telemt",
            "// telemt: https://t.me/telemtrs",
            "",
            "#include <stdint.h>",
            "",
            "#ifndef SQLITE_EXTRA_INIT",
            "#define SQLITE_EXTRA_INIT sqlcipher_extra_init",
            "#endif",
            "",
            "#ifndef SQLITE_EXTRA_SHUTDOWN",
            "#define SQLITE_EXTRA_SHUTDOWN sqlcipher_extra_shutdown",
            "#endif",
            "",
            '#include "generated/tdsqlite_rename.h"',
            '#include "upstream/sqlite3.c"',
            "",
        ]
    )


def render_expected_generated_files(repo_root: pathlib.Path | str) -> dict[str, str]:
    resolved_root = _normalize_repo_root(repo_root)
    rename_input_paths = _rename_generator_input_paths(resolved_root)
    expected_files = {
        "sqlite/generated/tdsqlite_rename.h": render_rename_header(
            collect_sqlite_identifiers_from_files(rename_input_paths), rename_input_paths
        ),
        "sqlite/sqlite/sqlite3.h": render_wrapper_header("sqlite3.h"),
        "sqlite/sqlite/sqlite3ext.h": render_wrapper_header("sqlite3ext.h"),
        "sqlite/sqlite/sqlite3session.h": render_wrapper_header("sqlite3session.h"),
        "sqlite/tdsqlite_amalgamation.c": render_amalgamation_translation_unit(),
    }
    return expected_files


def load_vendor_manifest(repo_root: pathlib.Path | str) -> dict[str, Any]:
    resolved_root = _normalize_repo_root(repo_root)
    manifest_path = resolved_root / VENDOR_MANIFEST_RELATIVE_PATH
    if not manifest_path.exists():
        raise FileNotFoundError(f"Missing vendor manifest: {manifest_path}")
    return json.loads(_read_text(manifest_path))


def write_vendor_manifest(repo_root: pathlib.Path | str, manifest: dict[str, Any]) -> None:
    resolved_root = _normalize_repo_root(repo_root)
    manifest_path = resolved_root / VENDOR_MANIFEST_RELATIVE_PATH
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _default_release_url(release_tag: str) -> str:
    return DEFAULT_SQLCIPHER_RELEASE_URL_TEMPLATE.format(release_tag=release_tag)


def _default_tarball_url(release_tag: str) -> str:
    return DEFAULT_SQLCIPHER_TARBALL_URL_TEMPLATE.format(release_tag=release_tag)


def _validate_manifest_url(value: Any, label: str) -> str | None:
    if not isinstance(value, str) or not value:
        return f"vendor manifest {label} must be a non-empty https URL"

    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme != "https":
        return f"vendor manifest {label} must use https"
    if parsed.username is not None or parsed.password is not None:
        return f"vendor manifest {label} must not contain embedded credentials"
    if parsed.port not in (None, 443):
        return f"vendor manifest {label} must use the default https port"
    if parsed.query or parsed.fragment:
        return f"vendor manifest {label} must not contain query or fragment components"

    hostname = parsed.hostname
    if hostname is None:
        return f"vendor manifest {label} must include a host"
    if not parsed.path or parsed.path == "/":
        return f"vendor manifest {label} must include a concrete path"

    return None


def _resolve_managed_repo_path(repo_root: pathlib.Path, relative_path: str) -> pathlib.Path:
    if not isinstance(relative_path, str) or not relative_path:
        raise ValueError("managed file path must be a non-empty repo-root-relative path")

    manifest_path = pathlib.PurePosixPath(relative_path)
    if manifest_path.is_absolute():
        raise ValueError("managed file path must be relative to the repo root")
    if not manifest_path.parts or any(part in ("", ".", "..") for part in manifest_path.parts):
        raise ValueError("managed file path must stay within the repo root")

    absolute_path = (repo_root / pathlib.Path(*manifest_path.parts)).resolve()
    if not absolute_path.is_relative_to(repo_root.resolve()):
        raise ValueError("managed file path must stay within the repo root")
    return absolute_path


def build_vendor_manifest(
    repo_root: pathlib.Path | str,
    release_tag: str,
    source_tarball_sha256: str,
    source_tarball_url: str | None = None,
    release_url: str | None = None,
) -> dict[str, Any]:
    if SHA256_HEX_RE.fullmatch(source_tarball_sha256) is None:
        raise ValueError("source_tarball_sha256 must be a 64-character lowercase hex digest")

    resolved_root = _normalize_repo_root(repo_root)
    report = build_phase1_report(resolved_root)
    managed_hash_paths = [
        report["vendor_paths"]["header"],
        report["vendor_paths"]["source"],
        "sqlite/upstream/sqlite3ext.h",
        "sqlite/upstream/sqlite3session.h",
        "sqlite/generated/tdsqlite_rename.h",
        *WRAPPER_HEADER_RELATIVE_PATHS,
        "sqlite/tdsqlite_amalgamation.c",
    ]
    hashes = {path: compute_sha256(resolved_root / path) for path in managed_hash_paths}
    return {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "vendor": {
            "base": "sqlcipher",
            "release_tag": release_tag,
            "release_url": release_url or _default_release_url(release_tag),
            "source_tarball_sha256": source_tarball_sha256,
            "source_tarball_url": source_tarball_url or _default_tarball_url(release_tag),
            "sqlite_source_id": report["sqlite_source_id"],
            "sqlite_version": report["sqlite_version"],
        },
        "build": {
            "compile_definitions": report["compile_definitions"],
        },
        "generation": {
            "rename_generator": RENAME_GENERATOR_RELATIVE_PATH,
            "rename_generator_inputs": rename_generator_input_relpaths(),
            "update_script": UPDATE_VENDOR_SCRIPT_RELATIVE_PATH,
            "wrapper_headers": list(WRAPPER_HEADER_RELATIVE_PATHS),
            "amalgamation_translation_unit": "sqlite/tdsqlite_amalgamation.c",
        },
        "hashes": {
            "sha256": hashes,
        },
    }


def verify_manifest_hashes(repo_root: pathlib.Path | str, manifest: dict[str, Any]) -> list[str]:
    resolved_root = _normalize_repo_root(repo_root)
    errors = []
    for relative_path, expected_hash in sorted(manifest.get("hashes", {}).get("sha256", {}).items()):
        if not isinstance(expected_hash, str) or SHA256_HEX_RE.fullmatch(expected_hash) is None:
            errors.append(f"invalid sha256 digest for {relative_path}: {expected_hash!r}")
            continue

        try:
            absolute_path = _resolve_managed_repo_path(resolved_root, relative_path)
        except ValueError as error:
            errors.append(f"invalid managed file path {relative_path}: {error}")
            continue

        if not absolute_path.exists():
            errors.append(f"missing managed file: {relative_path}")
            continue
        actual_hash = compute_sha256(absolute_path)
        if actual_hash != expected_hash:
            errors.append(
                f"hash mismatch for {relative_path}: expected {expected_hash}, got {actual_hash}"
            )
    return errors


def verify_generated_files(repo_root: pathlib.Path | str) -> list[str]:
    resolved_root = _normalize_repo_root(repo_root)
    errors = []
    for relative_path, expected_text in sorted(render_expected_generated_files(resolved_root).items()):
        absolute_path = resolved_root / relative_path
        if not absolute_path.exists():
            errors.append(f"missing generated file: {relative_path}")
            continue
        actual_text = _read_text(absolute_path)
        if actual_text != expected_text:
            errors.append(f"generated file is out of date: {relative_path}")
    return errors


def verify_vendor_manifest_metadata(repo_root: pathlib.Path | str, manifest: dict[str, Any]) -> list[str]:
    report = build_phase1_report(repo_root)
    errors = []
    vendor = manifest.get("vendor", {})
    build = manifest.get("build", {})
    generation = manifest.get("generation", {})
    release_tag = vendor.get("release_tag")

    if manifest.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        errors.append(
            f"unexpected vendor manifest schema version: {manifest.get('schema_version')}"
        )
    if vendor.get("base") != "sqlcipher":
        errors.append(f"vendor manifest vendor base drift: expected sqlcipher, got {vendor.get('base')}")
    if not isinstance(release_tag, str) or not release_tag:
        errors.append("vendor manifest release_tag must be a non-empty string")
    release_url_error = _validate_manifest_url(vendor.get("release_url"), "release_url")
    if release_url_error is not None:
        errors.append(release_url_error)
    source_tarball_url_error = _validate_manifest_url(
        vendor.get("source_tarball_url"), "source_tarball_url"
    )
    if source_tarball_url_error is not None:
        errors.append(source_tarball_url_error)
    if vendor.get("sqlite_version") != report["sqlite_version"]:
        errors.append(
            f"vendor manifest sqlite_version drift: expected {report['sqlite_version']}, got {vendor.get('sqlite_version')}"
        )
    if vendor.get("sqlite_source_id") != report["sqlite_source_id"]:
        errors.append("vendor manifest sqlite_source_id drift")
    if SHA256_HEX_RE.fullmatch(vendor.get("source_tarball_sha256", "")) is None:
        errors.append("vendor manifest source_tarball_sha256 must be a 64-character lowercase hex digest")
    if build.get("compile_definitions") != report["compile_definitions"]:
        errors.append("vendor manifest compile_definitions drift")
    if generation.get("rename_generator") != RENAME_GENERATOR_RELATIVE_PATH:
        errors.append("vendor manifest rename_generator path drift")
    if generation.get("update_script") != UPDATE_VENDOR_SCRIPT_RELATIVE_PATH:
        errors.append("vendor manifest update_script path drift")
    if generation.get("rename_generator_inputs") != rename_generator_input_relpaths():
        errors.append("vendor manifest rename_generator_inputs drift")
    if generation.get("wrapper_headers") != list(WRAPPER_HEADER_RELATIVE_PATHS):
        errors.append("vendor manifest wrapper_headers drift")
    if generation.get("amalgamation_translation_unit") != "sqlite/tdsqlite_amalgamation.c":
        errors.append("vendor manifest amalgamation_translation_unit drift")
    return errors


def verify_vendor_integrity(repo_root: pathlib.Path | str) -> list[str]:
    try:
        manifest = load_vendor_manifest(repo_root)
    except FileNotFoundError as error:
        return [str(error)]

    errors = []
    errors.extend(verify_vendor_manifest_metadata(repo_root, manifest))
    errors.extend(verify_manifest_hashes(repo_root, manifest))
    errors.extend(verify_generated_files(repo_root))
    return errors


def _apply_tdsqlite_prefix(line: str) -> str:
    return SQLITE_RENAME_TOKEN_RE.sub("tdsqlite3", line)


def is_mechanical_tdsqlite_rename(old_line: str, new_line: str) -> bool:
    transformed = _apply_tdsqlite_prefix(old_line)
    return transformed != old_line and transformed == new_line


def _is_build_configuration_path(file_path: str) -> bool:
    return file_path == SQLITE_CMAKE_RELATIVE_PATH


def _is_wrapper_policy_path(file_path: str) -> bool:
    return file_path.startswith(WRAPPER_RELATIVE_PREFIX)


def _contains_sqlcipher_marker(line: str) -> bool:
    lowered = line.lower()
    return any(marker.lower() in lowered for marker in SQLCIPHER_MARKERS)


def _classify_change(file_path: str, old_line: str | None, new_line: str | None) -> ClassifiedEntry:
    if _is_build_configuration_path(file_path):
        return ClassifiedEntry(CATEGORY_TELEGRAM_BUILD_CONFIGURATION, file_path, old_line, new_line)

    if _is_wrapper_policy_path(file_path):
        return ClassifiedEntry(CATEGORY_TELEGRAM_WRAPPER_POLICY, file_path, old_line, new_line)

    if old_line is not None and new_line is not None and is_mechanical_tdsqlite_rename(old_line, new_line):
        return ClassifiedEntry(CATEGORY_MECHANICAL_TDSQLITE_RENAME, file_path, old_line, new_line)

    if (old_line is not None and _contains_sqlcipher_marker(old_line)) or (
        new_line is not None and _contains_sqlcipher_marker(new_line)
    ):
        return ClassifiedEntry(CATEGORY_IMPORTED_SQLCIPHER, file_path, old_line, new_line)

    return ClassifiedEntry(CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH, file_path, old_line, new_line)


def _flush_removed_entries(
    file_path: str,
    pending_removed: list[str],
    classified_entries: list[ClassifiedEntry],
) -> None:
    while pending_removed:
        classified_entries.append(_classify_change(file_path, pending_removed.pop(0), None))


def classify_unified_diff(diff_text: str) -> AuditResult:
    current_file_path = "<unknown>"
    pending_removed: list[str] = []
    classified_entries: list[ClassifiedEntry] = []

    for raw_line in diff_text.splitlines():
        if raw_line.startswith("diff --git "):
            _flush_removed_entries(current_file_path, pending_removed, classified_entries)
            parts = raw_line.split()
            if len(parts) >= 4 and parts[3].startswith("b/"):
                current_file_path = parts[3][2:]
            continue

        if raw_line.startswith("+++ b/"):
            current_file_path = raw_line[6:]
            continue

        if raw_line.startswith("@@"):
            _flush_removed_entries(current_file_path, pending_removed, classified_entries)
            continue

        if raw_line.startswith("--- ") or raw_line.startswith("index "):
            continue

        if raw_line.startswith("-"):
            pending_removed.append(raw_line[1:])
            continue

        if raw_line.startswith("+"):
            new_line = raw_line[1:]
            if pending_removed:
                old_line = pending_removed.pop(0)
                classified_entries.append(_classify_change(current_file_path, old_line, new_line))
            else:
                classified_entries.append(_classify_change(current_file_path, None, new_line))
            continue

        _flush_removed_entries(current_file_path, pending_removed, classified_entries)

    _flush_removed_entries(current_file_path, pending_removed, classified_entries)

    category_counts = {category: 0 for category in CATEGORY_ORDER}
    unexplained_entries: list[dict[str, Any]] = []
    for entry in classified_entries:
        category_counts[entry.category] += 1
        if entry.category == CATEGORY_UNEXPECTED_SEMANTIC_LOCAL_PATCH:
            unexplained_entries.append(entry.to_dict())

    return AuditResult(category_counts=category_counts, unexplained_entries=unexplained_entries)


def _extract_required_match(pattern: re.Pattern[str], text: str, label: str) -> str:
    match = pattern.search(text)
    if match is None:
        raise ValueError(f"Missing required {label} marker")
    return match.group(1)


def _collect_compile_definitions(cmake_text: str) -> list[str]:
    return sorted(set(COMPILE_DEFINITION_RE.findall(cmake_text)))


def _collect_present_markers(text: str, markers: tuple[str, ...]) -> list[str]:
    present = []
    lowered_text = text.lower()
    for marker in markers:
        if marker.lower() in lowered_text:
            present.append(marker)
    return present


def _iter_searchable_files(repo_root: pathlib.Path):
    for path in repo_root.rglob("*"):
        if not path.is_file():
            continue

        relative_parts = path.relative_to(repo_root).parts
        if any(part in IGNORED_USAGE_DIRECTORIES for part in relative_parts):
            continue

        if path.suffix.lower() not in SEARCHABLE_SUFFIXES and path.name != "CMakeLists.txt":
            continue

        yield path


def _find_sqlite3session_external_usages(repo_root: pathlib.Path) -> list[str]:
    allowed_paths = {
        "sqlite/sqlite/sqlite3session.h",
        "sqlite/upstream/sqlite3session.h",
        SQLITE_CMAKE_RELATIVE_PATH,
    }
    usages = []
    for path in _iter_searchable_files(repo_root):
        relative_path = _relative_path(path, repo_root)
        if relative_path in allowed_paths:
            continue
        if any(relative_path.startswith(prefix) for prefix in IGNORED_SQLITE3SESSION_USAGE_PREFIXES):
            continue

        text = _read_text(path)
        if SQLITE3SESSION_USAGE_RE.search(text):
            usages.append(relative_path)

    return sorted(usages)


def _resolve_vendor_layout_paths(resolved_root: pathlib.Path) -> tuple[str, str, str]:
    candidates = (
        ("phase2_scaffold", "sqlite/upstream/sqlite3.h", "sqlite/upstream/sqlite3.c"),
        ("legacy_mutated_vendor", "sqlite/sqlite/sqlite3.h", "sqlite/sqlite/sqlite3.c"),
    )
    for layout_mode, header_relative_path, source_relative_path in candidates:
        if (resolved_root / header_relative_path).exists() and (resolved_root / source_relative_path).exists():
            return layout_mode, header_relative_path, source_relative_path
    raise FileNotFoundError("Unable to resolve SQLite vendor layout paths")


def build_phase1_report(repo_root: pathlib.Path | str) -> dict[str, Any]:
    resolved_root = _normalize_repo_root(repo_root)
    sqlite_cmake_path = resolved_root / SQLITE_CMAKE_RELATIVE_PATH
    vendor_layout_mode, header_relative_path, source_relative_path = _resolve_vendor_layout_paths(resolved_root)
    sqlite_header_path = resolved_root / header_relative_path
    sqlite_source_path = resolved_root / source_relative_path
    wrapper_sqlite_db_path = resolved_root / "tddb/td/db/SqliteDb.cpp"

    sqlite_cmake_text = _read_text(sqlite_cmake_path)
    sqlite_header_text = _read_text(sqlite_header_path)
    sqlite_source_text = _read_text(sqlite_source_path)
    wrapper_sqlite_db_text = _read_text(wrapper_sqlite_db_path)

    report = {
        "vendor_layout_mode": vendor_layout_mode,
        "vendor_paths": {
            "header": header_relative_path,
            "source": source_relative_path,
        },
        "sqlite_version": _extract_required_match(SQLITE_VERSION_RE, sqlite_header_text, "SQLITE_VERSION"),
        "sqlite_source_id": _extract_required_match(SQLITE_SOURCE_ID_RE, sqlite_header_text, "SQLITE_SOURCE_ID"),
        "compile_definitions": _collect_compile_definitions(sqlite_cmake_text),
        "sqlcipher_markers": {
            header_relative_path: _collect_present_markers(sqlite_header_text, SQLCIPHER_MARKERS),
            source_relative_path: _collect_present_markers(sqlite_source_text, SQLCIPHER_MARKERS),
        },
        "wrapper_sqlcipher_features": _collect_present_markers(wrapper_sqlite_db_text, WRAPPER_SQLCIPHER_FEATURES),
        "sqlite3session_external_usages": _find_sqlite3session_external_usages(resolved_root),
        "telegram_owned_customizations": [
            "mechanical tdsqlite3 rename",
            "sqlite CMake feature profile",
            "wrapper policy in tddb/td/db",
        ],
            "phase1_gap": (
                "pristine upstream SQLite and matching SQLCipher baseline are not yet imported into the workspace for semantic diff classification"
                if vendor_layout_mode == "legacy_mutated_vendor"
                else "phase-2 scaffold is in place and the current SQLCipher baseline is pinned by sqlite/VENDOR.json plus integrity checks"
            ),
    }
    return report


def render_markdown_audit(report: dict[str, Any]) -> str:
    compile_definitions = "\n".join(f"- {definition}" for definition in report["compile_definitions"])
    wrapper_features = "\n".join(f"- {feature}" for feature in report["wrapper_sqlcipher_features"])
    external_usages = report["sqlite3session_external_usages"]
    sqlite3session_status = "unused outside sqlite vendor tree and sqlite/CMakeLists.txt" if not external_usages else ", ".join(external_usages)
    header_path = report["vendor_paths"]["header"]
    source_path = report["vendor_paths"]["source"]
    header_markers = ", ".join(report["sqlcipher_markers"][header_path])
    source_markers = ", ".join(report["sqlcipher_markers"][source_path])

    return f"""# SQLite Vendor Baseline Audit

## Telegram-owned customizations

- mechanical tdsqlite3 rename
- SQLite build feature profile in sqlite/CMakeLists.txt
- wrapper policy in tddb/td/db

## SQLCipher-backed dependency delta

- {header_path} markers: {header_markers}
- {source_path} markers: {source_markers}
- wrapper SQLCipher features:
{wrapper_features}

## Baseline facts

- SQLite version: {report['sqlite_version']}
- SQLite source id: {report['sqlite_source_id']}
- sqlite3session.h status: {sqlite3session_status}

## Build profile

{compile_definitions}

## Remaining Phase 1 gap

- {report['phase1_gap']}
"""


def _json_dump(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit the current SQLite vendor state or classify a vendor diff.")
    parser.add_argument(
        "--repo-root",
        default=pathlib.Path(__file__).resolve().parents[2],
        type=pathlib.Path,
        help="Repository root used for the phase-1 baseline audit.",
    )
    parser.add_argument(
        "--diff-file",
        type=pathlib.Path,
        help="Optional unified diff file to classify into phase-1 vendor categories.",
    )
    parser.add_argument(
        "--format",
        choices=("json", "markdown"),
        default="json",
        help="Output format for the report or diff classification.",
    )
    parser.add_argument(
        "--check-integrity",
        action="store_true",
        help="Verify sqlite/VENDOR.json, managed file hashes, and generated sqlite sidecars.",
    )
    args = parser.parse_args(argv)

    if args.diff_file is not None:
        result = classify_unified_diff(_read_text(args.diff_file)).to_dict()
        print(_json_dump(result))
        return 0

    if args.check_integrity:
        errors = verify_vendor_integrity(args.repo_root)
        if args.format == "markdown":
            if errors:
                print("# SQLite Vendor Integrity Check\n\n" + "\n".join(f"- {error}" for error in errors))
            else:
                print("# SQLite Vendor Integrity Check\n\n- OK")
        else:
            print(_json_dump({"ok": not errors, "errors": errors}))
        return 0 if not errors else 1

    report = build_phase1_report(args.repo_root)
    if args.format == "markdown":
        print(render_markdown_audit(report))
    else:
        print(_json_dump(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())