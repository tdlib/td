# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_WRITER_CPP = REPO_ROOT / "td" / "generate" / "tl_writer_cpp.cpp"


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


class TlWriterCppTreeUnpoisonContractTest(unittest.TestCase):
    def test_arg_unpoison_recurses_into_dynamic_tree_payload(self) -> None:
        source = TL_WRITER_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "void unpoison_if_msan(const td::tl::arg &value) {",
            "void unpoison_if_msan(const std::vector<td::tl::arg> &values) {",
        )

        self.assertIn("if (value.type != nullptr) {", region)
        self.assertIn("unpoison_if_msan(*value.type);", region)
        self.assertNotIn("unpoison_object_if_msan(*value.type);", region)

    def test_tree_unpoison_dispatches_to_derived_node_layouts(self) -> None:
        source = TL_WRITER_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "void unpoison_if_msan(const td::tl::tl_tree &value) {",
            "void unpoison_if_msan(const std::vector<td::tl::tl_tree *> &values) {",
        )

        self.assertIn("switch (value.get_type())", region)
        self.assertIn("case td::tl::NODE_TYPE_TYPE:", region)
        self.assertIn("case td::tl::NODE_TYPE_ARRAY:", region)
        self.assertIn("case td::tl::NODE_TYPE_NAT_CONST:", region)
        self.assertIn("case td::tl::NODE_TYPE_VAR_TYPE:", region)
        self.assertIn("case td::tl::NODE_TYPE_VAR_NUM:", region)

    def test_type_unpoison_does_not_walk_constructor_graph(self) -> None:
        source = TL_WRITER_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "void unpoison_if_msan(const td::tl::tl_type &value) {",
            "void unpoison_if_msan(const td::tl::tl_combinator &value) {",
        )

        self.assertIn("unpoison_object_if_msan(value);", region)
        self.assertIn("unpoison_if_msan(value.name);", region)
        self.assertNotIn("unpoison_if_msan(value.constructors);", region)


if __name__ == "__main__":
    unittest.main()
