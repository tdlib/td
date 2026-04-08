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

from check_drs import check_drs


def make_report(record_payload_sizes, *, active_policy: str = "non_ru_egress", quic_enabled: bool = False, baseline_distance: float = 0.05, long_flow_windows=None):
    return {
        "active_policy": active_policy,
        "quic_enabled": quic_enabled,
        "record_payload_sizes": list(record_payload_sizes),
        "baseline_distance": baseline_distance,
        "long_flow_windows": [] if long_flow_windows is None else list(long_flow_windows),
    }


class CheckDrsTest(unittest.TestCase):
    def test_accepts_varied_record_sizes(self) -> None:
        report = make_report(
            [1200, 1400, 1800, 2400, 4096, 6144, 8192, 12288, 16384],
            long_flow_windows=[{"total_payload_bytes": 140000, "max_record_payload_size": 12288}],
        )

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertEqual([], failures)

    def test_rejects_legacy_2878_dominant_mode(self) -> None:
        report = make_report([2878] * 8 + [1200, 1500, 4096])

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("dominant-mode-2878", failures)

    def test_rejects_long_constant_size_run(self) -> None:
        report = make_report([1400] * 10 + [2400, 4096])

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("constant-size-run", failures)

    def test_rejects_two_size_oscillation_signature(self) -> None:
        report = make_report([1400, 4096] * 8)

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("two-size-oscillation", failures)

    def test_rejects_long_flow_without_growth_beyond_8192(self) -> None:
        report = make_report(
            [1200, 1400, 1800, 4096, 6144, 8192],
            long_flow_windows=[{"total_payload_bytes": 150000, "max_record_payload_size": 8192}],
        )

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("missing-long-flow-growth", failures)

    def test_rejects_histogram_distance_regression(self) -> None:
        report = make_report([1200, 1400, 2400, 4096], baseline_distance=0.22)

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("histogram-distance", failures)

    def test_rejects_negative_baseline_distance_fail_closed(self) -> None:
        report = make_report([1200, 1400, 2400, 4096], baseline_distance=-0.01)

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("baseline-distance-missing", failures)

    def test_rejects_quic_enabled_scenario(self) -> None:
        report = make_report([], quic_enabled=True)

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("quic-disabled", failures)

    def test_rejects_nan_baseline_distance_fail_closed(self) -> None:
        report = make_report([1200, 1400, 2400, 4096], baseline_distance=float("nan"))

        failures = check_drs(report, {"histogram_distance_threshold": 0.15})

        self.assertIn("baseline-distance-missing", failures)

    def test_rejects_infinite_threshold_fail_closed(self) -> None:
        report = make_report([1200, 1400, 2400, 4096])

        failures = check_drs(report, {"histogram_distance_threshold": float("inf")})

        self.assertIn("policy-histogram-distance-threshold", failures)


if __name__ == "__main__":
    unittest.main()