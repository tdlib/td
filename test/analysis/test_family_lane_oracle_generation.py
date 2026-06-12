# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import build_family_lane_baselines as baselines  # noqa: E402


def artifact(
    *,
    profile_id: str,
    fixture_id: str,
    source_sha256: str,
    extensions: list[str],
    cipher_suites: list[str],
    supported_groups: list[str],
    supported_versions: list[str],
    record_length: int,
    handshake_length: int,
    ech_payload_length: int | None,
) -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "profile_id": profile_id,
        "route_mode": "non_ru_egress",
        "source_kind": "browser_capture",
        "source_path": f"docs/Samples/Traffic dumps/Linux, desktop/{profile_id}.pcapng",
        "source_sha256": source_sha256,
        "scenario_id": profile_id,
        "parser_version": "tls-clienthello-parser-v1",
        "capture_date_utc": "2026-04-08T00:00:00Z",
        "os_family": "linux",
        "device_class": "desktop",
        "transport": "tcp",
        "samples": [
            {
                "fixture_id": fixture_id,
                "frame_number": 5,
                "tcp_stream": 0,
                "record_length": record_length,
                "handshake_length": handshake_length,
                "cipher_suites": ["0x0A0A", *cipher_suites],
                "non_grease_cipher_suites": cipher_suites,
                "supported_groups": ["0x1A1A", *supported_groups],
                "non_grease_supported_groups": supported_groups,
                "supported_versions": ["0x2A2A", *supported_versions],
                "non_grease_supported_versions": supported_versions,
                "extension_types": ["0x0A0A", *extensions, "0x1A1A"],
                "non_grease_extensions_without_padding": extensions,
                "alpn_protocols": ["h2", "http/1.1"],
                "compress_certificate_algorithms": ["0x0002"],
                "key_share_entries": [
                    {"group": "0x11EC", "key_exchange_length": 1216, "is_grease_group": False},
                    {"group": "0x001D", "key_exchange_length": 32, "is_grease_group": False},
                ],
                "ech": None
                if ech_payload_length is None
                else {
                    "type": "0xFE0D",
                    "payload_length": ech_payload_length,
                    "kdf_id": "0x0001",
                    "aead_id": "0x0001",
                },
            }
        ],
    }


class FamilyLaneOracleGenerationTest(unittest.TestCase):
    def test_exact_fields_and_histograms_are_emitted(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            (fixtures / "chrome_a.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_a",
                        fixture_id="chrome_a:frame5",
                        source_sha256="a" * 64,
                        extensions=["0x0000", "0x002B", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1800,
                        handshake_length=1795,
                        ech_payload_length=176,
                    )
                ),
                encoding="utf-8",
            )
            (fixtures / "chrome_b.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_b",
                        fixture_id="chrome_b:frame5",
                        source_sha256="b" * 64,
                        extensions=["0x002B", "0x0000", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1832,
                        handshake_length=1827,
                        ech_payload_length=208,
                    )
                ),
                encoding="utf-8",
            )

            oracle = baselines.build_family_lane_oracle_for_tests(fixtures)

        lane = oracle[("chromium_linux_desktop", "non_ru_egress")]
        self.assertEqual("exact", lane["fields"]["non_grease_cipher_suites_ordered"]["status"])
        self.assertEqual(["0x1301", "0x1302"], lane["fields"]["non_grease_cipher_suites_ordered"]["value"])
        self.assertEqual("exact", lane["fields"]["non_grease_extension_set"]["status"])
        self.assertEqual(
            ["0x0000", "0x0010", "0x002B", "0x44CD", "0xFE0D"],
            lane["fields"]["non_grease_extension_set"]["value"],
        )
        self.assertEqual({"5": 2}, lane["fields"]["non_grease_extension_count_histogram"]["value"])
        self.assertEqual([1800, 1832], lane["fields"]["record_lengths"]["value"])
        self.assertEqual([1795, 1827], lane["fields"]["handshake_lengths"]["value"])
        self.assertEqual([176, 208], lane["fields"]["ech_payload_lengths"]["value"])

    def test_mixed_exact_field_is_not_silently_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            (fixtures / "one.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_one",
                        fixture_id="chrome_one:frame5",
                        source_sha256="c" * 64,
                        extensions=["0x0000", "0x002B", "0x0010"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=100,
                        handshake_length=95,
                        ech_payload_length=None,
                    )
                ),
                encoding="utf-8",
            )
            (fixtures / "two.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome_two",
                        fixture_id="chrome_two:frame5",
                        source_sha256="d" * 64,
                        extensions=["0x0000", "0x002B", "0x0010"],
                        cipher_suites=["0x1301", "0x1303"],
                        supported_groups=["0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=100,
                        handshake_length=95,
                        ech_payload_length=None,
                    )
                ),
                encoding="utf-8",
            )

            oracle = baselines.build_family_lane_oracle_for_tests(fixtures)

        lane = oracle[("chromium_linux_desktop", "non_ru_egress")]
        self.assertEqual("mixed", lane["fields"]["non_grease_cipher_suites_ordered"]["status"])
        self.assertEqual(
            [
                ["0x1301", "0x1302"],
                ["0x1301", "0x1303"],
            ],
            lane["fields"]["non_grease_cipher_suites_ordered"]["observed_values"],
        )

    def test_generated_cpp_contains_field_status_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fixtures = root / "fixtures"
            fixtures.mkdir()
            output = root / "ReviewedFamilyLaneBaselines.h"
            (fixtures / "chrome.clienthello.json").write_text(
                json.dumps(
                    artifact(
                        profile_id="chrome146_177_linux_desktop",
                        fixture_id="chrome146_177_linux_desktop:frame5",
                        source_sha256="e" * 64,
                        extensions=["0x0000", "0x002B", "0x0010", "0xFE0D", "0x44CD"],
                        cipher_suites=["0x1301", "0x1302"],
                        supported_groups=["0x11EC", "0x001D"],
                        supported_versions=["0x0304", "0x0303"],
                        record_length=1800,
                        handshake_length=1795,
                        ech_payload_length=176,
                    )
                ),
                encoding="utf-8",
            )

            baselines.generate_family_lane_baselines_for_tests(fixtures, output)
            text = output.read_text(encoding="utf-8")

        self.assertIn("enum class EvidenceFieldStatus", text)
        self.assertIn("non_grease_cipher_suites_status", text)
        self.assertIn("non_grease_extension_set_status", text)
        self.assertIn("non_grease_extension_count_histogram", text)
        self.assertIn("observed_handshake_lengths", text)
        self.assertIn("observed_record_lengths", text)
