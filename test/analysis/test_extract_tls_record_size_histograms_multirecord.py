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

from extract_tls_record_size_histograms import build_record_size_artifact, parse_tshark_tls_record_rows


class ExtractTlsRecordSizeHistogramsMultirecordTest(unittest.TestCase):
    def test_splits_comma_separated_tls_record_lengths_into_distinct_records(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|16401,26\n"
            "2|0.001000|93.184.216.34|10.0.0.2|443|50000|93\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(1, len(connections))
        self.assertEqual(3, len(connections[0]["records"]))
        self.assertEqual(16401, connections[0]["records"][0]["tls_record_size"])
        self.assertEqual(26, connections[0]["records"][1]["tls_record_size"])
        self.assertEqual(0, connections[0]["records"][0]["relative_time_us"])
        self.assertEqual(0, connections[0]["records"][1]["relative_time_us"])
        self.assertEqual(1000, connections[0]["records"][2]["relative_time_us"])

    def test_build_artifact_counts_all_records_from_multirecord_frame(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|16401,26\n"
            "2|0.001000|93.184.216.34|10.0.0.2|443|50000|93\n"
            "3|0.002000|10.0.0.2|93.184.216.34|50000|443|517\n"
        )

        artifact = build_record_size_artifact(
            source_pcap="multi-record.pcap",
            platform="android",
            browser_family="chrome_146",
            tshark_output=rows,
            capture_date="2026-04-09T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        self.assertEqual(4, artifact["aggregate_stats"]["total_records"])
        self.assertEqual([[16401, 26, 517]], artifact["aggregate_stats"]["first_flight_c2s_sizes"])
        self.assertAlmostEqual(0.5, artifact["aggregate_stats"]["small_record_fraction"])


if __name__ == "__main__":
    unittest.main()