# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD_HTML = REPO_ROOT / "build.html"


class BuildInstructionsSemanticsContractTest(unittest.TestCase):
    def test_page_uses_semantic_main_wrapper(self) -> None:
        source = BUILD_HTML.read_text(encoding="utf-8")

        self.assertIn('<main class="main">', source)
        self.assertNotIn('<div class="main">', source)

    def test_top_level_question_and_result_blocks_use_sections(self) -> None:
        source = BUILD_HTML.read_text(encoding="utf-8")

        for block_id in (
            "languageSelectDiv",
            "osSelectDiv",
            "linuxSelectDiv",
            "buildTextDiv",
            "buildCommandsDiv",
        ):
            with self.subTest(block_id=block_id):
                self.assertIn(f'<section id="{block_id}"', source)

    def test_meaningful_radio_groups_use_fieldset_and_legend(self) -> None:
        source = BUILD_HTML.read_text(encoding="utf-8")

        for block_id in (
            "buildCompilerDiv",
            "buildArchiverDiv",
            "buildShellDiv",
            "buildShellBsdDiv",
            "buildMacOsHostDiv",
            "buildBitnessDiv",
        ):
            with self.subTest(block_id=block_id):
                self.assertIn(f'<fieldset id="{block_id}"', source)
                self.assertIn("<legend>", source)

    def test_select_prompts_use_explicit_labels(self) -> None:
        source = BUILD_HTML.read_text(encoding="utf-8")

        self.assertRegex(source, re.compile(r'<label\b[^>]*\bfor="languageSelect"'))
        self.assertRegex(source, re.compile(r'<label\b[^>]*\bfor="osSelect"'))
        self.assertRegex(source, re.compile(r'<label\b[^>]*\bfor="linuxSelect"'))


if __name__ == "__main__":
    unittest.main()
