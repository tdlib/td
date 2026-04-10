# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import unittest

from extract_tls_record_size_histograms import build_record_size_artifact, parse_tshark_tls_record_rows


class ExtractTlsRecordSizeHistogramsPartialCaptureEdgesTest(unittest.TestCase):
    def test_partial_capture_with_only_server_records_keeps_empty_c2s_first_flight(self) -> None:
        rows = (
            "1|0.000000|93.184.216.34|10.0.0.2|443|50000|93|7\n"
            "2|0.001245|93.184.216.34|10.0.0.2|443|50000|108|7\n"
        )

        artifact = build_record_size_artifact(
            source_pcap="server-only-midstream.pcap",
            platform="linux_desktop",
            browser_family="chrome_146",
            tshark_output=rows,
            capture_date="2026-04-10T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        self.assertEqual([[]], artifact["aggregate_stats"]["first_flight_c2s_sizes"])
        self.assertEqual(2, artifact["aggregate_stats"]["total_records"])

    def test_partial_capture_with_interleaved_streams_keeps_per_stream_relative_time_origin(self) -> None:
        rows = (
            "1|10.000000|10.0.0.2|93.184.216.34|50000|443|182|7\n"
            "2|10.100000|10.0.0.2|93.184.216.34|50001|443|205|8\n"
            "3|10.300000|93.184.216.34|10.0.0.2|443|50000|93|7\n"
            "4|10.600000|93.184.216.34|10.0.0.2|443|50001|108|8\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(2, len(connections))
        self.assertEqual([0, 300000], [record["relative_time_us"] for record in connections[0]["records"]])
        self.assertEqual([0, 500000], [record["relative_time_us"] for record in connections[1]["records"]])


if __name__ == "__main__":
    unittest.main()