# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_H = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.h"
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"
TL_CORE_H = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_core.h"


class TlConfigLifetimeContractTest(unittest.TestCase):
    def test_tl_config_declares_owning_special_members(self) -> None:
        header = TL_CONFIG_H.read_text(encoding="utf-8")

        self.assertIn("tl_config() = default;", header)
        self.assertIn("~tl_config();", header)
        self.assertIn("tl_config(const tl_config &) = delete;", header)
        self.assertIn("tl_config &operator=(const tl_config &) = delete;", header)
        self.assertIn("tl_config(tl_config &&other) noexcept;", header)
        self.assertIn("tl_config &operator=(tl_config &&other) noexcept;", header)

    def test_tl_parser_returns_config_by_move_not_copy(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        self.assertIn("return std::move(config);", source)

    def test_tl_core_declares_safe_initial_state(self) -> None:
        header = TL_CORE_H.read_text(encoding="utf-8")

        self.assertIn("std::int32_t id = 0;", header)
        self.assertIn("int arity = 0;", header)
        self.assertIn("std::int32_t flags = 0;", header)
        self.assertIn("int simple_constructors = 0;", header)
        self.assertIn("std::size_t constructors_num = 0;", header)
        self.assertIn("int var_num = -1;", header)
        self.assertIn("int exist_var_num = -1;", header)
        self.assertIn("tl_tree *type = nullptr;", header)
        self.assertIn("tl_tree *result = nullptr;", header)

    def test_tl_type_owns_constructor_links_via_custom_destructor(self) -> None:
        header = TL_CORE_H.read_text(encoding="utf-8")

        self.assertIn("std::vector<tl_combinator *> constructors;", header)
        self.assertNotIn("std::unique_ptr<tl_combinator>", header)
        self.assertIn("~tl_type();", header)
        self.assertNotIn("~tl_type() = default;", header)


if __name__ == "__main__":
    unittest.main()
