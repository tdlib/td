# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from audit_record_size_corpus import (
    audit_record_size_corpus,
    audit_split_record_size_corpus,
    compute_record_size_corpus_metrics,
    infer_browser_family_from_source_pcap,
    infer_platform_from_source_pcap,
    split_record_size_corpus,
)


def make_report() -> dict:
    return {
        "connections": [
            {
                "records": [
                    {"direction": "c2s", "tls_record_size": 640},
                    {"direction": "s2c", "tls_record_size": 1200},
                    {"direction": "c2s", "tls_record_size": 720},
                    {"direction": "s2c", "tls_record_size": 16384},
                    {"direction": "c2s", "tls_record_size": 680},
                    {"direction": "c2s", "tls_record_size": 740},
                    {"direction": "s2c", "tls_record_size": 8192},
                ]
            },
            {
                "records": [
                    {"direction": "c2s", "tls_record_size": 520},
                    {"direction": "s2c", "tls_record_size": 980},
                    {"direction": "c2s", "tls_record_size": 610},
                    {"direction": "s2c", "tls_record_size": 2048},
                    {"direction": "c2s", "tls_record_size": 560},
                    {"direction": "c2s", "tls_record_size": 620},
                ]
            },
        ]
    }


class RecordSizeCorpusAuditTest(unittest.TestCase):
    def test_infers_platform_and_browser_family_from_source_pcap(self) -> None:
        self.assertEqual("android", infer_platform_from_source_pcap("Android 16, Chrome 146.0.7680.177.pcap"))
        self.assertEqual("ios", infer_platform_from_source_pcap("iOS 26.4, Safari 26.4 (2).pcap"))
        self.assertEqual("macos", infer_platform_from_source_pcap("MacOS Tahoe 26.3, safari 26.4.pcap"))
        self.assertEqual("unknown", infer_platform_from_source_pcap("gosuslugi.pcap"))

        self.assertEqual("chrome", infer_browser_family_from_source_pcap("Android 16, Chrome 146.0.7680.177.pcap"))
        self.assertEqual("safari", infer_browser_family_from_source_pcap("iOS 26.4, Safari 26.4 (2).pcap"))
        self.assertEqual("yandex", infer_browser_family_from_source_pcap("Яндекс.Браузер 26.3.4.128.pcap"))
        self.assertEqual("unknown", infer_browser_family_from_source_pcap("gosuslugi.pcap"))

    def test_computes_directional_and_phase_small_record_metrics(self) -> None:
        metrics = compute_record_size_corpus_metrics(make_report())

        self.assertEqual(2, metrics["connection_count"])
        self.assertEqual(0, metrics["short_connection_count"])
        self.assertAlmostEqual(0.0, metrics["overall_small_fraction"])
        self.assertAlmostEqual(0.0, metrics["c2s_small_fraction"])
        self.assertAlmostEqual(0.0, metrics["s2c_small_fraction"])
        self.assertAlmostEqual(0.0, metrics["post_first_five_c2s_small_fraction"])

    def test_audit_flags_directional_and_phase_budget_violations(self) -> None:
        report = {
            "connections": [
                {
                    "records": [
                        {"direction": "c2s", "tls_record_size": 34},
                        {"direction": "s2c", "tls_record_size": 52},
                        {"direction": "c2s", "tls_record_size": 19},
                        {"direction": "s2c", "tls_record_size": 83},
                        {"direction": "c2s", "tls_record_size": 41},
                        {"direction": "s2c", "tls_record_size": 97},
                        {"direction": "c2s", "tls_record_size": 26},
                        {"direction": "s2c", "tls_record_size": 88},
                        {"direction": "c2s", "tls_record_size": 63},
                        {"direction": "c2s", "tls_record_size": 63},
                    ]
                }
            ]
        }

        _, findings = audit_record_size_corpus(report)

        self.assertIn("overall-small-record-budget", findings)
        self.assertIn("c2s-small-record-budget", findings)
        self.assertIn("s2c-small-record-budget", findings)
        self.assertIn("post-greeting-c2s-small-record-budget", findings)

    def test_splits_corpus_by_source_platform_and_browser_family(self) -> None:
        report = {
            "browser_family": "capture_corpus_v1",
            "connections": [
                {
                    "source_pcap": "Android 16, Chrome 146.0.7680.177.pcap",
                    "records": [
                        {"direction": "c2s", "tls_record_size": 34},
                        {"direction": "s2c", "tls_record_size": 52},
                    ],
                },
                {
                    "source_pcap": "iOS 26.4, Safari 26.4 (2).pcap",
                    "records": [
                        {"direction": "c2s", "tls_record_size": 620},
                        {"direction": "s2c", "tls_record_size": 1400},
                    ],
                },
                {
                    "source_pcap": "gosuslugi.pcap",
                    "records": [
                        {"direction": "c2s", "tls_record_size": 210},
                        {"direction": "s2c", "tls_record_size": 260},
                    ],
                },
            ],
        }

        by_source = split_record_size_corpus(report, grouping="source_pcap")
        self.assertEqual(3, len(by_source))
        self.assertIn("Android 16, Chrome 146.0.7680.177.pcap", by_source)

        by_platform = split_record_size_corpus(report, grouping="platform")
        self.assertEqual({"android", "ios", "unknown"}, set(by_platform))

        by_browser_family = split_record_size_corpus(report, grouping="browser_family")
        self.assertEqual({"chrome", "safari", "unknown"}, set(by_browser_family))

        audits = audit_split_record_size_corpus(
            report,
            grouping="platform",
            budgets={"short_connection_fraction_max": 1.0},
        )
        self.assertIn("overall-small-record-budget", audits["android"]["findings"])
        self.assertEqual([], audits["ios"]["findings"])


if __name__ == "__main__":
    unittest.main()