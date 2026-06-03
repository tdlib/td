# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_WRITER_HPP_CPP = REPO_ROOT / "td" / "generate" / "tl_writer_hpp.cpp"


class TlWriterHppDiagnosticsContractTest(unittest.TestCase):
    def test_additional_functions_returns_direct_initializer_list(self) -> None:
        source = TL_WRITER_HPP_CPP.read_text(encoding="utf-8")

        self.assertIn('return {std::string{"downcast_call"}};', source)
        self.assertNotIn('additional_functions.push_back("downcast_call");', source)
        self.assertNotIn('additional_functions.emplace_back("downcast_call");', source)
        self.assertNotIn(
            'additional_functions.emplace_back(std::string{"downcast_call"});', source
        )

    def test_proxy_case_builder_avoids_literal_plus_string_chain(self) -> None:
        source = TL_WRITER_HPP_CPP.read_text(encoding="utf-8")

        marker = "std::string TD_TL_writer_hpp::gen_additional_proxy_function_case(const std::string &function_name,"
        start = source.rfind(marker)
        self.assertNotEqual(start, -1)
        snippet = source[start : start + 1000]

        self.assertIn('std::string result = "    case ";', snippet)
        self.assertIn("result += concrete_class_name;", snippet)
        self.assertIn("return result;", snippet)
        self.assertNotIn('return "    case " + gen_class_name(t->name) +', snippet)


if __name__ == "__main__":
    unittest.main()
