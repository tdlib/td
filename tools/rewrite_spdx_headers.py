#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_PREFIXES = (
    "docs/Samples/JA3/",
    "docs/Samples/JA4/",
    "docs/Samples/scrapy-impersonate/",
    "docs/Samples/utls-code/",
    "docs/Samples/xray-core-code/",
    "docs/Standards/",
)

UPSTREAM_COPYRIGHT = (
    "Copyright Aliaksei Levin (levlam@telegram.org), "
    "Arseny Smirnov (arseny30@gmail.com) 2014-2026"
)
TELEMT_COPYRIGHT = "Copyright 2026 telemt community"
TELEMT_URLS = (
    "telemt: https://github.com/telemt",
    "telemt: https://t.me/telemtrs",
)
LICENSE_MARKERS = (
    "SPDX-",
    "telemt:",
    "Distributed under the Boost Software License",
    "file LICENSE_1_0.txt",
    "http://www.boost.org/LICENSE_1_0.txt",
    "Copyright Aliaksei Levin",
    "Copyright 2026 telemt community",
)
SLASH_COMMENT_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".ipp",
    ".js",
    ".ts",
}
HASH_COMMENT_EXTENSIONS = {
    ".cmake",
    ".gitignore",
    ".py",
    ".sh",
    ".toml",
    ".yaml",
    ".yml",
}
HTML_BLOCK_EXTENSIONS = {
    ".html",
    ".md",
}
HASH_COMMENT_FILENAMES = {
    "CMakeLists.txt",
    "Doxyfile",
}


def run_git(*args: str, check: bool = True) -> str:
    completed = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        check=check,
        text=True,
        capture_output=True,
    )
    return completed.stdout


def in_scope(path: str) -> bool:
    return not any(path.startswith(prefix) for prefix in EXCLUDED_PREFIXES)


def tracked_paths() -> set[str]:
    return {path for path in run_git("ls-files").splitlines() if path}


def current_changed_paths() -> set[str]:
    changed = set()
    for args in (("diff", "--name-only", "HEAD"), ("diff", "--cached", "--name-only", "HEAD")):
        for path in run_git(*args, check=False).splitlines():
            if path:
                changed.add(path)
    return changed


def david_touched_paths(author_pattern: str, tracked: set[str]) -> set[str]:
    touched = set()
    for path in run_git("log", "--all", f"--author={author_pattern}", "--name-only", "--format=").splitlines():
        if path and path in tracked:
            touched.add(path)
    return touched | {path for path in current_changed_paths() if path in tracked}


def effective_reference_path(path: str) -> str:
    if path.endswith(".license"):
        sibling = path[: -len(".license")]
        if (REPO_ROOT / sibling).exists():
            return sibling
    return path


def effective_target_path(path: str) -> str:
    reference_path = effective_reference_path(path)
    if reference_path.endswith(".json"):
        return f"{reference_path}.license"
    return path if not path.endswith(".json") else f"{path}.license"


def file_exists_in_head(path: str) -> bool:
    return subprocess.run(
        ["git", "cat-file", "-e", f"HEAD:{path}"],
        cwd=REPO_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def added_by_author(path: str, author_email: str) -> bool:
    if not file_exists_in_head(path):
        return True
    first_add = run_git("log", "--follow", "--diff-filter=A", "--format=%ae", "--", path, check=False)
    authors = [line.strip() for line in first_add.splitlines() if line.strip()]
    return bool(authors) and authors[-1] == author_email


def target_license_lines(is_new_file: bool) -> list[str]:
    if is_new_file:
        return [
            f"SPDX-FileCopyrightText: {TELEMT_COPYRIGHT}",
            "SPDX-License-Identifier: MIT",
            *TELEMT_URLS,
        ]
    return [
        f"SPDX-FileCopyrightText: {UPSTREAM_COPYRIGHT}",
        f"SPDX-FileCopyrightText: {TELEMT_COPYRIGHT}",
        "SPDX-License-Identifier: BSL-1.0 AND MIT",
        *TELEMT_URLS,
    ]


def comment_style_for(path: str) -> str | None:
    pure_path = Path(path)
    if path.endswith(".license"):
        return "plain"
    if pure_path.suffix == ".json":
        return "plain"
    if pure_path.name in HASH_COMMENT_FILENAMES or pure_path.suffix in HASH_COMMENT_EXTENSIONS:
        return "hash"
    if pure_path.suffix in SLASH_COMMENT_EXTENSIONS:
        return "slash"
    if pure_path.suffix in HTML_BLOCK_EXTENSIONS:
        return "html"
    return None


def is_license_line(content: str) -> bool:
    return any(marker in content for marker in LICENSE_MARKERS)


def strip_line_comment_header(text: str, prefix: str) -> tuple[str, str]:
    lines = text.splitlines(keepends=True)
    shebang = ""
    index = 0
    if prefix == "#" and lines and lines[0].startswith("#!"):
        shebang = lines[0]
        index = 1
    start = index
    matched = False
    while index < len(lines):
        stripped = lines[index].strip()
        if not stripped:
            if matched:
                index += 1
                while index < len(lines) and not lines[index].strip():
                    index += 1
            break
        if not stripped.startswith(prefix):
            break
        content = stripped[len(prefix) :].strip()
        if not content or is_license_line(content):
            matched = True
            index += 1
            continue
        break
    if not matched:
        return shebang, "".join(lines[start:])
    return shebang, "".join(lines[index:])


def strip_html_license_blocks(text: str) -> str:
    body = text
    removed = False
    while body.startswith("<!--"):
        end_index = body.find("-->")
        if end_index == -1:
            break
        block = body[: end_index + 3]
        if not is_license_line(block):
            break
        removed = True
        body = body[end_index + 3 :]
        body = body.lstrip("\n")
    return body if removed else text


def render_header(style: str, license_lines: list[str]) -> str:
    if style == "slash":
        return "\n".join(f"// {line}" for line in license_lines) + "\n//\n\n"
    if style == "hash":
        return "\n".join(f"# {line}" for line in license_lines) + "\n\n"
    if style == "html":
        return "<!--\n" + "\n".join(license_lines) + "\n-->\n\n"
    if style == "plain":
        return "\n".join(license_lines) + "\n"
    raise ValueError(f"Unsupported style: {style}")


def rewrite_text(path: str, target_path: str, is_new_file: bool) -> tuple[bool, str | None]:
    style = comment_style_for(target_path)
    if style is None:
        return False, f"unsupported-style:{path}"
    license_lines = target_license_lines(is_new_file)
    target = REPO_ROOT / target_path
    original_text = target.read_text(encoding="utf-8") if target.exists() else ""
    if style == "plain":
        updated_text = render_header(style, license_lines)
    elif style == "slash":
        shebang, body = strip_line_comment_header(original_text, "//")
        updated_text = shebang + render_header(style, license_lines) + body.lstrip("\n")
    elif style == "hash":
        shebang, body = strip_line_comment_header(original_text, "#")
        updated_text = shebang + render_header(style, license_lines) + body.lstrip("\n")
    else:
        body = strip_html_license_blocks(original_text)
        updated_text = render_header(style, license_lines) + body.lstrip("\n")
    return updated_text != original_text, updated_text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--author-pattern", default="David Osipov")
    parser.add_argument("--author-email", default="personal@david-osipov.vision")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    tracked = tracked_paths()
    candidates = sorted(path for path in david_touched_paths(args.author_pattern, tracked) if in_scope(path))
    work_items: dict[str, tuple[str, bool]] = {}
    skipped: list[str] = []
    for path in candidates:
        reference_path = effective_reference_path(path)
        if not (REPO_ROOT / reference_path).exists():
            continue
        target_path = effective_target_path(path)
        if not in_scope(target_path):
            continue
        is_new_file = added_by_author(reference_path, args.author_email)
        changed, payload = rewrite_text(path, target_path, is_new_file)
        if payload is None:
            skipped.append(target_path)
            continue
        if changed:
            work_items[target_path] = (payload, is_new_file)

    mit_only = sum(1 for _, is_new in work_items.values() if is_new)
    dual = len(work_items) - mit_only
    print(f"candidates={len(candidates)} updates={len(work_items)} mit_only={mit_only} dual={dual} skipped={len(skipped)}")
    for target_path in sorted(work_items):
        flavor = "MIT" if work_items[target_path][1] else "BSL-1.0 AND MIT"
        print(f"{flavor} {target_path}")
    for target_path in sorted(set(skipped)):
        print(f"SKIP {target_path}", file=sys.stderr)
    if args.write:
        for target_path, (payload, _) in work_items.items():
            (REPO_ROOT / target_path).write_text(payload, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())