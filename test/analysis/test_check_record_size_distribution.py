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
        "reference_sizes": [
            182,
            205,
            312,
            380,
            517,
            640,
            780,
            1200,
            1450,
            16384,
            15000,
            14080,
        ],
        "greeting_ranges": [(150, 260), (450, 700), (250, 400)],
        "small_record_threshold": 200,
        "small_record_max_fraction": 0.10,
        "bulk_record_threshold": 12000,
        "bulk_record_min_fraction": 0.15,
        "bucket_boundaries": [64, 128, 192, 256, 384, 512, 768, 1024, 1280],
        "bucket_overhead": 9,
        "bucket_tolerance": 16,
        "bucket_excess_ratio_threshold": 1.5,
        "max_lag1_autocorrelation_abs": 0.4,
        "max_adjacent_size_ratio": 3.0,
        "ks_min_pvalue": 0.05,
        "chi_squared_min_pvalue": 0.05,
        "bin_width": 50,
    }


def make_report(record_sizes, *, first_flight=None):
    return {
        "record_payload_sizes": list(record_sizes),
        "first_flight_c2s_sizes": [list(first_flight or [])],
    }


class CheckRecordSizeDistributionTest(unittest.TestCase):
    def test_accepts_browser_like_distribution(self) -> None:
        baseline = make_baseline()
        report = make_report(
            [
                517,
                205,
                380,
                640,
                1200,
                780,
                1450,
                2200,
                4100,
                3600,
                6800,
                5200,
                9000,
                11800,
                15000,
                14080,
                12000,
                16384,
                15020,
            ],
            first_flight=[205, 517, 312],
        )

        failures = check_record_size_distribution(report, baseline)

        self.assertEqual([], failures)

    def test_rejects_bucket_quantized_distribution(self) -> None:
        baseline = make_baseline()
        report = make_report(
            [73, 73, 73, 137, 137, 137, 265, 265, 265, 393, 393, 393, 521, 521, 521],
            first_flight=[73, 265, 137],
        )

        failures = check_record_size_distribution(report, baseline)

        self.assertIn("ks-test", failures)
        self.assertIn("chi-squared", failures)
        self.assertIn("bucket-quantization", failures)

    def test_rejects_small_record_frequency_and_bad_first_flight(self) -> None:
        baseline = make_baseline()
        report = make_report(
            [88, 97, 105, 115, 180, 190, 210, 220, 300, 320],
            first_flight=[88, 300, 128],
        )

        failures = check_record_size_distribution(report, baseline)

        self.assertIn("small-record-frequency", failures)
        self.assertIn("first-flight-template", failures)

    def test_rejects_deterministic_sequences(self) -> None:
        baseline = make_baseline()
        report = make_report(
            [200, 240, 280, 320, 360, 420, 500, 600, 720, 860, 1030, 1240, 1490, 1790, 2150, 2580,
             3100, 3720, 4460, 5350, 6420, 7700, 9240, 30000],
            first_flight=[200, 400, 800],
        )

        failures = check_record_size_distribution(report, baseline)

        self.assertIn("lag1-autocorrelation", failures)
        self.assertIn("phase-transition-smoothness", failures)


if __name__ == "__main__":
    unittest.main()