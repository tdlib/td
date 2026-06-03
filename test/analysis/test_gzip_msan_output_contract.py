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


class GzipMsanOutputContractTest(unittest.TestCase):
    def test_run_leaves_stream_intact_until_final_flush(self) -> None:
        source = GZIP_CPP.read_text(encoding="utf-8")
        run_region = extract_region(
            source,
            "Result<Gzip::State> Gzip::run() {",
            "size_t Gzip::left_input() const {",
        )

        done_branch = extract_region(
            run_region,
            "    if (ret == Z_STREAM_END) {",
            "    clear();",
        )

        self.assertNotIn("clear();", done_branch)
        self.assertIn("return State::Done;", done_branch)

    def test_flush_output_unpoisons_bytes_produced_by_zlib(self) -> None:
        source = GZIP_CPP.read_text(encoding="utf-8")
        flush_region = extract_region(
            source,
            "size_t Gzip::flush_output() {",
            "Gzip::Gzip() : impl_(make_unique<Impl>()) {",
        )

        self.assertIn("used_output()", flush_region)
        self.assertRegex(flush_region, r"unpoison_if_msan|__msan_unpoison")
        self.assertIn("output_size_ = left_output();", flush_region)


if __name__ == "__main__":
    unittest.main()
