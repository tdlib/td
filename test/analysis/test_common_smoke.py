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

from common_smoke import validate_smoke_scenario


def make_report(*, active_policy, quic_enabled=False):
    return {
        "active_policy": active_policy,
        "quic_enabled": quic_enabled,
    }


class ValidateSmokeScenarioTest(unittest.TestCase):
    def test_accepts_canonical_non_ru_policy(self) -> None:
        failures = validate_smoke_scenario(make_report(active_policy="non_ru_egress"))

        self.assertEqual([], failures)

    def test_rejects_legacy_non_ru_alias(self) -> None:
        failures = validate_smoke_scenario(make_report(active_policy="non_ru"))

        self.assertIn("active-policy-known", failures)

    def test_rejects_non_string_policy(self) -> None:
        failures = validate_smoke_scenario(make_report(active_policy=None))

        self.assertIn("active-policy-known", failures)

    def test_rejects_quic_enabled(self) -> None:
        failures = validate_smoke_scenario(make_report(active_policy="non_ru_egress", quic_enabled=True))

        self.assertIn("quic-disabled", failures)


if __name__ == "__main__":
    unittest.main()