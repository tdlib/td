#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import sys
import unittest
import unittest.mock as mock

THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import extract_client_hello_fixtures as extractor  # noqa: E402


class ExtractClientHelloSynTsharkFieldFallback(unittest.TestCase):
    def setUp(self) -> None:
        extractor._TCP_OPTION_KIND_FIELD_CACHE = None

    def tearDown(self) -> None:
        extractor._TCP_OPTION_KIND_FIELD_CACHE = None

    def test_falls_back_when_tcp_options_kind_field_is_unavailable(self) -> None:
        unsupported_error = RuntimeError(
            "command failed (1): tshark -r capture.pcap -Y tcp.stream == 3 -T fields -e tcp.options.kind\n"
            "Some fields aren't valid:\n\ttcp.options.kind"
        )
        with mock.patch(
            "extract_client_hello_fixtures.run_command",
            side_effect=[unsupported_error, "64||1460|8|2,4,8,1,3|321\n"],
        ) as run_command_mock:
            traits = extractor.collect_syn_transport_traits_for_stream(
                pathlib.Path("capture.pcap"), "3"
            )

        self.assertTrue(traits["available"])
        self.assertEqual("ttl_33_64", traits["ttl_bucket"])
        self.assertEqual("mss_1201_1460", traits["mss_bucket"])
        self.assertEqual("wscale_6_8", traits["window_scale_bucket"])
        self.assertEqual("2-4-8-1-3", traits["syn_option_order_class"])
        self.assertEqual("nonzero", traits["ipid_behavior_class"])

        self.assertEqual(2, run_command_mock.call_count)
        primary_argv = run_command_mock.call_args_list[0][0][0]
        fallback_argv = run_command_mock.call_args_list[1][0][0]
        self.assertIn("tcp.options.kind", primary_argv)
        self.assertIn("tcp.option_kind", fallback_argv)

    def test_raises_when_no_supported_tcp_option_kind_field_exists(self) -> None:
        first_error = RuntimeError(
            "command failed (1): tshark ... -e tcp.options.kind\nSome fields aren't valid:\n\ttcp.options.kind"
        )
        second_error = RuntimeError(
            "command failed (1): tshark ... -e tcp.option_kind\nSome fields aren't valid:\n\ttcp.option_kind"
        )

        with mock.patch(
            "extract_client_hello_fixtures.run_command",
            side_effect=[first_error, second_error],
        ):
            with self.assertRaisesRegex(
                RuntimeError, "lacks supported TCP option kind field"
            ):
                extractor.collect_syn_transport_traits_for_stream(
                    pathlib.Path("capture.pcap"), "3"
                )


if __name__ == "__main__":
    unittest.main()
