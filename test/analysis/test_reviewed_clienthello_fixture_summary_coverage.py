# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import re
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
HEADER_PATH = THIS_DIR.parent / "stealth" / "ReviewedClientHelloFixtures.h"
SOURCE_PATH_RE = re.compile(r'SourcePath\[\]\s*=\s*((?:"[^"]*"\s*)+);')

from merge_client_hello_fixture_summary import merge_artifacts


def parse_cxx_adjacent_string_literals(block: str) -> str:
    return "".join(re.findall(r'"([^"]*)"', block))


def make_artifact(source_path: str, fixture_id: str, scenario_id: str) -> dict:
    return {
        "profile_id": scenario_id,
        "route_mode": "non_ru_egress",
        "scenario_id": scenario_id,
        "source_path": source_path,
        "source_sha256": "a" * 64,
        "capture_date_utc": "2026-04-08T00:00:00Z",
        "parser_version": "tls-clienthello-parser-v1",
        "samples": [
            {
                "fixture_id": fixture_id,
                "sni": "example.com",
                "frame_number": 5,
                "tcp_stream": 0,
                "record_length": 100,
                "handshake_length": 96,
                "cipher_suites": ["0x1301"],
                "non_grease_cipher_suites": ["0x1301"],
                "supported_groups": ["0x001D"],
                "non_grease_supported_groups": ["0x001D"],
                "extension_types": ["0x000A", "0x0010"],
                "non_grease_extensions_without_padding": ["0x000A", "0x0010"],
                "alpn_protocols": ["h2"],
                "compress_certificate_algorithms": [],
                "key_share_entries": [{"group": "0x001D", "key_exchange_length": 32, "is_grease_group": False}],
                "ech": None,
            }
        ],
    }


class ReviewedClientHelloFixtureSummaryCoverageTest(unittest.TestCase):
    def test_merge_artifacts_recurses_platform_dirs_and_skips_legacy_batch1(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            (root / "linux_desktop").mkdir()
            (root / "android").mkdir()
            (root / "batch1").mkdir()

            (root / "linux_desktop" / "linux.clienthello.json").write_text(
                json.dumps(
                    make_artifact(
                        "/captures/Traffic dumps/Linux, desktop/linux.pcapng",
                        "linux_fixture:frame5",
                        "linux_fixture",
                    )
                ),
                encoding="utf-8",
            )
            (root / "android" / "android.clienthello.json").write_text(
                json.dumps(
                    make_artifact(
                        "/captures/Traffic dumps/Android/android.pcapng",
                        "android_fixture:frame5",
                        "android_fixture",
                    )
                ),
                encoding="utf-8",
            )
            (root / "batch1" / "legacy.clienthello.json").write_text(
                json.dumps(
                    make_artifact(
                        "/captures/batch1/legacy.pcapng",
                        "legacy_fixture:frame5",
                        "legacy_fixture",
                    )
                ),
                encoding="utf-8",
            )

            merged = merge_artifacts(root)

        self.assertIn("linux_fixture:frame5", merged)
        self.assertIn("android_fixture:frame5", merged)
        self.assertNotIn("legacy_fixture:frame5", merged)
        self.assertNotIn("/captures/batch1/legacy.pcapng", merged)

    def test_checked_in_reviewed_header_covers_live_platform_capture_dirs(self) -> None:
        content = HEADER_PATH.read_text(encoding="utf-8")
        source_paths = [parse_cxx_adjacent_string_literals(block) for block in SOURCE_PATH_RE.findall(content)]

        self.assertTrue(source_paths)
        for expected_marker in {
            "docs/Samples/Traffic dumps/Android",
            "docs/Samples/Traffic dumps/iOS",
            "docs/Samples/Traffic dumps/Linux, desktop",
            "docs/Samples/Traffic dumps/macOS",
        }:
            self.assertTrue(
                any(expected_marker in source_path for source_path in source_paths),
                msg=f"reviewed header missing capture path from {expected_marker}",
            )


if __name__ == "__main__":
    unittest.main()