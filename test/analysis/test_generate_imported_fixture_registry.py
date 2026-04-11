# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from generate_imported_fixture_registry import refresh_imported_candidate_corpus


def write_clienthello_artifact(path: pathlib.Path, *, profile_id: str, source_path: pathlib.Path, route_mode: str, samples: list[dict]) -> None:
    payload = {
        "profile_id": profile_id,
        "route_mode": route_mode,
        "scenario_id": f"{profile_id}-scenario",
        "source_path": str(source_path.resolve()),
        "source_sha256": "capture-sha256",
        "source_kind": "browser_capture",
        "device_class": "desktop",
        "os_family": "windows",
        "transport": "tcp",
        "samples": samples,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def write_serverhello_artifact(path: pathlib.Path, *, family: str, source_path: pathlib.Path, route_mode: str) -> None:
    payload = {
        "route_mode": route_mode,
        "scenario_id": f"{family}-serverhello",
        "source_path": str(source_path.resolve()),
        "source_sha256": "capture-sha256",
        "parser_version": "tls-serverhello-parser-v1",
        "samples": [
            {
                "fixture_id": f"{family}:frame8",
                "family": family,
                "selected_version": "0x0304",
                "cipher_suite": "0x1301",
                "extensions": ["0x002b", "0x0033"],
                "record_layout_signature": [22, 20],
            }
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


class GenerateImportedFixtureRegistryTest(unittest.TestCase):
    def test_generates_candidate_registry_and_normalizes_route_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            base = pathlib.Path(temp_dir)
            chrome_capture_path = base / "captures" / "chrome.pcapng"
            safari_capture_path = base / "captures" / "safari.pcapng"
            chrome_capture_path.parent.mkdir(parents=True, exist_ok=True)
            chrome_capture_path.write_text("chrome-capture\n", encoding="utf-8")
            safari_capture_path.write_text("safari-capture\n", encoding="utf-8")

            clienthello_root = base / "imported" / "clienthello"
            serverhello_root = base / "imported" / "serverhello"
            manifest_path = base / "imported" / "import_manifest.json"
            registry_path = base / "profiles_imported.json"

            write_clienthello_artifact(
                clienthello_root / "windows" / "chrome.clienthello.json",
                profile_id="chrome146_windows_test",
                source_path=chrome_capture_path,
                route_mode="unknown",
                samples=[
                    {
                        "fixture_id": "chrome146_windows_test:frame1",
                        "cipher_suites": ["0x1301"],
                        "supported_groups": ["0x001D", "0x11EC"],
                        "key_share_entries": [{"group": "0x001D"}, {"group": "0x11EC"}],
                        "non_grease_extensions_without_padding": ["0x000D", "0x002B", "0x44CD"],
                        "alpn_protocols": ["h2"],
                        "extensions": [{"type": "0xFE0D", "body_hex": ""}, {"type": "0x44CD", "body_hex": ""}],
                        "ech": {"payload_length": 208},
                    },
                    {
                        "fixture_id": "chrome146_windows_test:frame2",
                        "cipher_suites": ["0x1301"],
                        "supported_groups": ["0x001D"],
                        "key_share_entries": [{"group": "0x001D"}],
                        "non_grease_extensions_without_padding": ["0x002B", "0x000D"],
                        "alpn_protocols": ["h2"],
                        "extensions": [],
                        "ech": None,
                    },
                ],
            )
            write_clienthello_artifact(
                clienthello_root / "ios" / "safari.clienthello.json",
                profile_id="safari26_ios_test",
                source_path=safari_capture_path,
                route_mode="unknown",
                samples=[
                    {
                        "fixture_id": "safari26_ios_test:frame1",
                        "cipher_suites": ["0x1301"],
                        "supported_groups": ["0x001D"],
                        "key_share_entries": [{"group": "0x001D"}],
                        "non_grease_extensions_without_padding": ["0x000D", "0x002B"],
                        "alpn_protocols": ["h2"],
                        "extensions": [],
                        "ech": None,
                    }
                ],
            )
            write_serverhello_artifact(
                serverhello_root / "windows" / "chrome.serverhello.json",
                family="chrome146_windows_test",
                source_path=chrome_capture_path,
                route_mode="unknown",
            )
            write_serverhello_artifact(
                serverhello_root / "ios" / "safari.serverhello.json",
                family="safari26_ios_test",
                source_path=safari_capture_path,
                route_mode="unknown",
            )

            manifest_payload = {
                "version": "imported-capture-corpus-v1",
                "entries": [
                    {
                        "capture_path": str(chrome_capture_path.resolve()),
                        "profile_id": "chrome146_windows_test",
                        "browser_alias": "chrome",
                        "route_mode": "unknown",
                    },
                    {
                        "capture_path": str(safari_capture_path.resolve()),
                        "profile_id": "safari26_ios_test",
                        "browser_alias": "safari",
                        "route_mode": "unknown",
                    },
                ],
            }
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text(json.dumps(manifest_payload), encoding="utf-8")

            registry = refresh_imported_candidate_corpus(
                clienthello_root,
                serverhello_root,
                manifest_path,
                registry_path,
                "non_ru_egress",
            )

            self.assertIn("chrome146_windows_test", registry["profiles"])
            self.assertEqual(
                "ChromeShuffleAnchored",
                registry["profiles"]["chrome146_windows_test"]["extension_order_policy"],
            )
            self.assertEqual(
                {"allow_present": True, "allow_absent": True},
                registry["profiles"]["chrome146_windows_test"]["ech_type"],
            )
            self.assertEqual(
                {"allowed_types": ["0x44CD"], "allow_absent": True},
                registry["profiles"]["chrome146_windows_test"]["alps_type"],
            )
            self.assertEqual(
                {"allowed_groups": ["0x11EC"], "allow_absent": True},
                registry["profiles"]["chrome146_windows_test"]["pq_group"],
            )
            self.assertEqual(
                "FixedFromFixture",
                registry["profiles"]["safari26_ios_test"]["extension_order_policy"],
            )
            self.assertIn("chrome146_windows_test", registry["server_hello_matrix"])

            rewritten_clienthello = json.loads((clienthello_root / "windows" / "chrome.clienthello.json").read_text())
            rewritten_serverhello = json.loads((serverhello_root / "windows" / "chrome.serverhello.json").read_text())
            rewritten_manifest = json.loads(manifest_path.read_text())
            self.assertEqual("non_ru_egress", rewritten_clienthello["route_mode"])
            self.assertEqual("non_ru_egress", rewritten_serverhello["route_mode"])
            self.assertTrue(all(entry["route_mode"] == "non_ru_egress" for entry in rewritten_manifest["entries"]))
            self.assertTrue(registry_path.exists())


if __name__ == "__main__":
    unittest.main()