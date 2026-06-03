# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"


class TlConfigLifetimeAdversarialTest(unittest.TestCase):
    def test_clear_releases_type_and_function_owners(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")

        clear_pos = source.find("void tl_config::clear()")
        self.assertNotEqual(clear_pos, -1)
        clear_slice = source[clear_pos : clear_pos + 1200]

        self.assertIn("for (auto *type : types)", clear_slice)
        self.assertIn("destroy_zeroed(type);", clear_slice)
        self.assertIn("for (auto *function : functions)", clear_slice)
        self.assertIn("destroy_zeroed(function);", clear_slice)
        self.assertIn("types.clear();", clear_slice)
        self.assertIn("functions.clear();", clear_slice)
        self.assertIn("id_to_type.clear();", clear_slice)
        self.assertIn("name_to_type.clear();", clear_slice)
        self.assertIn("id_to_function.clear();", clear_slice)
        self.assertIn("name_to_function.clear();", clear_slice)

    def test_move_assignment_drops_previous_ownership_before_takeover(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")

        op_pos = source.find(
            "tl_config &tl_config::operator=(tl_config &&other) noexcept"
        )
        self.assertNotEqual(op_pos, -1)
        op_slice = source[op_pos : op_pos + 1000]

        self.assertIn("clear();", op_slice)
        self.assertIn("types = std::move(other.types);", op_slice)
        self.assertIn("functions = std::move(other.functions);", op_slice)


if __name__ == "__main__":
    unittest.main()
