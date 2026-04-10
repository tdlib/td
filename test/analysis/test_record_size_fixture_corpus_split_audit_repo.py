# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from audit_record_size_corpus import audit_split_record_size_corpus
from common_smoke import load_json_report


REPO_ROOT = THIS_DIR.parent.parent
FIXTURE_PATH = REPO_ROOT / "test" / "analysis" / "fixtures" / "record_sizes" / "capture_corpus.aggregate.record_sizes.json"


class RecordSizeFixtureCorpusSplitAuditRepoTest(unittest.TestCase):
    def test_checked_in_aggregate_fixture_splits_into_multiple_source_groups(self) -> None:
        report = load_json_report(FIXTURE_PATH)
        audits = audit_split_record_size_corpus(report, grouping="source_pcap")

        self.assertGreaterEqual(len(audits), 20)
        hottest_source = max(audits.items(), key=lambda item: item[1]["metrics"]["overall_small_fraction"])
        self.assertGreater(hottest_source[1]["metrics"]["overall_small_fraction"], 0.75)
        self.assertIn("overall-small-record-budget", hottest_source[1]["findings"])

    def test_checked_in_aggregate_fixture_splits_into_platform_and_browser_groups(self) -> None:
        report = load_json_report(FIXTURE_PATH)
        platform_audits = audit_split_record_size_corpus(report, grouping="platform")
        browser_audits = audit_split_record_size_corpus(report, grouping="browser_family")

        self.assertIn("android", platform_audits)
        self.assertIn("ios", platform_audits)
        self.assertIn("macos", platform_audits)
        self.assertIn("unknown", platform_audits)
        self.assertGreater(platform_audits["android"]["metrics"]["overall_small_fraction"], 0.60)
        self.assertGreater(platform_audits["android"]["metrics"]["s2c_small_fraction"], 0.80)
        self.assertGreater(platform_audits["ios"]["metrics"]["overall_small_fraction"], 0.55)
        self.assertGreater(platform_audits["ios"]["metrics"]["post_first_five_c2s_small_fraction"], 0.55)
        self.assertGreater(platform_audits["macos"]["metrics"]["overall_small_fraction"], 0.65)
        self.assertEqual(
            {
                "overall-small-record-budget",
                "c2s-small-record-budget",
                "s2c-small-record-budget",
                "post-greeting-c2s-small-record-budget",
            },
            set(platform_audits["android"]["findings"]),
        )
        self.assertEqual(
            {
                "overall-small-record-budget",
                "c2s-small-record-budget",
                "s2c-small-record-budget",
                "post-greeting-c2s-small-record-budget",
            },
            set(platform_audits["ios"]["findings"]),
        )
        self.assertEqual(
            {
                "overall-small-record-budget",
                "c2s-small-record-budget",
                "s2c-small-record-budget",
                "post-greeting-c2s-small-record-budget",
            },
            set(platform_audits["macos"]["findings"]),
        )
        self.assertIn("short-connection-share", platform_audits["unknown"]["findings"])
        self.assertLess(platform_audits["unknown"]["metrics"]["overall_small_fraction"], 0.35)

        self.assertIn("chrome", browser_audits)
        self.assertIn("firefox", browser_audits)
        self.assertIn("safari", browser_audits)
        self.assertIn("unknown", browser_audits)
        self.assertGreater(browser_audits["safari"]["metrics"]["overall_small_fraction"], 0.60)
        self.assertGreater(browser_audits["chrome"]["metrics"]["overall_small_fraction"], 0.50)
        self.assertGreater(browser_audits["firefox"]["metrics"]["c2s_small_fraction"], 0.80)
        self.assertGreater(browser_audits["firefox"]["metrics"]["post_first_five_c2s_small_fraction"], 0.80)
        self.assertEqual(
            {
                "overall-small-record-budget",
                "c2s-small-record-budget",
                "s2c-small-record-budget",
                "post-greeting-c2s-small-record-budget",
            },
            set(browser_audits["chrome"]["findings"]),
        )
        self.assertEqual(
            {
                "overall-small-record-budget",
                "c2s-small-record-budget",
                "s2c-small-record-budget",
                "post-greeting-c2s-small-record-budget",
            },
            set(browser_audits["safari"]["findings"]),
        )
        self.assertIn("short-connection-share", browser_audits["firefox"]["findings"])
        self.assertEqual([], browser_audits["unknown"]["findings"])
        self.assertLess(browser_audits["unknown"]["metrics"]["overall_small_fraction"], 0.01)
        self.assertLess(browser_audits["unknown"]["metrics"]["post_first_five_c2s_small_fraction"], 0.01)


if __name__ == "__main__":
    unittest.main()