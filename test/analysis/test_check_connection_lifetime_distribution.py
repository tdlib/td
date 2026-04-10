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

from check_connection_lifetime_distribution import check_connection_lifetime_distribution


def make_baseline(**overrides):
    baseline = {
        "profile_family": "desktop_http1_proxy_like",
        "parser_version": "connection-lifetime-baseline-v1",
        "aggregate_stats": {
            "lifetime_ms": {"p10": 18000, "p50": 91000, "p90": 240000, "p99": 540000},
            "idle_gap_ms": {"p50": 7200, "p90": 28000, "p99": 64000},
            "replacement_overlap_ms": {"p50": 350, "p95": 1800, "p99": 5000},
            "max_parallel_per_destination": 6,
        },
    }
    baseline.update(overrides)
    return baseline


def make_policy(**overrides):
    policy = {
        "max_connects_per_10s_per_destination": 6,
        "min_reuse_ratio": 0.55,
        "min_conn_lifetime_ms": 1500,
        "max_conn_lifetime_ms": 180000,
        "max_destination_share": 0.70,
        "sticky_domain_rotation_window_sec": 900,
        "anti_churn_min_reconnect_interval_ms": 300,
    }
    policy.update(overrides)
    return policy


def make_connection(
    destination: str,
    started_at_ms: int,
    ended_at_ms: int,
    *,
    reused: bool,
    bytes_sent: int = 65536,
    overlap_ms: int = 0,
):
    return {
        "destination": destination,
        "started_at_ms": started_at_ms,
        "ended_at_ms": ended_at_ms,
        "reused": reused,
        "bytes_sent": bytes_sent,
        "overlap_ms": overlap_ms,
    }


def make_report(connections, *, active_policy: str = "non_ru_egress", quic_enabled: bool = False):
    return {
        "active_policy": active_policy,
        "quic_enabled": quic_enabled,
        "connections": list(connections),
    }


class CheckConnectionLifetimeDistributionTest(unittest.TestCase):
    def test_real_browser_like_lifetimes_pass_baseline_check(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 80000, reused=False, overlap_ms=300),
                make_connection("1.1.1.1:443|a.example", 81000, 182000, reused=True, overlap_ms=600),
                make_connection("2.2.2.2:443|b.example", 5000, 99000, reused=True, overlap_ms=250),
                make_connection("2.2.2.2:443|b.example", 110000, 205000, reused=True, overlap_ms=400),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertEqual([], failures)

    def test_single_pinned_socket_anomaly_is_rejected(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 200001, reused=True, bytes_sent=65536),
                make_connection("2.2.2.2:443|b.example", 0, 45000, reused=False),
                make_connection("2.2.2.2:443|b.example", 50000, 120000, reused=True),
                make_connection("3.3.3.3:443|c.example", 10000, 90000, reused=True),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertIn("pinned-socket-anomaly", failures)

    def test_periodic_120_second_reconnect_pattern_is_rejected(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 120000, reused=False),
                make_connection("1.1.1.1:443|a.example", 120500, 240500, reused=True),
                make_connection("1.1.1.1:443|a.example", 241000, 361000, reused=True),
                make_connection("1.1.1.1:443|a.example", 361500, 481500, reused=True),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertIn("periodic-reconnect-pattern", failures)

    def test_median_lifetime_too_short_is_rejected(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("1.1.1.1:443|a.example", 5200, 12000, reused=True),
                make_connection("2.2.2.2:443|b.example", 0, 6000, reused=True),
                make_connection("2.2.2.2:443|b.example", 6100, 14000, reused=True),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertIn("baseline-median-lifetime", failures)

    def test_overlap_spike_above_budget_is_rejected(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 90000, reused=False, overlap_ms=2500),
                make_connection("1.1.1.1:443|a.example", 88000, 170000, reused=True, overlap_ms=3000),
                make_connection("2.2.2.2:443|b.example", 1000, 100000, reused=True, overlap_ms=2400),
                make_connection("2.2.2.2:443|b.example", 98000, 200000, reused=True, overlap_ms=2600),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertIn("replacement-overlap-spike", failures)

    def test_destination_monopoly_during_rotation_is_rejected(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 100000, reused=False, overlap_ms=300),
                make_connection("1.1.1.1:443|a.example", 1000, 101000, reused=True, overlap_ms=300),
                make_connection("1.1.1.1:443|a.example", 2000, 102000, reused=True, overlap_ms=300),
                make_connection("1.1.1.1:443|a.example", 3000, 103000, reused=True, overlap_ms=300),
                make_connection("1.1.1.1:443|a.example", 4000, 104000, reused=True, overlap_ms=300),
                make_connection("2.2.2.2:443|b.example", 5000, 105000, reused=True, overlap_ms=300),
            ]
        )

        failures = check_connection_lifetime_distribution(report, make_baseline(), make_policy())

        self.assertIn("destination-share", failures)


if __name__ == "__main__":
    unittest.main()