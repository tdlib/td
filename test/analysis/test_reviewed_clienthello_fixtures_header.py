# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import re
import unittest

from merge_client_hello_fixture_summary import merge_artifacts


THIS_DIR = pathlib.Path(__file__).resolve().parent
HEADER_PATH = THIS_DIR.parent / "stealth" / "ReviewedClientHelloFixtures.h"
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"
LEGACY_BATCH1_TOKEN = "batch1"
CANONICAL_ROUTE_MODES = {"unknown", "ru_egress", "non_ru_egress"}
SOURCE_PATH_RE = re.compile(r'SourcePath\[\]\s*=\s*((?:"[^"]*"\s*)+);')
ROUTE_MODE_RE = re.compile(r'RouteMode\[\] = "([^"]+)";')


def parse_cxx_adjacent_string_literals(block: str) -> str:
    return "".join(re.findall(r'"([^"]*)"', block))


class ReviewedClientHelloFixturesHeaderTest(unittest.TestCase):
    def test_generated_header_matches_full_corpus_generator_output(self) -> None:
        content = HEADER_PATH.read_text(encoding="utf-8")
        regenerated = merge_artifacts(FIXTURES_ROOT)

        self.assertEqual(regenerated, content)

    def test_generated_header_uses_live_capture_paths(self) -> None:
        content = HEADER_PATH.read_text(encoding="utf-8")
        source_paths = [parse_cxx_adjacent_string_literals(block) for block in SOURCE_PATH_RE.findall(content)]

        self.assertTrue(source_paths)
        for source_path in source_paths:
            self.assertNotIn(LEGACY_BATCH1_TOKEN, source_path)
            self.assertTrue(pathlib.Path(source_path).exists(), msg=f"missing capture referenced by header: {source_path}")

    def test_generated_header_route_modes_are_canonical(self) -> None:
        content = HEADER_PATH.read_text(encoding="utf-8")
        route_modes = ROUTE_MODE_RE.findall(content)

        self.assertTrue(route_modes)
        for route_mode in route_modes:
            self.assertIn(route_mode, CANONICAL_ROUTE_MODES)

    def test_generated_header_contains_no_legacy_batch1_tokens(self) -> None:
        content = HEADER_PATH.read_text(encoding="utf-8")

        self.assertNotIn(LEGACY_BATCH1_TOKEN, content)


if __name__ == "__main__":
    unittest.main()