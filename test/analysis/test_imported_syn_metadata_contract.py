#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import sys
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import extract_client_hello_fixtures as extractor  # noqa: E402


class ImportedSynMetadataContract(unittest.TestCase):
    def test_collect_syn_transport_traits_contract(self) -> None:
        row = {
            "ip_ttl": "64",
            "ipv6_hlim": "",
            "tcp_mss": "1460",
            "tcp_wscale": "8",
            "tcp_options_kind": "2,4,8,1,3",
            "ip_id": "321",
        }
        traits = extractor.parse_syn_transport_traits_row(row)

        self.assertTrue(traits["available"])
        self.assertEqual("ttl_33_64", traits["ttl_bucket"])
        self.assertEqual("mss_1201_1460", traits["mss_bucket"])
        self.assertEqual("wscale_6_8", traits["window_scale_bucket"])
        self.assertEqual("2-4-8-1-3", traits["syn_option_order_class"])
        self.assertEqual("nonzero", traits["ipid_behavior_class"])

    def test_collect_syn_transport_traits_fail_closed_without_ttl_or_hlim(self) -> None:
        row = {
            "ip_ttl": "",
            "ipv6_hlim": "",
            "tcp_mss": "1460",
            "tcp_wscale": "8",
            "tcp_options_kind": "2,4,8,1,3",
            "ip_id": "321",
        }
        traits = extractor.parse_syn_transport_traits_row(row)
        self.assertFalse(traits["available"])
        self.assertEqual("missing_ttl_or_hlim", traits["reason"])

    def test_collect_syn_transport_traits_accepts_hex_ip_id(self) -> None:
        row = {
            "ip_ttl": "64",
            "ipv6_hlim": "",
            "tcp_mss": "1460",
            "tcp_wscale": "8",
            "tcp_options_kind": "2,4,8,1,3",
            "ip_id": "0x0960",
        }
        traits = extractor.parse_syn_transport_traits_row(row)

        self.assertTrue(traits["available"])
        self.assertEqual("nonzero", traits["ipid_behavior_class"])


if __name__ == "__main__":
    unittest.main()
