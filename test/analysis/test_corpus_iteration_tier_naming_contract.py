# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STEALTH_TEST_ROOT = REPO_ROOT / "test" / "stealth"


class CorpusIterationTierNamingContract(unittest.TestCase):
    def test_files_named_1k_do_not_use_quick_iterations(self) -> None:
        offenders: list[str] = []
        for path in STEALTH_TEST_ROOT.glob("*1k*.cpp"):
            text = path.read_text(encoding="utf-8")
            if "kQuickIterations" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)

    def test_1k_suite_names_are_full_tier_or_explicit_nightly(self) -> None:
        offenders: list[str] = []
        for path in STEALTH_TEST_ROOT.glob("*.cpp"):
            text = path.read_text(encoding="utf-8")
            if "1k" not in path.name.lower() and not re.search(r"TEST\([^,]*1k", text):
                continue
            # A 1k suite is truthful when its corpus budget reaches the full
            # (nightly) tier. ``spot_or_full_corpus_iterations()`` expands to
            # ``is_nightly_corpus_enabled() ? kFullIterations : kSpotIterations``,
            # so it is an accepted full-tier marker alongside the raw tokens.
            if (
                "kFullIterations" in text
                or "is_nightly_corpus_enabled()" in text
                or "spot_or_full_corpus_iterations()" in text
            ):
                continue
            offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)
