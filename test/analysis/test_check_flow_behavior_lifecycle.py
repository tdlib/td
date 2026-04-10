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

from check_flow_behavior import check_flow_behavior


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


def make_connection(destination: str, started_at_ms: int, ended_at_ms: int, *, reused: bool, bytes_sent: int = 65536):
    return {
        "destination": destination,
        "started_at_ms": started_at_ms,
        "ended_at_ms": ended_at_ms,
        "reused": reused,
        "bytes_sent": bytes_sent,
    }


def make_report(connections):
    return {
        "active_policy": "non_ru_egress",
        "quic_enabled": False,
        "connections": list(connections),
    }


class CheckFlowBehaviorLifecycleTest(unittest.TestCase):
    def test_rejects_periodic_reconnect_pattern_just_under_lifetime_ceiling(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", start, start + 170000, reused=True)
                for start in (0, 180000, 360000, 540000)
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_conn_lifetime_ms=180000))

        self.assertIn("periodic-reconnect-pattern", failures)

    def test_accepts_jittered_same_destination_rotation_pattern(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 121000, reused=False),
                make_connection("1.1.1.1:443|a.example", 143000, 296000, reused=True),
                make_connection("1.1.1.1:443|a.example", 338000, 508000, reused=True),
                make_connection("1.1.1.1:443|a.example", 579000, 714000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_conn_lifetime_ms=180000))

        self.assertNotIn("periodic-reconnect-pattern", failures)

    def test_periodic_reconnect_pattern_requires_multiple_intervals(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 170000, reused=False),
                make_connection("1.1.1.1:443|a.example", 180000, 350000, reused=True),
                make_connection("1.1.1.1:443|a.example", 360000, 530000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_conn_lifetime_ms=180000))

        self.assertNotIn("periodic-reconnect-pattern", failures)


if __name__ == "__main__":
    unittest.main()