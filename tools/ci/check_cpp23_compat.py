# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import sys


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
IGNORED_TOP_LEVEL_DIRS = {
    ".git",
    ".github",
    "build",
    "build-asan",
    "build-ninja",
    "build-stealth",
    "cmake-build-debug",
    "cmake-build-release",
    "docs",
    "sqlite",
    "test",
}
BLOCKED_TOKENS = ("u8R\"", "u8\"", "u8'")


@dataclasses.dataclass(frozen=True)
class Finding:
    line: int
    column: int
    token: str


def should_skip_path(path: pathlib.Path, repo_root: pathlib.Path) -> bool:
    relative_path = path.relative_to(repo_root)
    return any(part in IGNORED_TOP_LEVEL_DIRS for part in relative_path.parts)


def collect_source_files(repo_root: pathlib.Path) -> list[pathlib.Path]:
    source_files: list[pathlib.Path] = []
    for path in sorted(repo_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        if should_skip_path(path, repo_root):
            continue
        source_files.append(path)
    return source_files


def _advance_position(chunk: str, line: int, column: int) -> tuple[int, int]:
    for character in chunk:
        if character == "\n":
            line += 1
            column = 1
        else:
            column += 1
    return line, column


def _consume_quoted_literal(text: str, index: int, quote: str, line: int, column: int) -> tuple[int, int, int]:
    i = index
    escaped = False
    while i < len(text):
        character = text[i]
        i += 1
        if character == "\n":
            line += 1
            column = 1
            escaped = False
            continue
        column += 1
        if escaped:
            escaped = False
            continue
        if character == "\\":
            escaped = True
            continue
        if character == quote:
            break
    return i, line, column


def _consume_raw_string(text: str, index: int, line: int, column: int) -> tuple[int, int, int]:
    delimiter_end = text.find("(", index)
    if delimiter_end == -1:
        return len(text), *_advance_position(text[index:], line, column)

    delimiter = text[index:delimiter_end]
    terminator = ")" + delimiter + '"'
    content_start = delimiter_end + 1
    terminator_index = text.find(terminator, content_start)
    if terminator_index == -1:
        return len(text), *_advance_position(text[index:], line, column)

    end_index = terminator_index + len(terminator)
    return end_index, *_advance_position(text[index:end_index], line, column)


def find_u8_literal_findings(text: str) -> list[Finding]:
    findings: list[Finding] = []
    index = 0
    line = 1
    column = 1
    length = len(text)

    while index < length:
        if text.startswith("//", index):
            newline_index = text.find("\n", index)
            if newline_index == -1:
                break
            line += 1
            column = 1
            index = newline_index + 1
            continue

        if text.startswith("/*", index):
            block_end = text.find("*/", index + 2)
            if block_end == -1:
                break
            line, column = _advance_position(text[index:block_end + 2], line, column)
            index = block_end + 2
            continue

        matched_token = next((token for token in BLOCKED_TOKENS if text.startswith(token, index)), None)
        if matched_token is not None:
            findings.append(Finding(line=line, column=column, token=matched_token))
            line, column = _advance_position(matched_token, line, column)
            index += len(matched_token)
            if matched_token == "u8R\"":
                index, line, column = _consume_raw_string(text, index, line, column)
            elif matched_token == "u8\"":
                index, line, column = _consume_quoted_literal(text, index, '"', line, column)
            else:
                index, line, column = _consume_quoted_literal(text, index, "'", line, column)
            continue

        if text.startswith('R"', index):
            line, column = _advance_position('R"', line, column)
            index += 2
            index, line, column = _consume_raw_string(text, index, line, column)
            continue

        if text[index] == '"':
            line, column = _advance_position('"', line, column)
            index += 1
            index, line, column = _consume_quoted_literal(text, index, '"', line, column)
            continue

        if text[index] == "'":
            line, column = _advance_position("'", line, column)
            index += 1
            index, line, column = _consume_quoted_literal(text, index, "'", line, column)
            continue

        if text[index] == "\n":
            line += 1
            column = 1
        else:
            column += 1
        index += 1

    return findings


def scan_repository(repo_root: pathlib.Path) -> list[tuple[pathlib.Path, Finding]]:
    findings: list[tuple[pathlib.Path, Finding]] = []
    for path in collect_source_files(repo_root):
        source_text = path.read_text(encoding="utf-8")
        for finding in find_u8_literal_findings(source_text):
            findings.append((path, finding))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail closed on new u8 literal prefixes in production C/C++ sources, which can regress C++23 char8_t compatibility.",
    )
    parser.add_argument("--repo-root", default=".", help="Repository root to scan.")
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    findings = scan_repository(repo_root)
    if not findings:
        return 0

    for path, finding in findings:
        relative_path = path.relative_to(repo_root)
        print(
            f"{relative_path}:{finding.line}:{finding.column}: blocked token {finding.token} is not allowed in production sources under C++23",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())