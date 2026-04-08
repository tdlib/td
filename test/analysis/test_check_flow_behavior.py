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


def make_connection(destination: str, started_at_ms: int, ended_at_ms: int, *, reused: bool, bytes_sent: int = 4096):
    return {
        "destination": destination,
        "started_at_ms": started_at_ms,
        "ended_at_ms": ended_at_ms,
        "reused": reused,
        "bytes_sent": bytes_sent,
    }


def make_report(connections, *, active_policy: str = "non_ru_egress", quic_enabled: bool = False):
    return {
        "active_policy": active_policy,
        "quic_enabled": quic_enabled,
        "connections": list(connections),
    }


class CheckFlowBehaviorTest(unittest.TestCase):
    def test_accepts_balanced_flow_behavior(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("1.1.1.1:443|a.example", 5000, 10000, reused=True),
                make_connection("2.2.2.2:443|b.example", 10000, 16000, reused=True),
                make_connection("2.2.2.2:443|b.example", 22000, 29000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy())

        self.assertEqual([], failures)

    def test_rejects_reconnect_storm_to_single_destination(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", offset, offset + 1600, reused=False)
                for offset in (0, 1000, 2000, 3000, 4000, 5000, 6000)
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_connects_per_10s_per_destination=6))

        self.assertIn("reconnect-storm", failures)

    def test_rejects_reconnect_storm_hidden_by_domain_rotation_on_same_endpoint(self) -> None:
        report = make_report(
            [
                make_connection(f"1.1.1.1:443|domain{index}.example", index * 1000, index * 1000 + 1600, reused=False)
                for index in range(7)
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_connects_per_10s_per_destination=6))

        self.assertIn("reconnect-storm", failures)

    def test_rejects_low_reuse_ratio(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=False),
                make_connection("3.3.3.3:443|c.example", 15000, 22000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(min_reuse_ratio=0.55))

        self.assertIn("reuse-ratio", failures)

    def test_rejects_short_lifetime_and_anti_churn_violation(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 900, reused=False),
                make_connection("1.1.1.1:443|a.example", 250, 1150, reused=True),
                make_connection("2.2.2.2:443|b.example", 2000, 2900, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(min_conn_lifetime_ms=1500, anti_churn_min_reconnect_interval_ms=300))

        self.assertIn("median-connection-lifetime", failures)
        self.assertIn("anti-churn", failures)

    def test_rejects_pinned_socket_anomaly(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 200001, reused=True, bytes_sent=65536),
                make_connection("2.2.2.2:443|b.example", 0, 5000, reused=False),
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_conn_lifetime_ms=180000))

        self.assertIn("pinned-socket-anomaly", failures)

    def test_rejects_destination_share_collapse(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("1.1.1.1:443|a.example", 1000, 6000, reused=True),
                make_connection("1.1.1.1:443|a.example", 2000, 7000, reused=True),
                make_connection("1.1.1.1:443|a.example", 3000, 8000, reused=True),
                make_connection("2.2.2.2:443|b.example", 4000, 9000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_destination_share=0.70))

        self.assertIn("destination-share", failures)

    def test_rejects_quic_enabled_scenario(self) -> None:
        report = make_report([], quic_enabled=True)

        failures = check_flow_behavior(report, make_policy())

        self.assertIn("quic-disabled", failures)

    def test_rejects_nan_policy_value_fail_closed(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(max_destination_share=float("nan")))

        self.assertIn("policy-max-destination-share", failures)

    def test_rejects_infinite_policy_value_fail_closed(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(min_reuse_ratio=float("inf")))

        self.assertIn("policy-min-reuse-ratio", failures)

    def test_rejects_malformed_destination_bucket(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy())

        self.assertIn("connection-schema", failures)

    def test_rejects_too_small_sticky_rotation_window_fail_closed(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=True),
            ]
        )

        failures = check_flow_behavior(report, make_policy(sticky_domain_rotation_window_sec=59))

        self.assertIn("policy-sticky-domain-rotation-window", failures)

    def test_rejects_missing_sticky_rotation_window_fail_closed(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("2.2.2.2:443|b.example", 7000, 13000, reused=True),
            ]
        )
        policy = make_policy()
        del policy["sticky_domain_rotation_window_sec"]

        failures = check_flow_behavior(report, policy)

        self.assertIn("policy-sticky-domain-rotation-window", failures)

    def test_rejects_prolonged_destination_concentration_across_sticky_window(self) -> None:
        report = make_report(
            [
                make_connection("1.1.1.1:443|a.example", 0, 5000, reused=False),
                make_connection("1.1.1.1:443|a.example", 20000, 26000, reused=True),
                make_connection("2.2.2.2:443|b.example", 30000, 36000, reused=True),
                make_connection("1.1.1.1:443|a.example", 40000, 46000, reused=True),
                make_connection("1.1.1.1:443|a.example", 59000, 65000, reused=True),
            ]
        )

        failures = check_flow_behavior(
            report,
            make_policy(sticky_domain_rotation_window_sec=60, max_destination_share=0.70),
        )

        self.assertIn("sticky-domain-concentration", failures)


if __name__ == "__main__":
    unittest.main()