# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import unittest

from extract_server_hello_fixtures import parse_tshark_server_hello_rows


class ExtractServerHelloFixturesTest(unittest.TestCase):
    def test_parses_server_hello_rows_into_artifact_samples(self) -> None:
        rows = "7|22,20|0x0304|0x1301|43,51,41\n8|22,20|0x0304|0x1301|43,51\n"

        samples = parse_tshark_server_hello_rows(rows, "chrome146_serverhello", "chromium_44cd_mlkem_linux_desktop")

        self.assertEqual(2, len(samples))
        self.assertEqual("chrome146_serverhello:frame7", samples[0]["fixture_id"])
        self.assertEqual("chromium_44cd_mlkem_linux_desktop", samples[0]["fixture_family_id"])
        self.assertEqual([22, 20], samples[0]["record_layout_signature"])
        self.assertEqual("0x0304", samples[0]["selected_version"])
        self.assertEqual("0x1301", samples[0]["cipher_suite"])
        self.assertEqual(["0x002B", "0x0033", "0x0029"], samples[0]["extensions"])

    def test_rejects_malformed_tshark_rows(self) -> None:
        with self.assertRaises(ValueError):
            parse_tshark_server_hello_rows("7|22,20|0x0304\n", "chrome146_serverhello", "family")


if __name__ == "__main__":
    unittest.main()