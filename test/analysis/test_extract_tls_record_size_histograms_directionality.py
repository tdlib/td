# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import unittest

from extract_tls_record_size_histograms import build_record_size_artifact, parse_tshark_tls_record_rows


class ExtractTlsRecordSizeHistogramsDirectionalityTest(unittest.TestCase):
    def test_infers_client_endpoint_when_server_application_data_arrives_first(self) -> None:
        rows = (
            "1|0.000000|93.184.216.34|10.0.0.2|443|50000|93\n"
            "2|0.001245|10.0.0.2|93.184.216.34|50000|443|517\n"
            "3|0.003891|93.184.216.34|10.0.0.2|443|50000|16384\n"
            "4|0.005102|10.0.0.2|93.184.216.34|50000|443|312\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(1, len(connections))
        self.assertEqual(["10.0.0.2", 50000], connections[0]["client_endpoint"])
        self.assertEqual(["93.184.216.34", 443], connections[0]["server_endpoint"])
        self.assertEqual(["s2c", "c2s", "s2c", "c2s"], [record["direction"] for record in connections[0]["records"]])

    def test_first_flight_c2s_ignores_server_first_record_when_capture_starts_midstream(self) -> None:
        rows = (
            "1|0.000000|93.184.216.34|10.0.0.2|443|50000|93\n"
            "2|0.001245|10.0.0.2|93.184.216.34|50000|443|517\n"
            "3|0.003891|93.184.216.34|10.0.0.2|443|50000|16384\n"
            "4|0.005102|10.0.0.2|93.184.216.34|50000|443|312\n"
        )

        artifact = build_record_size_artifact(
            source_pcap="midstream.pcap",
            platform="linux_desktop",
            browser_family="chrome_146",
            tshark_output=rows,
            capture_date="2026-04-10T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        self.assertEqual([[517, 312]], artifact["aggregate_stats"]["first_flight_c2s_sizes"])

    def test_infers_known_alternative_tls_server_port_when_seen_first(self) -> None:
        rows = (
            "1|0.000000|93.184.216.34|10.0.0.2|8443|50000|101\n"
            "2|0.001245|10.0.0.2|93.184.216.34|50000|8443|611\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(["10.0.0.2", 50000], connections[0]["client_endpoint"])
        self.assertEqual(["93.184.216.34", 8443], connections[0]["server_endpoint"])
        self.assertEqual(["s2c", "c2s"], [record["direction"] for record in connections[0]["records"]])

    def test_falls_back_to_ephemeral_port_heuristic_for_nonstandard_tls_ports(self) -> None:
        rows = (
            "1|0.000000|93.184.216.34|10.0.0.2|1443|52000|119\n"
            "2|0.001245|10.0.0.2|93.184.216.34|52000|1443|701\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(["10.0.0.2", 52000], connections[0]["client_endpoint"])
        self.assertEqual(["93.184.216.34", 1443], connections[0]["server_endpoint"])
        self.assertEqual(["s2c", "c2s"], [record["direction"] for record in connections[0]["records"]])


if __name__ == "__main__":
    unittest.main()