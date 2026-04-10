# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import unittest

from extract_tls_record_size_histograms import (
    build_record_size_artifact,
    parse_tshark_tls_record_rows,
)


class ExtractTlsRecordSizeHistogramsTest(unittest.TestCase):
    def test_parses_rows_into_connections_and_directions(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|182\n"
            "2|0.001245|93.184.216.34|10.0.0.2|443|50000|93\n"
            "3|0.003891|10.0.0.2|93.184.216.34|50000|443|517\n"
            "4|0.005102|93.184.216.34|10.0.0.2|443|50000|16384\n"
            "5|10.000000|10.0.0.2|93.184.216.34|50001|443|205\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(2, len(connections))
        self.assertEqual(4, len(connections[0]["records"]))
        self.assertEqual("c2s", connections[0]["records"][0]["direction"])
        self.assertEqual("s2c", connections[0]["records"][1]["direction"])
        self.assertEqual(0, connections[0]["records"][0]["seq"])
        self.assertEqual(182, connections[0]["records"][0]["tls_record_size"])
        self.assertEqual(0, connections[0]["records"][0]["relative_time_us"])
        self.assertEqual(1245, connections[0]["records"][1]["relative_time_us"])
        self.assertGreaterEqual(connections[0]["duration_ms"], 5)

    def test_build_artifact_computes_monotonic_percentiles_and_small_record_fraction(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|182\n"
            "2|0.001245|93.184.216.34|10.0.0.2|443|50000|93\n"
            "3|0.003891|10.0.0.2|93.184.216.34|50000|443|517\n"
            "4|0.005102|93.184.216.34|10.0.0.2|443|50000|16384\n"
            "5|0.008000|10.0.0.2|93.184.216.34|50000|443|312\n"
            "6|0.009000|10.0.0.2|93.184.216.34|50001|443|205\n"
            "7|0.011000|93.184.216.34|10.0.0.2|443|50001|600\n"
        )

        artifact = build_record_size_artifact(
            source_pcap="chrome146_android16_pixel9.pcap",
            platform="android",
            browser_family="chrome_146",
            tshark_output=rows,
            capture_date="2026-04-09T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        self.assertEqual("chrome146_android16_pixel9.pcap", artifact["source_pcap"])
        self.assertEqual("android", artifact["platform"])
        self.assertEqual("chrome_146", artifact["browser_family"])
        self.assertEqual(2, artifact["aggregate_stats"]["total_connections"])
        self.assertEqual(7, artifact["aggregate_stats"]["total_records"])
        c2s = artifact["aggregate_stats"]["c2s_size_percentiles"]
        self.assertLessEqual(c2s["p5"], c2s["p25"])
        self.assertLessEqual(c2s["p25"], c2s["p50"])
        self.assertLessEqual(c2s["p50"], c2s["p75"])
        self.assertLessEqual(c2s["p75"], c2s["p95"])
        self.assertAlmostEqual(2.0 / 7.0, artifact["aggregate_stats"]["small_record_fraction"])
        self.assertEqual([[182, 517, 312], [205]], artifact["aggregate_stats"]["first_flight_c2s_sizes"])

    def test_rejects_malformed_rows_and_non_positive_sizes(self) -> None:
        with self.assertRaises(ValueError):
            parse_tshark_tls_record_rows("1|0.000|10.0.0.2|93.184.216.34|50000|443\n")

        with self.assertRaises(ValueError):
            parse_tshark_tls_record_rows("1|0.000|10.0.0.2|93.184.216.34|50000|443|0\n")


if __name__ == "__main__":
    unittest.main()