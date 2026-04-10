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

from check_record_size_distribution import check_record_size_distribution


def make_baseline() -> dict:
    return {
        "reference_sizes": [182, 205, 312, 380, 517, 640, 780, 1200, 1450, 16384, 15000, 14080],
        "greeting_ranges": [[150, 260], [450, 700], [250, 400]],
        "small_record_threshold": 200,
        "small_record_max_fraction": 0.20,
        "bulk_record_threshold": 12000,
        "bulk_record_min_fraction": 0.0,
        "bucket_boundaries": [64, 128, 192, 256, 384, 512, 768, 1024, 1280],
        "bucket_overhead": 9,
        "bucket_tolerance": 16,
        "bucket_excess_ratio_threshold": 10.0,
        "max_lag1_autocorrelation_abs": 1.0,
        "max_adjacent_size_ratio": 100.0,
        "ks_min_pvalue": 0.0,
        "chi_squared_min_pvalue": 0.0,
        "bin_width": 50,
    }


class CheckRecordSizeDistributionConnectionNormalizationTest(unittest.TestCase):
    def test_accepts_raw_connection_report_and_uses_only_c2s_first_flight(self) -> None:
        baseline = make_baseline()
        report = {
            "connections": [
                {
                    "records": [
                        {"direction": "s2c", "tls_record_size": 182},
                        {"direction": "c2s", "tls_record_size": 205},
                        {"direction": "s2c", "tls_record_size": 380},
                        {"direction": "c2s", "tls_record_size": 517},
                        {"direction": "c2s", "tls_record_size": 312},
                        {"direction": "s2c", "tls_record_size": 14080},
                    ]
                }
            ]
        }

        failures = check_record_size_distribution(report, baseline)

        self.assertEqual([], failures)

    def test_ignores_invalid_connection_entries_without_poisoning_valid_first_flight(self) -> None:
        baseline = make_baseline()
        report = {
            "connections": [
                {
                    "records": [
                        {"direction": "c2s", "tls_record_size": 0},
                        {"direction": "c2s", "tls_record_size": -1},
                        {"direction": "c2s", "tls_record_size": True},
                        {"direction": "s2c", "tls_record_size": 182},
                    ]
                },
                {
                    "records": [
                        {"direction": "c2s", "tls_record_size": 205},
                        {"direction": "s2c", "tls_record_size": 380},
                        {"direction": "c2s", "tls_record_size": 517},
                        {"direction": "c2s", "tls_record_size": 312},
                        {"direction": "s2c", "tls_record_size": 640},
                        {"direction": "s2c", "tls_record_size": 780},
                        {"direction": "s2c", "tls_record_size": 1200},
                        {"direction": "s2c", "tls_record_size": 1450},
                    ]
                },
            ]
        }

        failures = check_record_size_distribution(report, baseline)

        self.assertNotIn("schema", failures)
        self.assertNotIn("first-flight-template", failures)


if __name__ == "__main__":
    unittest.main()