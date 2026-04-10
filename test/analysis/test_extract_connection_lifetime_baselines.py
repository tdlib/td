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

from extract_tls_connection_lifetime_baselines import build_connection_lifetime_baseline_artifact


class ExtractConnectionLifetimeBaselinesTest(unittest.TestCase):
    def test_extracts_lifetime_percentiles_from_known_capture(self) -> None:
        rows = (
            "1|0.000|10.0.0.2|93.184.216.34|50000|443|1|0|0|0|0|7\n"
            "2|0.010|10.0.0.2|93.184.216.34|50000|443|0|0|0|182|182|7\n"
            "3|0.040|93.184.216.34|10.0.0.2|443|50000|0|0|0|512|512|7\n"
            "4|0.090|10.0.0.2|93.184.216.34|50000|443|0|1|0|0|0|7\n"
            "5|1.000|10.0.0.2|93.184.216.35|50001|443|1|0|0|0|0|8\n"
            "6|1.020|10.0.0.2|93.184.216.35|50001|443|0|0|0|240|240|8\n"
            "7|1.120|93.184.216.35|10.0.0.2|443|50001|0|0|1|0|0|8\n"
        )

        artifact = build_connection_lifetime_baseline_artifact(
            source_pcap="browser_like_sample.pcapng",
            profile_family="desktop_http1_proxy_like",
            tshark_output=rows,
            capture_date="2026-04-10T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        self.assertEqual("browser_like_sample.pcapng", artifact["source_pcap"])
        self.assertEqual("desktop_http1_proxy_like", artifact["profile_family"])
        self.assertEqual(2, artifact["aggregate_stats"]["total_connections"])
        lifetime = artifact["aggregate_stats"]["lifetime_ms"]
        self.assertLessEqual(lifetime["p10"], lifetime["p50"])
        self.assertLessEqual(lifetime["p50"], lifetime["p90"])
        self.assertLessEqual(lifetime["p90"], lifetime["p99"])

    def test_idle_gap_histogram_is_monotonic(self) -> None:
        rows = (
            "1|0.000|10.0.0.2|93.184.216.34|50000|443|1|0|0|0|0|7\n"
            "2|0.010|10.0.0.2|93.184.216.34|50000|443|0|0|0|182|182|7\n"
            "3|0.070|93.184.216.34|10.0.0.2|443|50000|0|0|0|512|512|7\n"
            "4|0.130|10.0.0.2|93.184.216.34|50000|443|0|0|0|128|128|7\n"
            "5|0.180|93.184.216.34|10.0.0.2|443|50000|0|1|0|0|0|7\n"
        )

        artifact = build_connection_lifetime_baseline_artifact(
            source_pcap="idle_gap_sample.pcapng",
            profile_family="desktop_http1_proxy_like",
            tshark_output=rows,
            capture_date="2026-04-10T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        idle_gap = artifact["aggregate_stats"]["idle_gap_ms"]
        self.assertLessEqual(idle_gap["p50"], idle_gap["p90"])
        self.assertLessEqual(idle_gap["p90"], idle_gap["p99"])
        self.assertGreaterEqual(idle_gap["p50"], 0)

    def test_rejects_capture_without_tcp_close_or_timeout_marker(self) -> None:
        rows = (
            "1|0.000|10.0.0.2|93.184.216.34|50000|443|1|0|0|0|0|7\n"
            "2|0.010|10.0.0.2|93.184.216.34|50000|443|0|0|0|182|182|7\n"
            "3|0.070|93.184.216.34|10.0.0.2|443|50000|0|0|0|512|512|7\n"
        )

        with self.assertRaises(ValueError):
            build_connection_lifetime_baseline_artifact(
                source_pcap="missing_close_sample.pcapng",
                profile_family="desktop_http1_proxy_like",
                tshark_output=rows,
                capture_date="2026-04-10T00:00:00Z",
                tshark_version="TShark 4.2.0",
            )

    def test_profile_family_split_does_not_mix_http2_and_http11_cover(self) -> None:
        rows = (
            "1|0.000|10.0.0.2|93.184.216.34|50000|443|1|0|0|0|0|7\n"
            "2|0.010|10.0.0.2|93.184.216.34|50000|443|0|0|0|182|182|7\n"
            "3|0.090|10.0.0.2|93.184.216.34|50000|443|0|1|0|0|0|7\n"
        )

        with self.assertRaises(ValueError):
            build_connection_lifetime_baseline_artifact(
                source_pcap="mixed_cover_sample.pcapng",
                profile_family="desktop_http11_http2_mixed_cover",
                tshark_output=rows,
                capture_date="2026-04-10T00:00:00Z",
                tshark_version="TShark 4.2.0",
            )

    def test_overlap_window_extraction_handles_connection_replacement(self) -> None:
        rows = (
            "1|0.000|10.0.0.2|93.184.216.34|50000|443|1|0|0|0|0|7\n"
            "2|0.010|10.0.0.2|93.184.216.34|50000|443|0|0|0|182|182|7\n"
            "3|0.060|10.0.0.2|93.184.216.34|50001|443|1|0|0|0|0|8\n"
            "4|0.070|10.0.0.2|93.184.216.34|50001|443|0|0|0|200|200|8\n"
            "5|0.100|10.0.0.2|93.184.216.34|50000|443|0|1|0|0|0|7\n"
            "6|0.140|10.0.0.2|93.184.216.34|50001|443|0|1|0|0|0|8\n"
        )

        artifact = build_connection_lifetime_baseline_artifact(
            source_pcap="overlap_sample.pcapng",
            profile_family="desktop_http1_proxy_like",
            tshark_output=rows,
            capture_date="2026-04-10T00:00:00Z",
            tshark_version="TShark 4.2.0",
        )

        first_connection = artifact["connections"][0]
        self.assertEqual(60, first_connection["successor_opened_at_ms"])
        self.assertEqual(40, first_connection["overlap_ms"])
        overlap = artifact["aggregate_stats"]["replacement_overlap_ms"]
        self.assertEqual(40, overlap["p50"])
        self.assertEqual(40, overlap["p95"])

    def test_empty_corpus_fails_closed(self) -> None:
        with self.assertRaises(ValueError):
            build_connection_lifetime_baseline_artifact(
                source_pcap="empty_sample.pcapng",
                profile_family="desktop_http1_proxy_like",
                tshark_output="",
                capture_date="2026-04-10T00:00:00Z",
                tshark_version="TShark 4.2.0",
            )


if __name__ == "__main__":
    unittest.main()