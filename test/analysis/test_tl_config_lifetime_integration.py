# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"
TL_CORE_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_core.cpp"


class TlConfigLifetimeIntegrationTest(unittest.TestCase):
    def test_tl_core_destructors_release_recursive_tree_and_constructor_allocations(
        self,
    ) -> None:
        core_source = TL_CORE_CPP.read_text(encoding="utf-8")
        config_source = TL_CONFIG_CPP.read_text(encoding="utf-8")

        self.assertIn("tl_type::~tl_type()", core_source)
        self.assertIn("tl_combinator::~tl_combinator()", core_source)
        self.assertIn("tl_tree_type::~tl_tree_type()", core_source)
        self.assertIn("tl_tree_array::~tl_tree_array()", core_source)

        self.assertIn("for (auto *constructor : constructors)", core_source)
        self.assertIn("destroy_zeroed_tl_object(constructor);", core_source)
        self.assertNotIn("delete constructor;", core_source)
        self.assertIn("for (auto *function : functions)", config_source)
        self.assertIn("destroy_zeroed(function);", config_source)

        self.assertIn("for (auto &arg_entry : args)", core_source)
        self.assertIn("delete arg_entry.type;", core_source)
        self.assertIn("delete result;", core_source)
        self.assertIn("for (auto *child : children)", core_source)
        self.assertIn("delete child;", core_source)
        self.assertIn("delete multiplicity;", core_source)


if __name__ == "__main__":
    unittest.main()
