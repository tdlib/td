# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import unittest

from extract_tls_record_size_histograms import parse_tshark_tls_record_rows


class ExtractTlsRecordSizeHistogramsStreamIdentityTest(unittest.TestCase):
    def test_separates_same_four_tuple_when_tcp_stream_changes(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|182|7\n"
            "2|0.001000|93.184.216.34|10.0.0.2|443|50000|93|7\n"
            "3|10.000000|10.0.0.2|93.184.216.34|50000|443|205|19\n"
            "4|10.001000|93.184.216.34|10.0.0.2|443|50000|108|19\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(2, len(connections))
        self.assertEqual(7, connections[0]["tcp_stream_id"])
        self.assertEqual(19, connections[1]["tcp_stream_id"])
        self.assertEqual([182, 93], [record["tls_record_size"] for record in connections[0]["records"]])
        self.assertEqual([205, 108], [record["tls_record_size"] for record in connections[1]["records"]])

    def test_interleaved_rows_from_multiple_streams_keep_connection_identity(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|182|7\n"
            "2|0.000500|10.0.0.2|93.184.216.34|50001|443|312|8\n"
            "3|0.001000|93.184.216.34|10.0.0.2|443|50000|93|7\n"
            "4|0.001500|93.184.216.34|10.0.0.2|443|50001|600|8\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(2, len(connections))
        self.assertEqual([7, 8], [connection["tcp_stream_id"] for connection in connections])
        self.assertEqual(["c2s", "s2c"], [record["direction"] for record in connections[0]["records"]])
        self.assertEqual(["c2s", "s2c"], [record["direction"] for record in connections[1]["records"]])

    def test_preserves_backward_compatible_parsing_without_tcp_stream_field(self) -> None:
        rows = (
            "1|0.000000|10.0.0.2|93.184.216.34|50000|443|182\n"
            "2|0.001000|93.184.216.34|10.0.0.2|443|50000|93\n"
        )

        connections = parse_tshark_tls_record_rows(rows)

        self.assertEqual(1, len(connections))
        self.assertIsNone(connections[0]["tcp_stream_id"])
        self.assertEqual([182, 93], [record["tls_record_size"] for record in connections[0]["records"]])


if __name__ == "__main__":
    unittest.main()