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

from extract_client_hello_fixtures import select_record_hex
from extract_client_hello_fixtures import select_stream_start_hex
from extract_client_hello_fixtures import parse_client_hello
from extract_client_hello_fixtures import earliest_fragment_frame_number


class SelectRecordHexTest(unittest.TestCase):
    def test_prefers_reassembled_data_when_present(self) -> None:
        frame = {
            "tcp_reassembled_data": "160301abcd",
            "tcp_payload": "160301ef01",
        }

        self.assertEqual("160301abcd", select_record_hex(frame))

    def test_falls_back_to_tcp_payload(self) -> None:
        frame = {
            "tcp_reassembled_data": "",
            "tcp_payload": "160301ef01",
        }

        self.assertEqual("160301ef01", select_record_hex(frame))

    def test_raises_when_no_tls_bytes_available(self) -> None:
        frame = {
            "tcp_reassembled_data": "",
            "tcp_payload": "",
        }

        with self.assertRaises(ValueError):
            select_record_hex(frame)

    def test_stream_start_anchors_at_reassembled_record_inside_payload(self) -> None:
        frame = {
            "tcp_reassembled_data": "160301abcd",
            "tcp_payload": "deadbeef160301abcd1122",
        }

        self.assertEqual("160301abcd1122", select_stream_start_hex(frame))

    def test_earliest_fragment_frame_number_prefers_smallest_fragment(self) -> None:
        self.assertEqual("90", earliest_fragment_frame_number("90,91,91,91", "91"))
        self.assertEqual("91", earliest_fragment_frame_number("", "91"))


class ParseClientHelloTest(unittest.TestCase):
    def test_parses_client_hello_spanning_multiple_tls_records(self) -> None:
        random_bytes = bytes(range(32))
        handshake_body = bytearray()
        handshake_body.extend(b"\x03\x03")
        handshake_body.extend(random_bytes)
        handshake_body.extend(b"\x00")
        handshake_body.extend(b"\x00\x02")
        handshake_body.extend(b"\x13\x01")
        handshake_body.extend(b"\x01")
        handshake_body.extend(b"\x00")
        handshake_body.extend(b"\x00\x00")

        handshake = b"\x01" + len(handshake_body).to_bytes(3, "big") + bytes(handshake_body)
        first_record_body = handshake[:20]
        second_record_body = handshake[20:]
        record_sequence = (
            b"\x16\x03\x03" + len(first_record_body).to_bytes(2, "big") + first_record_body +
            b"\x16\x03\x03" + len(second_record_body).to_bytes(2, "big") + second_record_body
        )

        parsed = parse_client_hello(record_sequence)

        self.assertEqual("0x16", parsed["record_type"])
        self.assertEqual("0x0303", parsed["record_version"])
        self.assertEqual(2, parsed["record_count"])
        self.assertEqual([20, len(second_record_body)], parsed["record_lengths"])
        self.assertEqual(47, parsed["record_length"])
        self.assertEqual(43, parsed["handshake_length"])
        self.assertEqual(["0x1301"], parsed["cipher_suites"])
        self.assertEqual([], parsed["extensions"])


if __name__ == "__main__":
    unittest.main()