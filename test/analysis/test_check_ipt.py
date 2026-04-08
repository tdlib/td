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

from check_ipt import check_ipt


def make_run(*, p_value: float, keepalive_bypass_p99_ms: float, baseline_distance: float, active_intervals_ms):
    return {
        "lognormal_fit_p_value": p_value,
        "keepalive_bypass_p99_ms": keepalive_bypass_p99_ms,
        "baseline_distance": baseline_distance,
        "active_intervals_ms": list(active_intervals_ms),
    }


def make_report(runs, *, active_policy: str = "non_ru_egress", quic_enabled: bool = False):
    return {
        "active_policy": active_policy,
        "quic_enabled": quic_enabled,
        "runs": list(runs),
    }


class CheckIptTest(unittest.TestCase):
    def test_accepts_majority_good_runs(self) -> None:
        report = make_report(
            [
                make_run(p_value=0.40, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10, 20, 30]),
                make_run(p_value=0.07, keepalive_bypass_p99_ms=3.0, baseline_distance=0.06, active_intervals_ms=[15, 25, 40]),
                make_run(p_value=0.12, keepalive_bypass_p99_ms=4.0, baseline_distance=0.05, active_intervals_ms=[12, 18, 35]),
            ]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertEqual([], failures)

    def test_rejects_two_failed_lognormal_runs(self) -> None:
        report = make_report(
            [
                make_run(p_value=0.01, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10]),
                make_run(p_value=0.03, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[20]),
                make_run(p_value=0.20, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[30]),
            ]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("lognormal-fit", failures)

    def test_rejects_duplicate_active_interval_runs(self) -> None:
        report = make_report(
            [
                make_run(p_value=0.40, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10, 20, 40]),
                make_run(p_value=0.45, keepalive_bypass_p99_ms=2.5, baseline_distance=0.05, active_intervals_ms=[10, 20, 40]),
                make_run(p_value=0.50, keepalive_bypass_p99_ms=3.0, baseline_distance=0.05, active_intervals_ms=[10, 20, 40]),
            ]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("replayed-active-intervals", failures)

    def test_rejects_keepalive_bypass_regression(self) -> None:
        report = make_report(
            [make_run(p_value=0.10, keepalive_bypass_p99_ms=12.0, baseline_distance=0.04, active_intervals_ms=[10, 20])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("keepalive-bypass", failures)

    def test_rejects_detector_visible_stall(self) -> None:
        report = make_report(
            [make_run(p_value=0.20, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10, 6000, 20])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("detector-visible-stall", failures)

    def test_rejects_baseline_distance_regression(self) -> None:
        report = make_report(
            [make_run(p_value=0.20, keepalive_bypass_p99_ms=2.0, baseline_distance=0.20, active_intervals_ms=[10, 20, 30])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("baseline-distance", failures)

    def test_rejects_negative_baseline_distance_fail_closed(self) -> None:
        report = make_report(
            [make_run(p_value=0.20, keepalive_bypass_p99_ms=2.0, baseline_distance=-0.01, active_intervals_ms=[10, 20, 30])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("run-schema", failures)

    def test_rejects_quic_enabled_scenario(self) -> None:
        report = make_report([], quic_enabled=True)

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("quic-disabled", failures)

    def test_rejects_nan_run_metrics_fail_closed(self) -> None:
        report = make_report(
            [make_run(p_value=float("nan"), keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": 0.10})

        self.assertIn("run-schema", failures)

    def test_rejects_infinite_policy_threshold_fail_closed(self) -> None:
        report = make_report(
            [make_run(p_value=0.20, keepalive_bypass_p99_ms=2.0, baseline_distance=0.04, active_intervals_ms=[10, 20])]
        )

        failures = check_ipt(report, {"baseline_distance_threshold": float("inf")})

        self.assertIn("policy-baseline-distance-threshold", failures)


if __name__ == "__main__":
    unittest.main()