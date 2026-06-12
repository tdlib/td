# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class SimilarityReleaseGateContract(unittest.TestCase):
    def test_release_similarity_tests_do_not_return_on_empty_baselines(self) -> None:
        checked_files = [
            REPO_ROOT / "test" / "stealth" / "test_tls_multi_dump_windows_chrome_stats.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_multi_dump_ios_apple_tls_stats.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_generator_fixture_exact_fields_gate.cpp",
            REPO_ROOT / "test" / "stealth" / "test_tls_generator_wire_length_fixture_gate.cpp",
        ]
        offenders: list[str] = []
        for path in checked_files:
            if not path.exists():
                continue
            text = path.read_text(encoding="utf-8")
            if "return;  // Corpus not yet reviewed" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
            if "return;  // Linux baseline not yet populated" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
            if "Baseline review still in progress" in text:
                offenders.append(str(path.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)

    def test_docs_separate_similarity_gates_from_seed_stress(self) -> None:
        pipeline = (REPO_ROOT / "docs" / "Documentation" / "FINGERPRINT_GENERATION_PIPELINE.md").read_text(
            encoding="utf-8"
        )
        lessons = (REPO_ROOT / "docs" / "Documentation" / "Lessons_Learnt.md").read_text(encoding="utf-8")
        combined = pipeline + "\n" + lessons

        self.assertIn("real-corpus similarity gate", combined)
        self.assertIn("seed-stress diagnostic", combined)
        self.assertIn("self-calibrated generator tests are not real-browser similarity evidence", combined)

    def test_multi_dump_baseline_sources_are_wired_into_run_all_tests(self) -> None:
        cmake_text = (REPO_ROOT / "test" / "CMakeLists.txt").read_text(encoding="utf-8")
        required_sources = [
            "test_tls_multi_dump_android_firefox_baseline.cpp",
            "test_tls_multi_dump_macos_chromium_baseline.cpp",
        ]

        missing = [source for source in required_sources if source not in cmake_text]
        self.assertEqual([], missing)
