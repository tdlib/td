# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GZIP_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "Gzip.cpp"


def extract_region(text: str, begin_marker: str, end_marker: str) -> str:
    begin = text.find(begin_marker)
    if begin == -1:
        raise AssertionError(f"missing begin marker: {begin_marker}")
    end = text.find(end_marker, begin + len(begin_marker))
    if end == -1:
        raise AssertionError(f"missing end marker: {end_marker}")
    if end <= begin:
        raise AssertionError("invalid region bounds")
    return text[begin:end]


class GzipErrorPathContractTest(unittest.TestCase):
    def test_flush_output_only_unpoisons_while_stream_is_active(self) -> None:
        source = GZIP_CPP.read_text(encoding="utf-8")
        flush_region = extract_region(
            source,
            "size_t Gzip::flush_output() {",
            "void Gzip::init_common() {",
        )

        self.assertIn("mode_ != Mode::Empty", flush_region)
        self.assertIn("unpoison_if_msan", flush_region)


if __name__ == "__main__":
    unittest.main()
