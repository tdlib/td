# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"


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


class TlConfigZeroedAllocationContractTest(unittest.TestCase):
    def test_zeroed_allocation_has_matching_destroy_helper(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        helper_region = extract_region(
            source,
            "template <class T, class... Args>",
            "template <class T>\nvoid unpoison_if_msan(T &value) {",
        )

        self.assertIn("::operator new(sizeof(T))", helper_region)
        self.assertIn("std::memset(storage, 0, sizeof(T));", helper_region)
        self.assertIn("void destroy_zeroed(", helper_region)
        self.assertIn("std::destroy_at(value);", helper_region)
        self.assertIn(
            "return new (storage) T(std::forward<Args>(args)...);", helper_region
        )

    def test_clear_uses_destroy_helper_instead_of_plain_delete(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        clear_region = extract_region(
            source,
            "void tl_config::clear() {",
            "void tl_config::add_type(tl_type *type) {",
        )

        self.assertIn("destroy_zeroed(type);", clear_region)
        self.assertIn("destroy_zeroed(function);", clear_region)
        self.assertNotIn("delete type;", clear_region)
        self.assertNotIn("delete function;", clear_region)


if __name__ == "__main__":
    unittest.main()
