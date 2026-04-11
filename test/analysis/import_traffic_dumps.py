#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import unicodedata


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TRAFFIC_DUMPS_ROOT = REPO_ROOT / "docs" / "Samples" / "Traffic dumps"
UNSORTED_ROOT = TRAFFIC_DUMPS_ROOT / "Unsorted"
ANALYSIS_ROOT = REPO_ROOT / "test" / "analysis"
IMPORTED_FIXTURES_ROOT = ANALYSIS_ROOT / "fixtures" / "imported"
IMPORT_MANIFEST_PATH = IMPORTED_FIXTURES_ROOT / "import_manifest.json"

CLIENTHELLO_EXTRACTOR = ANALYSIS_ROOT / "extract_client_hello_fixtures.py"
SERVERHELLO_EXTRACTOR = ANALYSIS_ROOT / "extract_server_hello_fixtures.py"

CAPTURE_EXTENSIONS = {".pcap", ".pcapng"}

PLATFORM_LAYOUT = {
    "android": {
        "captures_dir": "Android",
        "fixtures_dir": "android",
        "os_family": "android",
        "device_class": "mobile",
        "profile_fragment": "android",
    },
    "ios": {
        "captures_dir": "iOS",
        "fixtures_dir": "ios",
        "os_family": "ios",
        "device_class": "mobile",
        "profile_fragment": "ios",
    },
    "linux_desktop": {
        "captures_dir": "Linux, desktop",
        "fixtures_dir": "linux_desktop",
        "os_family": "linux",
        "device_class": "desktop",
        "profile_fragment": "linux",
    },
    "macos": {
        "captures_dir": "macOS",
        "fixtures_dir": "macos",
        "os_family": "macos",
        "device_class": "desktop",
        "profile_fragment": "macos",
    },
    "windows": {
        "captures_dir": "Windows",
        "fixtures_dir": "windows",
        "os_family": "windows",
        "device_class": "desktop",
        "profile_fragment": "windows",
    },
}

TERM_REPLACEMENTS = {
    "яндекс.браузер": "yandex browser",
    "яндекс браузер": "yandex browser",
    "яндекс": "yandex",
    "андроид": "android",
    "версия": "version",
    "аппарат": "device",
    "самсунг": "samsung",
}

BROWSER_PATTERNS: list[tuple[str, tuple[str, ...]]] = [
    ("adblock_browser", ("adblock browser",)),
    ("samsung_internet", ("samsung internet browser", "samsung internet")),
    ("yandex", ("yabrowser", "yabrows", "ya browser", "yandex browser", "yandex")),
    ("librewolf", ("librewolf",)),
    ("ironfox", ("ironfox",)),
    ("firefox", ("firefox", "auto fi", " fi")),
    ("chrome", ("google chrome",)),
    ("chrome", (" chrome ", "chrome")),
    ("chromium", ("chromium",)),
    ("edge", ("edge",)),
    ("safari", ("safari",)),
    ("brave", ("brave",)),
    ("cromite", ("cromite",)),
    ("vivaldi", ("vivaldi",)),
    ("opera", ("opera",)),
    ("maxthon", ("maxthon",)),
    ("zen", ("zen browser", "zen")),
]

OS_PREFIX_PATTERNS: list[tuple[str, tuple[str, ...]]] = [
    ("android", ("android",)),
    ("ios", ("ios",)),
    ("macos", ("macos",)),
    ("windows", ("windows",)),
    ("linux_desktop", ("linux", "arch linux", "cachyos")),
]

GENERIC_NOISE_WORDS = {
    "auto",
    "browser",
    "official",
    "head",
    "null",
}

PROFILE_ID_VERSION = "imported-capture-corpus-v1"


@dataclass(frozen=True)
class CapturePlan:
    source_path: str
    target_capture_path: str
    platform_key: str
    captures_dir: str
    fixtures_dir: str
    os_family: str
    device_class: str
    browser_alias: str
    browser_version_slug: str
    os_version_slug: str
    profile_id: str
    clienthello_out: str
    serverhello_out: str
    clienthello_scenario_id: str
    serverhello_scenario_id: str
    user_os_token: str
    user_browser_token: str
    auto_os_token: str
    auto_browser_token: str
    selected_os_token: str
    selected_browser_token: str
    selected_os_source: str
    selected_browser_source: str


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def repo_relative(path: pathlib.Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def normalize_free_text(text: str) -> str:
    normalized = text.strip().lower().replace("_", " ")
    for source, replacement in TERM_REPLACEMENTS.items():
        normalized = normalized.replace(source, replacement)
    normalized = re.sub(r"\s+", " ", normalized)
    return normalized.strip()


def slugify_ascii(text: str) -> str:
    normalized = normalize_free_text(text)
    ascii_text = unicodedata.normalize("NFKD", normalized).encode("ascii", "ignore").decode("ascii")
    ascii_text = re.sub(r"[^a-z0-9]+", "_", ascii_text).strip("_")
    ascii_text = re.sub(r"_+", "_", ascii_text)
    return ascii_text


def classify_platform_key(text: str) -> str | None:
    normalized = normalize_free_text(text)
    if not normalized:
        return None
    for platform_key, patterns in OS_PREFIX_PATTERNS:
        if any(pattern in normalized for pattern in patterns):
            return platform_key
    return None


def detect_browser_alias(text: str) -> str | None:
    normalized = normalize_free_text(text)
    if not normalized or normalized == "null":
        return None
    normalized = normalized.replace("auto ", " ")
    padded = f" {normalized} "
    for browser_alias, patterns in BROWSER_PATTERNS:
        if any(pattern in padded or pattern == normalized for pattern in patterns):
            return browser_alias
    return None


def split_capture_fields(stem: str) -> list[str]:
    return [field.strip(" _") for field in stem.split(",") if field.strip(" _")]


def find_first_auto_field(fields: list[str], detector) -> str:
    for field in fields:
        normalized = normalize_free_text(field)
        if not normalized.startswith("auto"):
            continue
        if detector(field):
            return field
    return ""


def strip_known_prefix_words(text: str, prefixes: tuple[str, ...]) -> str:
    normalized = normalize_free_text(text)
    for prefix in prefixes:
        if normalized.startswith(prefix + " "):
            normalized = normalized[len(prefix) + 1 :]
            break
        if normalized == prefix:
            return ""
    return normalized


def compress_version_slug(text: str) -> str:
    slug = slugify_ascii(text)
    if not slug:
        return ""
    parts = [part for part in slug.split("_") if part and (re.search(r"\d", part) or part not in GENERIC_NOISE_WORDS)]
    if not parts:
        return ""
    return "_".join(parts)


def extract_os_version_slug(token: str, platform_key: str) -> str:
    prefixes = {
        "android": ("android",),
        "ios": ("ios",),
        "macos": ("macos",),
        "windows": ("windows",),
        "linux_desktop": ("arch linux", "linux", "cachyos"),
    }[platform_key]
    stripped = strip_known_prefix_words(token, prefixes)
    return compress_version_slug(stripped)


def extract_browser_version_slug(token: str, browser_alias: str | None) -> str:
    stripped = normalize_free_text(token)
    if browser_alias:
        for alias, patterns in BROWSER_PATTERNS:
            if alias != browser_alias:
                continue
            for pattern in patterns:
                candidate = pattern.strip()
                stripped = re.sub(rf"\b{re.escape(candidate)}\b", " ", stripped)
    stripped = stripped.replace("auto ", " ")
    return compress_version_slug(stripped)


def build_profile_base(browser_alias: str, browser_version_slug: str, platform_key: str, os_version_slug: str) -> str:
    platform_fragment = PLATFORM_LAYOUT[platform_key]["profile_fragment"]
    browser_fragment = browser_alias
    if browser_version_slug:
        browser_fragment = f"{browser_fragment}{browser_version_slug}"
    os_fragment = platform_fragment
    if os_version_slug:
        os_fragment = f"{os_fragment}{os_version_slug}"
    return slugify_ascii(f"{browser_fragment}_{os_fragment}") or "capture"


def derive_capture_plan(capture_path: pathlib.Path) -> CapturePlan:
    fields = split_capture_fields(capture_path.stem)
    user_os_token = fields[0] if len(fields) > 0 else ""
    user_browser_token = fields[1] if len(fields) > 1 else ""
    auto_os_token = find_first_auto_field(fields[2:], classify_platform_key)
    auto_browser_token = find_first_auto_field(fields[2:], detect_browser_alias)

    platform_key = (
        classify_platform_key(user_os_token)
        or classify_platform_key(auto_os_token)
        or classify_platform_key(capture_path.stem)
    )
    if platform_key is None:
        raise ValueError(f"unable to classify platform from capture name: {capture_path.name}")

    browser_alias = (
        detect_browser_alias(user_browser_token)
        or detect_browser_alias(auto_browser_token)
        or detect_browser_alias(capture_path.stem)
        or "unknown_browser"
    )
    if browser_alias == "unknown_browser":
        raise ValueError(f"unable to classify browser from capture name: {capture_path.name}")

    selected_os_token = user_os_token if classify_platform_key(user_os_token) else auto_os_token or user_os_token or capture_path.stem
    selected_os_source = "user" if classify_platform_key(user_os_token) else ("auto" if auto_os_token else "fallback")

    selected_browser_token = user_browser_token
    selected_browser_source = "user"
    if not detect_browser_alias(user_browser_token):
        if auto_browser_token:
            selected_browser_token = auto_browser_token
            selected_browser_source = "auto"
        else:
            selected_browser_token = user_browser_token or capture_path.stem
            selected_browser_source = "fallback"

    browser_version_slug = extract_browser_version_slug(user_browser_token or selected_browser_token, browser_alias)
    if not browser_version_slug and selected_browser_token != user_browser_token:
        browser_version_slug = extract_browser_version_slug(selected_browser_token, browser_alias)
    os_version_slug = extract_os_version_slug(selected_os_token, platform_key)

    profile_base = build_profile_base(browser_alias, browser_version_slug, platform_key, os_version_slug)
    suffix = hashlib.sha1(capture_path.stem.encode("utf-8")).hexdigest()[:8]
    profile_id = f"{profile_base}_{suffix}"

    platform_layout = PLATFORM_LAYOUT[platform_key]
    target_capture_path = TRAFFIC_DUMPS_ROOT / platform_layout["captures_dir"] / capture_path.name
    clienthello_out = IMPORTED_FIXTURES_ROOT / "clienthello" / platform_layout["fixtures_dir"] / f"{profile_id}.clienthello.json"
    serverhello_out = IMPORTED_FIXTURES_ROOT / "serverhello" / platform_layout["fixtures_dir"] / f"{profile_id}.serverhello.json"

    return CapturePlan(
        source_path=str(capture_path),
        target_capture_path=str(target_capture_path),
        platform_key=platform_key,
        captures_dir=platform_layout["captures_dir"],
        fixtures_dir=platform_layout["fixtures_dir"],
        os_family=platform_layout["os_family"],
        device_class=platform_layout["device_class"],
        browser_alias=browser_alias,
        browser_version_slug=browser_version_slug,
        os_version_slug=os_version_slug,
        profile_id=profile_id,
        clienthello_out=str(clienthello_out),
        serverhello_out=str(serverhello_out),
        clienthello_scenario_id=f"imported_{profile_id}_clienthello",
        serverhello_scenario_id=f"imported_{profile_id}_serverhello",
        user_os_token=user_os_token,
        user_browser_token=user_browser_token,
        auto_os_token=auto_os_token,
        auto_browser_token=auto_browser_token,
        selected_os_token=selected_os_token,
        selected_browser_token=selected_browser_token,
        selected_os_source=selected_os_source,
        selected_browser_source=selected_browser_source,
    )


def iter_unsorted_captures(unsorted_root: pathlib.Path) -> list[pathlib.Path]:
    return sorted(
        path
        for path in unsorted_root.iterdir()
        if path.is_file() and path.suffix.lower() in CAPTURE_EXTENSIONS and ":Zone.Identifier" not in path.name
    )


def zone_identifier_path(capture_path: pathlib.Path) -> pathlib.Path:
    return capture_path.with_name(f"{capture_path.name}:Zone.Identifier")


def run_command(argv: list[str]) -> tuple[bool, str]:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        stderr = result.stderr.strip()
        stdout = result.stdout.strip()
        combined = stderr if stderr else stdout
        return False, combined or f"command failed with exit code {result.returncode}"
    return True, result.stdout.strip()


def sha256_prefix(path: pathlib.Path, prefix_len: int = 8) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as infile:
        for chunk in iter(lambda: infile.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()[:prefix_len]


def disambiguate_target_path(source_path: pathlib.Path, target_path: pathlib.Path) -> pathlib.Path:
    if not target_path.exists():
        return target_path
    suffix = sha256_prefix(source_path)
    candidate = target_path.with_name(f"{target_path.stem}__{suffix}{target_path.suffix}")
    if candidate == source_path:
        return candidate
    if candidate.exists():
        raise FileExistsError(f"disambiguated target capture already exists: {candidate}")
    return candidate


def move_capture(plan: CapturePlan, dry_run: bool) -> tuple[pathlib.Path, list[str]]:
    source_path = pathlib.Path(plan.source_path)
    target_path = pathlib.Path(plan.target_capture_path)
    notes: list[str] = []

    if source_path.resolve() == target_path.resolve():
        return target_path, notes

    resolved_target_path = disambiguate_target_path(source_path, target_path)
    if resolved_target_path != target_path:
        notes.append(
            f"disambiguated target {repo_relative(target_path)} -> {repo_relative(resolved_target_path)}"
        )

    if dry_run:
        notes.append(f"dry-run: move {repo_relative(source_path)} -> {repo_relative(resolved_target_path)}")
        return resolved_target_path, notes

    resolved_target_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(source_path), str(resolved_target_path))

    sidecar_source = zone_identifier_path(source_path)
    if sidecar_source.exists():
        sidecar_target = zone_identifier_path(resolved_target_path)
        if sidecar_target.exists():
            raise FileExistsError(f"target sidecar already exists: {sidecar_target}")
        shutil.move(str(sidecar_source), str(sidecar_target))
    return resolved_target_path, notes


def extract_clienthello(plan: CapturePlan, capture_path: pathlib.Path, route_mode: str, dry_run: bool) -> tuple[bool, str]:
    clienthello_out = pathlib.Path(plan.clienthello_out)
    argv = [
        sys.executable,
        str(CLIENTHELLO_EXTRACTOR),
        "--pcap",
        str(capture_path),
        "--out",
        str(clienthello_out),
        "--profile-id",
        plan.profile_id,
        "--source-kind",
        "browser_capture",
        "--scenario-id",
        plan.clienthello_scenario_id,
        "--device-class",
        plan.device_class,
        "--os-family",
        plan.os_family,
        "--route-mode",
        route_mode,
    ]
    if dry_run:
        return True, f"dry-run: {' '.join(argv)}"
    clienthello_out.parent.mkdir(parents=True, exist_ok=True)
    return run_command(argv)


def extract_serverhello(plan: CapturePlan, capture_path: pathlib.Path, route_mode: str, dry_run: bool) -> tuple[bool, str]:
    serverhello_out = pathlib.Path(plan.serverhello_out)
    argv = [
        sys.executable,
        str(SERVERHELLO_EXTRACTOR),
        "--pcap",
        str(capture_path),
        "--route-mode",
        route_mode,
        "--scenario",
        plan.serverhello_scenario_id,
        "--family",
        plan.profile_id,
        "--out",
        str(serverhello_out),
        "--source-kind",
        "browser_capture",
    ]
    if dry_run:
        return True, f"dry-run: {' '.join(argv)}"
    serverhello_out.parent.mkdir(parents=True, exist_ok=True)
    return run_command(argv)


def load_manifest(path: pathlib.Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        return {}
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        return {}
    result: dict[str, dict] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        capture_path = entry.get("capture_path")
        if isinstance(capture_path, str) and capture_path:
            result[capture_path] = entry
    return result


def build_manifest_entry(
    plan: CapturePlan,
    capture_path: pathlib.Path,
    route_mode: str,
    clienthello_status: tuple[bool, str] | None,
    serverhello_status: tuple[bool, str] | None,
) -> dict:
    capture_path_relative = repo_relative(capture_path)
    clienthello_ok, clienthello_message = clienthello_status if clienthello_status is not None else (False, "not-run")
    serverhello_ok, serverhello_message = serverhello_status if serverhello_status is not None else (False, "not-run")
    return {
        "capture_path": capture_path_relative,
        "profile_id": plan.profile_id,
        "platform_key": plan.platform_key,
        "captures_dir": plan.captures_dir,
        "fixtures_dir": plan.fixtures_dir,
        "browser_alias": plan.browser_alias,
        "browser_version_slug": plan.browser_version_slug,
        "os_version_slug": plan.os_version_slug,
        "device_class": plan.device_class,
        "os_family": plan.os_family,
        "route_mode": route_mode,
        "user_os_token": plan.user_os_token,
        "user_browser_token": plan.user_browser_token,
        "auto_os_token": plan.auto_os_token,
        "auto_browser_token": plan.auto_browser_token,
        "selected_os_token": plan.selected_os_token,
        "selected_browser_token": plan.selected_browser_token,
        "selected_os_source": plan.selected_os_source,
        "selected_browser_source": plan.selected_browser_source,
        "artifacts": {
            "clienthello": repo_relative(pathlib.Path(plan.clienthello_out)),
            "serverhello": repo_relative(pathlib.Path(plan.serverhello_out)),
        },
        "clienthello": {
            "ok": clienthello_ok,
            "message": clienthello_message,
        },
        "serverhello": {
            "ok": serverhello_ok,
            "message": serverhello_message,
        },
    }


def write_manifest(path: pathlib.Path, entries: dict[str, dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": PROFILE_ID_VERSION,
        "generated_at_utc": utc_now(),
        "entries": [entries[key] for key in sorted(entries)],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_plan(plan: CapturePlan) -> None:
    summary = {
        "source": repo_relative(pathlib.Path(plan.source_path)),
        "target": repo_relative(pathlib.Path(plan.target_capture_path)),
        "platform": plan.platform_key,
        "browser": plan.browser_alias,
        "profile_id": plan.profile_id,
        "selected_os_source": plan.selected_os_source,
        "selected_browser_source": plan.selected_browser_source,
    }
    print(json.dumps(summary, ensure_ascii=True, sort_keys=True))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Move unsorted traffic dumps into canonical platform directories and generate imported fixtures."
    )
    parser.add_argument("--unsorted-root", default=str(UNSORTED_ROOT), help="Directory containing unsorted captures")
    parser.add_argument("--capture", action="append", help="Explicit pcap/pcapng path to import or re-import; can be repeated")
    parser.add_argument("--route-mode", default="non_ru_egress", help="Route mode for generated imported fixtures")
    parser.add_argument("--dry-run", action="store_true", help="Print the import plan without moving files or generating fixtures")
    parser.add_argument("--skip-serverhello", action="store_true", help="Generate only ClientHello fixtures")
    parser.add_argument("--limit", type=int, default=0, help="Optional max number of captures to process")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    captures: list[pathlib.Path] = []
    if args.capture:
        captures = [pathlib.Path(value).resolve() for value in args.capture]
        for capture_path in captures:
            if not capture_path.exists():
                raise SystemExit(f"explicit capture does not exist: {capture_path}")
            if capture_path.suffix.lower() not in CAPTURE_EXTENSIONS:
                raise SystemExit(f"explicit capture must be a pcap/pcapng file: {capture_path}")
    else:
        unsorted_root = pathlib.Path(args.unsorted_root).resolve()
        if not unsorted_root.exists():
            raise SystemExit(f"unsorted root does not exist: {unsorted_root}")
        captures = iter_unsorted_captures(unsorted_root)

    if args.limit > 0:
        captures = captures[: args.limit]

    if not captures:
        print("no unsorted captures found")
        return 0

    manifest_entries = load_manifest(IMPORT_MANIFEST_PATH)
    failures: list[str] = []

    for capture_path in captures:
        try:
            plan = derive_capture_plan(capture_path)
        except Exception as exc:
            failures.append(f"plan[{capture_path.name}]: {exc}")
            continue

        print_plan(plan)

        try:
            target_capture_path, _ = move_capture(plan, args.dry_run)
        except Exception as exc:
            failures.append(f"move[{capture_path.name}]: {exc}")
            continue

        try:
            effective_plan = derive_capture_plan(target_capture_path)
        except Exception as exc:
            failures.append(f"effective-plan[{target_capture_path.name}]: {exc}")
            continue

        clienthello_status = extract_clienthello(effective_plan, target_capture_path, args.route_mode, args.dry_run)
        if not clienthello_status[0]:
            failures.append(f"clienthello[{target_capture_path.name}]: {clienthello_status[1]}")

        serverhello_status: tuple[bool, str] | None = None
        if not args.skip_serverhello and clienthello_status[0]:
            serverhello_status = extract_serverhello(effective_plan, target_capture_path, args.route_mode, args.dry_run)
            if not serverhello_status[0]:
                failures.append(f"serverhello[{target_capture_path.name}]: {serverhello_status[1]}")

        manifest_entry = build_manifest_entry(
            effective_plan,
            target_capture_path,
            args.route_mode,
            clienthello_status,
            serverhello_status,
        )
        manifest_entries[manifest_entry["capture_path"]] = manifest_entry

    if not args.dry_run:
        write_manifest(IMPORT_MANIFEST_PATH, manifest_entries)

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())