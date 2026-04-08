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


if __name__ == "__main__":
    unittest.main()